#include "../include/xivres/excel.type2gen.h"
#include "../include/xivres/util.span_cast.h"

#include <numeric>

using namespace xivres::util;

uint32_t xivres::excel::type2gen::calculate_fixed_data_size(const std::vector<exh::column>& columns) {
	uint16_t size = 0;
	for (const auto& col : columns) {
		switch (col.Type) {
			case cell_type::PackedBool0:
			case cell_type::PackedBool1:
			case cell_type::PackedBool2:
			case cell_type::PackedBool3:
			case cell_type::PackedBool4:
			case cell_type::PackedBool5:
			case cell_type::PackedBool6:
			case cell_type::PackedBool7:
			case cell_type::Bool:
			case cell_type::Int8:
			case cell_type::UInt8:
				size = (std::max<uint16_t>)(size, col.Offset + 1);
				break;

			case cell_type::Int16:
			case cell_type::UInt16:
				size = (std::max<uint16_t>)(size, col.Offset + 2);
				break;

			case cell_type::String:
			case cell_type::Int32:
			case cell_type::UInt32:
			case cell_type::Float32:
				size = (std::max<uint16_t>)(size, col.Offset + 4);
				break;

			case cell_type::Int64:
			case cell_type::UInt64:
				size = (std::max<uint16_t>)(size, col.Offset + 8);
				break;

			default:
				throw std::invalid_argument(std::format("Invald column type {}", static_cast<uint32_t>(*col.Type)));
		}
	}
	return xivres::align<uint32_t>(size, 4).Alloc;
}

xivres::excel::type2gen::type2gen(std::string name, std::vector<exh::column> columns, exh::read_strategy readStrategy, size_t divideUnit, uint32_t fixedDataSize)
	: Name(std::move(name))
	, Columns(std::move(columns))
	, ReadStrategy(readStrategy)
	, DivideUnit(divideUnit)
	, FixedDataSize(fixedDataSize ? fixedDataSize : calculate_fixed_data_size(Columns)) {
	if (fixedDataSize && fixedDataSize < calculate_fixed_data_size(Columns))
		throw std::invalid_argument(std::format("Fixed data size {} is smaller than the columns need", fixedDataSize));
}

void xivres::excel::type2gen::add_language(game_language language) {
	if (const auto it = std::ranges::lower_bound(Languages, language);
		it == Languages.end() || *it != language)
		Languages.insert(it, language);
}

const std::vector<xivres::excel::cell>& xivres::excel::type2gen::get_row(uint32_t id, game_language language) const {
	return Data.at(id).at(language);
}

void xivres::excel::type2gen::set_row(uint32_t id, game_language language, std::vector<cell> row, bool replace) {
	if (!row.empty() && row.size() != Columns.size())
		throw std::invalid_argument(std::format("bad column data (expected {} columns, got {} columns)", Columns.size(), row.size()));
	auto& target = Data[id][language];
	if (target.empty() || replace)
		target = std::move(row);
}

std::pair<xivres::path_spec, std::vector<char>> xivres::excel::type2gen::flush(uint32_t startId, const std::map<uint32_t, std::vector<char>>& rows, game_language language) const {
	exd::header exdHeader;
	const auto exdHeaderSpan = span_cast<char>(1, &exdHeader);
	std::memcpy(exdHeader.Signature, exd::header::Signature_Value, 4);
	exdHeader.Version = exd::header::Version_Value;

	size_t dataSize = 0;
	for (const auto& row : rows | std::views::values)
		dataSize += row.size();
	exdHeader.DataSize = static_cast<uint32_t>(dataSize);

	std::vector<exd::row::locator> locators;
	auto offsetAccumulator = static_cast<uint32_t>(sizeof exdHeader + rows.size() * sizeof(exd::row::locator));
	for (const auto& row : rows) {
		locators.emplace_back(row.first, offsetAccumulator);
		offsetAccumulator += static_cast<uint32_t>(row.second.size());
	}
	const auto locatorSpan = span_cast<char>(locators);
	exdHeader.IndexSize = static_cast<uint32_t>(locatorSpan.size_bytes());

	std::vector<char> exdFile;
	exdFile.reserve(offsetAccumulator);
	std::copy_n(exdHeaderSpan.data(), exdHeaderSpan.size_bytes(), std::back_inserter(exdFile));
	std::copy_n(locatorSpan.data(), locatorSpan.size_bytes(), std::back_inserter(exdFile));
	for (const auto& row : rows | std::views::values)
		std::copy_n(row.data(), row.size(), std::back_inserter(exdFile));

	if (auto pcszLangCode = game_language_code(language))
		return std::make_pair(path_spec(std::format("exd/{}_{}_{}.exd", Name, startId, pcszLangCode)), std::move(exdFile));
	else
		return std::make_pair(path_spec(std::format("exd/{}_{}.exd", Name, startId)), std::move(exdFile));
}

