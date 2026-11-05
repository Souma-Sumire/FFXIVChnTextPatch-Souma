#include "../include/xivres/excel.h"

#include "../include/xivres/installation.h"
#include "../include/xivres/sqpack.reader.h"
#include "../include/xivres/unpacked_stream.h"

using namespace xivres::util;

xivres::excel::exl::reader::reader(const installation& installation)
	: reader(*installation.get_file("exd/root.exl")) {
}

xivres::excel::exl::reader::reader(const stream& strm) {
	std::string data(static_cast<size_t>(strm.size()), '\0');
	strm.read_fully(0, std::span(data));
	std::istringstream in(std::move(data));

	std::string line;
	for (size_t i = 0; std::getline(in, line); ++i) {
		line = trim(line);
		if (line.empty())
			continue; // skip empty lines
		auto splitted = split(line, std::string(","), 1);
		if (splitted.size() < 2)
			throw bad_data_error("Could not find delimiter character ','");

		char* end;
		const auto id = std::strtol(splitted[1].data(), &end, 10);
		if (end != &splitted[1].back() + 1)
			throw bad_data_error("Bad numeric specification");
		if (!i) {
			if (splitted[0] != "EXLT")
				throw bad_data_error("Bad header");
			m_version = id;
		} else {
			if (id != -1)
				m_idToNameMap.emplace(id, splitted[0]);
			m_nameToIdMap.emplace(std::move(splitted[0]), id);
		}
	}
}

xivres::excel::exh::reader::reader(const installation& installation, std::string name, bool /*strict*/)
	: reader(name, *installation.get_file(std::format("exd/{}.exh", name))) {}

xivres::excel::exh::reader::reader(std::string name, const stream& strm, bool strict)
	: m_name(std::move(name))
	, m_header(strm.read_fully<exh::header>(0))
	, m_columns(strm.read_vector<column>(sizeof m_header, m_header.ColumnCount))
	, m_pages(strm.read_vector<page>(static_cast<std::streamoff>(sizeof m_header + std::span(m_columns).size_bytes()), m_header.PageCount))
	, m_languages(strm.read_vector<game_language>(static_cast<std::streamoff>(sizeof m_header + std::span(m_columns).size_bytes() + std::span(m_pages).size_bytes()), m_header.LanguageCount)) {
	if (strict) {
		const auto dataLength = static_cast<size_t>(strm.size());
		const auto expectedLength = sizeof m_header + std::span(m_columns).size_bytes() + std::span(m_pages).size_bytes() + std::span(m_languages).size_bytes();
		if (dataLength > expectedLength)
			throw bad_data_error(std::format("Extra {} byte(s) found", dataLength - expectedLength));
		if (m_header.Version != header::Version_Value)
			throw bad_data_error(std::format("Unsupported version {}", *m_header.Version));
	}
}

size_t xivres::excel::exh::reader::get_owning_page_index(uint32_t rowId) const {
	const auto it = std::ranges::upper_bound(m_pages, rowId, {}, [](const page& p) -> uint32_t { return p.StartId + p.RowCountWithSkip; });
	if (it == m_pages.end())
		throw std::out_of_range("RowId not in range");

	return std::distance(m_pages.begin(), it);
}

xivres::path_spec xivres::excel::exh::reader::get_exd_path(const page& page, game_language language) const {
	if (language == game_language::Unspecified)
		return std::format("exd/{}_{}.exd", m_name, *page.StartId);
	else
		return std::format("exd/{}_{}_{}.exd", m_name, *page.StartId, game_language_code(language));
}

xivres::excel::exd::row::reader::reader(std::span<const char> fixedData, std::span<const char> fullData, const std::vector<exh::column>& columns)
	: m_fixedData(fixedData)
	, m_fullData(fullData)
	, m_cells(columns.size())
	, Columns(columns) {
}

xivres::excel::cell& xivres::excel::exd::row::reader::at(size_t index) {
	if (index >= Columns.size())
		throw std::out_of_range("Index out of range");
	return resolve_cell(index);
}

xivres::excel::cell& xivres::excel::exd::row::reader::resolve_cell(size_t index) const {
	if (m_cells[index])
		return *m_cells[index];

	const auto& columnDefinition = Columns[index];
	auto& column = m_cells[index].emplace();
	column.Type = columnDefinition.Type;
	switch (column.Type) {
		case cell_type::String: {
			BE<uint32_t> stringOffset;
			std::copy_n(&m_fixedData[columnDefinition.Offset], 4, reinterpret_cast<char*>(&stringOffset));
			column.String.escaped(&m_fullData[m_fixedData.size() + stringOffset]);
			break;
		}

		case cell_type::Bool:
		case cell_type::Int8:
		case cell_type::UInt8:
			column.ValidSize = 1;
			break;

		case cell_type::Int16:
		case cell_type::UInt16:
			column.ValidSize = 2;
			break;

		case cell_type::Int32:
		case cell_type::UInt32:
		case cell_type::Float32:
			column.ValidSize = 4;
			break;

		case cell_type::Int64:
		case cell_type::UInt64:
			column.ValidSize = 8;
			break;

		case cell_type::PackedBool0:
		case cell_type::PackedBool1:
		case cell_type::PackedBool2:
		case cell_type::PackedBool3:
		case cell_type::PackedBool4:
		case cell_type::PackedBool5:
		case cell_type::PackedBool6:
		case cell_type::PackedBool7:
			column.boolean = m_fixedData[columnDefinition.Offset] & (1 << (static_cast<int>(column.Type) - static_cast<int>(cell_type::PackedBool0)));
			break;

		default:
			throw bad_data_error(std::format("Invald column type {}", static_cast<uint32_t>(column.Type)));
	}
	if (column.ValidSize) {
		std::copy_n(&m_fixedData[columnDefinition.Offset], column.ValidSize, &column.Buffer[0]);
		std::reverse(&column.Buffer[0], &column.Buffer[column.ValidSize]);
	}
	return column;
}

const xivres::excel::cell& xivres::excel::exd::row::reader::at(size_t index) const {
	if (index >= Columns.size())
		throw std::out_of_range("Index out of range");
	return resolve_cell(index);
}

xivres::excel::exd::row::buffer::buffer()
	: m_rowId(UINT32_MAX) {

}

xivres::excel::exd::row::buffer::buffer(uint32_t rowId, const exh::reader& exhReader, const stream& strm, std::streamoff offset)
	: m_rowId(rowId)
	, m_rowHeader(strm.read_fully<row::header>(offset))
	, m_buffer(strm.read_vector<char>(offset + static_cast<std::streamoff>(sizeof m_rowHeader), m_rowHeader.DataSize)) {
	m_rows.reserve(m_rowHeader.SubRowCount);

	if (exhReader.header().Variant == variant::Level2) {
		const auto fixedData = std::span(m_buffer).subspan(0, exhReader.header().FixedDataSize);

		for (size_t i = 0, i_ = m_rowHeader.SubRowCount; i < i_; i++)
			m_rows.emplace_back(fixedData, std::span(m_buffer), exhReader.get_columns());

	} else if (exhReader.header().Variant == variant::Level3) {
		for (size_t i = 0, i_ = m_rowHeader.SubRowCount; i < i_; ++i) {
			const auto baseOffset = i * (size_t() + 2 + exhReader.header().FixedDataSize);
			const auto fixedData = std::span(m_buffer).subspan(2 + baseOffset, exhReader.header().FixedDataSize);

			m_rows.emplace_back(fixedData, std::span(m_buffer), exhReader.get_columns());
		}

	} else {
		throw bad_data_error("Invalid excel depth");
	}
}

xivres::excel::exd::reader::reader(const exh::reader& exhReader, std::shared_ptr<const stream> strm)
	: Stream(std::move(strm))
	, ExhReader(exhReader)
	, Header(Stream->read_fully<header>(0)) {
	const auto count = Header.IndexSize / sizeof(row::locator);
	m_rowIds.reserve(count);
	m_offsets.reserve(count);
	m_rowBuffers.resize(count);

	std::vector<std::pair<uint32_t, uint32_t>> locators;
	locators.reserve(count);
	for (const auto& locator : Stream->read_vector<row::locator>(sizeof Header, count))
		locators.emplace_back(*locator.RowId, *locator.Offset);
	std::ranges::sort(locators);

	for (const auto& locator : locators) {
		m_rowIds.emplace_back(locator.first);
		m_offsets.emplace_back(locator.second);
	}
}