std::map<xivres::path_spec, std::vector<char>, xivres::path_spec::FullPathComparator> xivres::excel::type2gen::compile() {
	std::map<path_spec, std::vector<char>, path_spec::FullPathComparator> result;

	std::vector<std::pair<exh::page, std::vector<uint32_t>>> pages;
	for (const auto id : Data | std::views::keys) {
		if (pages.empty()) {
			pages.emplace_back();
		} else if (pages.back().second.size() == DivideUnit || DivideAtIds.contains(id)) {
			pages.back().first.RowCountWithSkip = pages.back().second.back() - pages.back().second.front() + 1;
			pages.emplace_back();
		}

		if (pages.back().second.empty())
			pages.back().first.StartId = id;
		pages.back().second.push_back(id);
	}
	if (pages.empty())
		return result;

	pages.back().first.RowCountWithSkip = pages.back().second.back() - pages.back().second.front() + 1;

	// Strings are stored in the order of their columns' offsets, as the game's files do.
	std::vector<size_t> columnOrder(Columns.size());
	std::iota(columnOrder.begin(), columnOrder.end(), size_t{});
	std::ranges::stable_sort(columnOrder, {}, [this](size_t i) { return *Columns[i].Offset; });

	for (const auto& page : pages) {
		for (const auto language : Languages) {
			std::map<uint32_t, std::vector<char>> rows;
			for (const auto id : page.second) {
				std::vector<char> row(sizeof(exd::row::header) + FixedDataSize);

				constexpr auto fixedDataOffset = sizeof(exd::row::header);
				const auto variableDataOffset = fixedDataOffset + FixedDataSize;

				auto sourceLanguage = language;
				auto& rowSet = Data[id];
				if (!rowSet.contains(sourceLanguage)) {
					sourceLanguage = game_language::Unspecified;
					for (auto lang : FillMissingLanguageFrom) {
						if (rowSet.contains(lang)) {
							sourceLanguage = lang;
							break;
						}
					}
					if (sourceLanguage == game_language::Unspecified)
						continue;
				}

				auto& columns = rowSet[sourceLanguage];
				if (columns.empty())
					continue;

				for (const auto i : columnOrder) {
					auto& column = columns[i];
					const auto& columnDefinition = Columns[i];
					switch (columnDefinition.Type) {
						case cell_type::String:
						{
							const auto stringOffset = BE<uint32_t>(static_cast<uint32_t>(row.size() - variableDataOffset));
							std::copy_n(reinterpret_cast<const char*>(&stringOffset), 4, &row[fixedDataOffset + columnDefinition.Offset]);
							row.insert(row.end(), column.String.escaped().begin(), column.String.escaped().end());
							row.push_back(0);
							column.ValidSize = 0;
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
						{
							column.ValidSize = 0;
							auto& packed = row[fixedDataOffset + columnDefinition.Offset];
							const auto mask = 1 << (static_cast<int>(column.Type) - static_cast<int>(cell_type::PackedBool0));
							packed = static_cast<char>(column.boolean ? packed | mask : packed & ~mask);
							break;
						}

						default:
							break;
					}
					if (column.ValidSize) {
						const auto target = std::span(row).subspan(fixedDataOffset + columnDefinition.Offset, column.ValidSize);
						std::copy_n(&column.Buffer[0], column.ValidSize, target.data());
						std::ranges::reverse(target);
					}
				}
				if (AlignRowData)
					row.resize(fixedDataOffset + xivres::align<size_t>(row.size() - fixedDataOffset, 4));
				else
					row.resize(xivres::align<size_t>(row.size(), 4));

				auto& rowHeader = *reinterpret_cast<exd::row::header*>(row.data());
				rowHeader.DataSize = static_cast<uint32_t>(row.size() - sizeof rowHeader);
				rowHeader.SubRowCount = 1;

				rows.emplace(id, std::move(row));
			}
			if (rows.empty())
				continue;
			result.emplace(flush(page.first.StartId, rows, language));
		}
	}

	{
		exh::header exhHeader;
		const auto exhHeaderSpan = span_cast<char>(1, &exhHeader);
		std::memcpy(exhHeader.Signature, exh::header::Signature_Value, 4);
		exhHeader.Version = exh::header::Version_Value;
		exhHeader.FixedDataSize = static_cast<uint16_t>(FixedDataSize);
		exhHeader.ColumnCount = static_cast<uint16_t>(Columns.size());
		exhHeader.PageCount = static_cast<uint16_t>(pages.size());
		exhHeader.LanguageCount = static_cast<uint16_t>(Languages.size());
		exhHeader.ReadStrategy = ReadStrategy;
		exhHeader.Variant = variant::Level2;
		exhHeader.RowCountWithoutSkip = static_cast<uint32_t>(Data.size());

		const auto columnSpan = span_cast<char>(Columns);
		std::vector<exh::page> paginations;
		for (const auto& pagination : pages | std::views::keys)
			paginations.emplace_back(pagination);
		const auto paginationSpan = span_cast<char>(paginations);
		const auto languageSpan = span_cast<char>(Languages);

		std::vector<char> exhFile;
		exhFile.reserve(exhHeaderSpan.size_bytes() + columnSpan.size_bytes() + paginationSpan.size_bytes() + languageSpan.size_bytes());
		std::copy_n(exhHeaderSpan.data(), exhHeaderSpan.size_bytes(), std::back_inserter(exhFile));
		std::copy_n(columnSpan.data(), columnSpan.size_bytes(), std::back_inserter(exhFile));
		std::copy_n(paginationSpan.data(), paginationSpan.size_bytes(), std::back_inserter(exhFile));
		std::copy_n(languageSpan.data(), languageSpan.size_bytes(), std::back_inserter(exhFile));
		result.emplace(std::format("exd/{}.exh", Name), std::move(exhFile));
	}

	return result;
}