const xivres::excel::exd::row::buffer& xivres::excel::exd::reader::operator[](uint32_t rowId) const {
	const auto it = std::ranges::lower_bound(m_rowIds, rowId);
	if (it == m_rowIds.end() || *it != rowId)
		throw std::out_of_range("index out of range");

	const auto index = it - m_rowIds.begin();
	if (!m_rowBuffers[index]) {
		const auto lock = std::scoped_lock(m_populateMtx);
		if (!m_rowBuffers[index])
			m_rowBuffers[index].emplace(rowId, ExhReader, *Stream, m_offsets[index]);
	}

	return *m_rowBuffers[index];
}

[[nodiscard]] xivres::excel::reader xivres::installation::get_excel(const std::string& name) const {
	return {&get_sqpack(0x0a0000), name};
}

std::string xivres::installation::get_version(uint8_t expac) const {
	file_stream stream;
	if (expac == 0)
		stream = m_gamePath / "ffxivgame.ver";
	else
		stream = m_gamePath / std::format("sqpack/ex{}/ex{}.ver", expac, expac); 
	std::string res(stream.size(), '\0');
	stream.read_fully(0, std::span(res));
	return res;
}

xivres::excel::reader& xivres::excel::reader::operator=(reader&& r) noexcept {
	if (this == &r)
		return *this;

	m_sqpackReader = r.m_sqpackReader;
	m_exhReader = std::move(r.m_exhReader);
	m_language = r.m_language;
	m_exdReaders = std::move(r.m_exdReaders);
	r.clear();
	return *this;
}

xivres::excel::reader& xivres::excel::reader::operator=(const reader& r) {
	if (this == &r)
		return *this;

	m_sqpackReader = r.m_sqpackReader;
	m_exhReader = r.m_exhReader;
	m_language = r.m_language;
	m_exdReaders.clear();
	if (m_exhReader)
		m_exdReaders.resize(m_exhReader->get_pages().size());
	return *this;
}

xivres::excel::reader::reader(const sqpack::reader* sqpackReader, const std::string& name)
	: m_sqpackReader(sqpackReader)
	, m_exhReader(std::in_place, name, sqpackReader->packed_at(std::format("exd/{}.exh", name))->get_unpacked())
	, m_language(m_exhReader->get_languages().front())
	, m_exdReaders(m_exhReader->get_pages().size()) {
}

xivres::excel::reader::reader()
	: m_sqpackReader(nullptr)
	, m_language(game_language::Unspecified) {
}

xivres::excel::reader::reader(const reader& r)
	: m_sqpackReader(r.m_sqpackReader)
	, m_exhReader(r.m_exhReader)
	, m_language(r.m_language)
	, m_exdReaders(m_exhReader ? m_exhReader->get_pages().size() : 0) {
}

xivres::excel::reader::reader(reader&& r) noexcept
	: m_sqpackReader(r.m_sqpackReader)
	, m_exhReader(std::move(r.m_exhReader))
	, m_language(r.m_language)
	, m_exdReaders(std::move(r.m_exdReaders)) {
	r.clear();
}

void xivres::excel::reader::clear() {
	m_sqpackReader = nullptr;
	m_exhReader.reset();
	m_language = game_language::Unspecified;
	m_exdReaders.clear();
}

const xivres::game_language& xivres::excel::reader::get_language() const {
	return m_language;
}

xivres::excel::reader xivres::excel::reader::new_with_language(game_language language) {
	if (m_language == language)
		return *this;

	return reader(*this).set_language(language);
}

xivres::excel::reader& xivres::excel::reader::set_language(game_language language) {
	if (m_language == language)
		return *this;

	const auto lock = std::scoped_lock(m_populateMtx);
	m_language = language;
	for (auto& v : m_exdReaders)
		v.reset();
	return *this;
}

const xivres::excel::exh::reader& xivres::excel::reader::get_exh_reader() const {
	return *m_exhReader;
}

const xivres::excel::exd::reader& xivres::excel::reader::get_exd_reader(size_t pageIndex) const {
	if (!m_exdReaders[pageIndex]) {
		const auto lock = std::scoped_lock(m_populateMtx);
		if (!m_exdReaders[pageIndex])
			m_exdReaders[pageIndex] = std::make_unique<exd::reader>(*m_exhReader, m_sqpackReader->packed_at(m_exhReader->get_exd_path(m_exhReader->get_pages().at(pageIndex), m_language))->make_unpacked_ptr());
	}

	return *m_exdReaders[pageIndex];
}

const xivres::excel::exd::row::buffer& xivres::excel::reader::operator[](uint32_t rowId) const {
	return get_exd_reader(m_exhReader->get_owning_page_index(rowId))[rowId];
}
