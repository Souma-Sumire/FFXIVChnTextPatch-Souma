#include "../include/xivres/sqpack.generator.h"

#include <fstream>
#include <ranges>
#include <utility>

#include "../include/xivres/packed_stream.model.h"
#include "../include/xivres/packed_stream.placeholder.h"
#include "../include/xivres/packed_stream.standard.h"
#include "../include/xivres/packed_stream.texture.h"
#include "../include/xivres/sound.h"

xivres::packed_stream* xivres::sqpack::generator::add_result::any() const {
	if (!Added.empty())
		return Added[0];
	if (!Replaced.empty())
		return Replaced[0];
	if (!SkippedExisting.empty())
		return SkippedExisting[0];
	return nullptr;
}

std::vector<xivres::packed_stream*> xivres::sqpack::generator::add_result::all_success() const {
	std::vector<packed_stream*> res;
	res.insert(res.end(), Added.begin(), Added.end());
	res.insert(res.end(), Replaced.begin(), Replaced.end());
	res.insert(res.end(), SkippedExisting.begin(), SkippedExisting.end());
	return res;
}

xivres::sqpack::generator::entry_info* xivres::sqpack::generator::sqpack_views::find_entry(const path_spec& pathSpec) const {
	if (const auto it = HashOnlyEntries.find(pathSpec); it != HashOnlyEntries.end())
		return &it->second;
	if (const auto it = FullPathEntries.find(pathSpec); it != FullPathEntries.end())
		return &it->second;
	return nullptr;
}

xivres::sqpack::generator::entry_info& xivres::sqpack::generator::sqpack_views::get_entry(const path_spec& pathSpec) const {
	if (const auto it = HashOnlyEntries.find(pathSpec); it != HashOnlyEntries.end())
		return it->second;
	if (const auto it = FullPathEntries.find(pathSpec); it != FullPathEntries.end())
		return it->second;
	throw std::out_of_range("File not found");
}

std::shared_ptr<const xivres::packed_stream> xivres::sqpack::generator::entry_info::swap_stream(std::shared_ptr<const packed_stream> newStream) {
	if (newStream && newStream->size() > m_entrySize)
		throw std::invalid_argument("Provided strm requires more space than reserved size");
	const auto lock = std::scoped_lock(m_streamMtx);
	auto oldStream{std::move(m_stream)};
	m_stream = std::move(newStream);
	return oldStream;
}

std::shared_ptr<const xivres::packed_stream> xivres::sqpack::generator::entry_info::current_stream() const {
	auto current = [this] {
		const auto lock = std::scoped_lock(m_streamMtx);
		return m_stream ? m_stream : m_baseStream;
	}();

	if (current && std::cmp_greater(current->size(), m_entrySize))
		return nullptr;
	return current;
}

void xivres::sqpack::generator::entry_info::finalize_entry_size() {
	m_entrySize = align((std::max)(m_entrySize, static_cast<uint32_t>(m_baseStream->size()))).Alloc;
}

bool xivres::sqpack::generator::entry_info::try_keep_original_place() {
	m_keepsOriginalPlace = false;
	if (!m_originalPlace)
		return false;

	const auto& [locator, allocation] = *m_originalPlace;
	if (allocation > UINT32_MAX || m_entrySize > allocation || std::cmp_greater(m_baseStream->size(), allocation))
		return false;

	m_entrySize = static_cast<uint32_t>(allocation);
	m_locator = locator;
	m_keepsOriginalPlace = true;
	return true;
}

std::streamsize xivres::sqpack::generator::entry_info::read(std::streamoff offset, void* buf, std::streamsize length) const {
	if (std::cmp_greater_equal(offset, m_entrySize))
		return 0;
	if (offset + length > m_entrySize)
		length = m_entrySize - offset;

	auto target = std::span(static_cast<uint8_t*>(buf), static_cast<size_t>(length));
	const auto current = current_stream();
	const auto& underlyingStream = current ? *current : placeholder_packed_stream::instance();
	const auto underlyingStreamLength = underlyingStream.size();
	const auto dataLength = offset < underlyingStreamLength ? (std::min)(length, underlyingStreamLength - offset) : 0;

	if (offset < underlyingStreamLength) {
		const auto dataTarget = target.subspan(0, static_cast<size_t>(dataLength));
		const auto readLength = static_cast<size_t>(underlyingStream.read(offset, dataTarget.data(), static_cast<std::streamsize>(dataTarget.size_bytes())));
		if (readLength != dataTarget.size_bytes())
			throw std::logic_error("entry_info underlying data read fail");
		target = target.subspan(readLength);
	}
	std::ranges::fill(target, 0);
	return length;
}

uint64_t xivres::sqpack::generator::entry_info::data_size() const {
	const auto current = current_stream();
	return (std::min<uint64_t>)(m_entrySize, current ? current->size() : placeholder_packed_stream::instance().size());
}

void xivres::sqpack::generator::entry_info::hold_until(std::chrono::steady_clock::time_point until) const {
	if (const auto current = current_stream())
		current->hold_until(until);
}

xivres::packed::type xivres::sqpack::generator::entry_info::get_packed_type() const {
	const auto current = current_stream();
	return current ? current->get_packed_type() : placeholder_packed_stream::instance().get_packed_type();
}

xivres::stream_source xivres::sqpack::generator::entry_info::source_impl() const {
	const auto current = current_stream();
	return current ? current->source() : stream_source{};
}

xivres::sqpack::generator::add_result& xivres::sqpack::generator::add_result::operator+=(add_result& r) {
	Added.insert(Added.end(), r.Added.begin(), r.Added.end());
	Replaced.insert(Replaced.end(), r.Replaced.begin(), r.Replaced.end());
	SkippedExisting.insert(SkippedExisting.end(), r.SkippedExisting.begin(), r.SkippedExisting.end());
	Error.insert(Error.end(), r.Error.begin(), r.Error.end());
	r.Added.clear();
	r.Replaced.clear();
	r.SkippedExisting.clear();
	r.Error.clear();
	return *this;
}

xivres::sqpack::generator::add_result& xivres::sqpack::generator::add_result::operator+=(const add_result& r) {
	Added.insert(Added.end(), r.Added.begin(), r.Added.end());
	Replaced.insert(Replaced.end(), r.Replaced.begin(), r.Replaced.end());
	SkippedExisting.insert(SkippedExisting.end(), r.SkippedExisting.begin(), r.SkippedExisting.end());
	Error.insert(Error.end(), r.Error.begin(), r.Error.end());
	return *this;
}

namespace {
	constexpr std::chrono::seconds StreamMaximumInterval{60};
	constexpr uint64_t StreamWindowSize = 0x30000;

	std::optional<std::chrono::steady_clock::duration> predict_window_interval(const xivres::sqpack::generator::entry_info& entry) {
		try {
			// The entry is owned elsewhere and outlives this.
			const auto unpacked = std::make_shared<xivres::unpacked_stream>(std::shared_ptr<const xivres::packed_stream>(std::shared_ptr<void>(), &entry));
			const auto bytesPerSecond = xivres::sound::reader(unpacked).stream_bytes_per_second(0);
			if (!bytesPerSecond || *bytesPerSecond <= 0)
				return std::nullopt;
			return std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(StreamWindowSize / *bytesPerSecond));
		} catch (...) {
			return std::nullopt;
		}
	}
}

bool xivres::sqpack::generator::sqpack_view_entry_cache::read(const entry_info& entry, bool streamed, uint64_t offset, std::span<uint8_t> out) {
	if (streamed)
		return read_streamed(entry, offset);

	const auto now = clock::now();
	const auto thread = std::this_thread::get_id();
	std::shared_ptr<buffer> buf;
	{
		const auto lock = std::scoped_lock(m_mtx);
		expire_locked(now);

		auto& current = m_readers[thread];
		if (current.Entry != &entry || offset == 0) {
			current = {};
			if (const auto it = m_buffers.find(&entry); it != m_buffers.end())
				buf = it->second.lock();
			if (!buf) {
				if (offset != 0 || entry.entry_size() > MaxBufferedEntrySize) {
					m_readers.erase(thread);
					return false;
				}
				buf = std::make_shared<buffer>();
				m_buffers.insert_or_assign(&entry, buf);
			}
			current.Entry = &entry;
			current.Buffer = buf;
		} else
			buf = current.Buffer;
		current.LastRead = now;
	}

	{
		const auto lock = std::scoped_lock(buf->FillMtx);
		if (!buf->Filled) {
			buf->Filled = true;
			try {
				buf->Data.resize(static_cast<size_t>(entry.data_size()));
				entry.read_fully(0, buf->Data.data(), static_cast<std::streamsize>(buf->Data.size()));
			} catch (...) {
				buf->Failed = true;
				std::vector<uint8_t>().swap(buf->Data);
			}
		}
	}

	const auto& data = buf->Data;
	const auto done = buf->Failed || offset + out.size() >= data.size();
	if (!buf->Failed) {
		const auto available = offset < data.size() ? static_cast<size_t>((std::min<uint64_t>)(out.size(), data.size() - offset)) : 0;
		std::copy_n(data.begin() + static_cast<ptrdiff_t>(offset), available, out.begin());
		std::fill(out.begin() + static_cast<ptrdiff_t>(available), out.end(), 0);
	}

	if (done) {
		const auto lock = std::scoped_lock(m_mtx);
		if (const auto it = m_readers.find(thread); it != m_readers.end() && it->second.Buffer == buf)
			m_readers.erase(it);
	}
	return !buf->Failed;
}

bool xivres::sqpack::generator::sqpack_view_entry_cache::read_streamed(const entry_info& entry, uint64_t offset) {
	if (offset != 0)
		return false;

	const auto now = clock::now();
	bool predict;
	{
		const auto lock = std::scoped_lock(m_mtx);
		expire_locked(now);

		auto& state = m_streams[&entry];
		if (state.LastWindow != clock::time_point{}) {
			if (const auto interval = now - state.LastWindow; interval <= StreamMaximumInterval)
				state.LongestInterval = (std::max)(state.LongestInterval, interval);
			else
				state.LongestInterval = {};
		}
		state.LastWindow = now;
		predict = !std::exchange(state.Predicted, true);
	}

	// Reads from the file, so not under the lock.
	const auto predicted = predict ? predict_window_interval(entry) : std::nullopt;
	if (predict)
		OnStreamWindowPredicted(entry.path_spec(), predicted);

	clock::duration hold;
	{
		const auto lock = std::scoped_lock(m_mtx);
		auto& state = m_streams[&entry];
		if (predict)
			state.PredictedInterval = predicted;

		const auto observed = state.LongestInterval * 3 / 2;
		if (state.PredictedInterval)
			hold = (std::max<clock::duration>)({*state.PredictedInterval * 3 / 2, observed, StreamPredictedMinimumHold});
		else
			hold = (std::max<clock::duration>)(observed, StreamMinimumHold);
	}

	entry.hold_until(now + hold);
	return false;
}

void xivres::sqpack::generator::sqpack_view_entry_cache::expire_locked(clock::time_point now) {
	std::erase_if(m_readers, [now](const auto& item) { return now - item.second.LastRead > ReaderTimeout; });
	std::erase_if(m_buffers, [](const auto& item) { return item.second.expired(); });
	std::erase_if(m_streams, [now](const auto& item) { return now - item.second.LastWindow > StreamMaximumInterval; });
}

void xivres::sqpack::generator::sqpack_view_entry_cache::flush() {
	const auto lock = std::scoped_lock(m_mtx);
	m_readers.clear();
	m_buffers.clear();
}

xivres::sqpack::generator::generator(std::string ex, std::string name, uint64_t maxFileSize)
	: m_maxFileSize(maxFileSize)
	, DatExpac(std::move(ex))
	, DatName(std::move(name)) {
	if (maxFileSize > sqdata::header::MaxFileSize_MaxValue)
		throw std::invalid_argument("MaxFileSize cannot be more than 32GiB.");
}

void xivres::sqpack::generator::add(add_result& result, std::shared_ptr<packed_stream> provider, bool overwriteExisting) {
	const auto pProvider = provider.get();

	try {
		entry_info* pEntry = nullptr;

		if (const auto it = m_hashOnlyEntries.find(provider->path_spec()); it != m_hashOnlyEntries.end()) {
			pEntry = &it->second;
			if (!pEntry->path_spec().has_original() && provider->path_spec().has_original()) {
				pEntry->update_path_spec(provider->path_spec());
				auto node = m_hashOnlyEntries.extract(it);
				node.key() = pEntry->path_spec();
				pEntry = &m_fullEntries.insert(std::move(node)).position->second;
			}
		} else if (const auto itFull = m_fullEntries.find(provider->path_spec()); itFull != m_fullEntries.end()) {
			pEntry = &itFull->second;
		}

		if (pEntry) {
			if (!overwriteExisting) {
				pEntry->update_path_spec(provider->path_spec());
				result.SkippedExisting.emplace_back(pEntry);
				return;
			}
			pEntry->reset_base_stream(std::move(provider));
			result.Replaced.emplace_back(pProvider);
			return;
		}

		const auto pathSpec = pProvider->path_spec();
		if (pathSpec.has_original())
			m_fullEntries.try_emplace(pathSpec, pathSpec, std::move(provider));
		else
			m_hashOnlyEntries.try_emplace(pathSpec, pathSpec, std::move(provider));
		result.Added.emplace_back(pProvider);
	} catch (const std::exception& e) {
		result.Error.emplace_back(pProvider->path_spec(), e.what());
	}
}

xivres::sqpack::generator::add_result xivres::sqpack::generator::add(std::shared_ptr<packed_stream> provider, bool overwriteExisting) {
	add_result result;
	add(result, std::move(provider), overwriteExisting);
	return result;
}

xivres::sqpack::generator::add_result xivres::sqpack::generator::add_sqpack(const std::filesystem::path& indexPath, bool overwriteExisting, bool overwriteUnknownSegments, bool keepOriginalLayout) {
	return add_sqpack(reader::from_path(indexPath), overwriteExisting, overwriteUnknownSegments, keepOriginalLayout);
}

xivres::sqpack::generator::add_result xivres::sqpack::generator::add_sqpack(const reader& reader, bool overwriteExisting, bool overwriteUnknownSegments, bool keepOriginalLayout) {
	if (keepOriginalLayout) {
		if (!m_originalData.empty())
			throw std::logic_error("The layout of another sqpack is already kept.");
		for (const auto& data : reader.Data)
			m_originalData.emplace_back(data.Stream);
	}

	if (overwriteUnknownSegments) {
		m_sqpackIndexSegment3 = { reader.Index1.segment_3().begin(), reader.Index1.segment_3().end() };
		m_sqpackIndex2Segment3 = { reader.Index2.segment_3().begin(), reader.Index2.segment_3().end() };
	}

	add_result result;
	for (const auto& entryInfo : reader.Entries) {
		try {
			const auto added = result.Added.size();
			add(result, reader.packed_at(entryInfo), overwriteExisting);
			if (keepOriginalLayout && result.Added.size() != added)
				find_entry_mutable(entryInfo.PathSpec)->original_place(entryInfo.Locator, entryInfo.Allocation);
		} catch (const std::exception& e) {
			result.Error.emplace_back(entryInfo.PathSpec, e.what());
		}
	}
	return result;
}

const xivres::sqpack::generator::entry_info* xivres::sqpack::generator::find_entry(const path_spec& pathSpec) const {
	if (const auto it = m_hashOnlyEntries.find(pathSpec); it != m_hashOnlyEntries.end())
		return &it->second;
	if (const auto it = m_fullEntries.find(pathSpec); it != m_fullEntries.end())
		return &it->second;
	return nullptr;
}

xivres::sqpack::generator::entry_info* xivres::sqpack::generator::find_entry_mutable(const path_spec& pathSpec) {
	return const_cast<entry_info*>(find_entry(pathSpec));
}

xivres::sqpack::generator::add_result xivres::sqpack::generator::add_file(path_spec pathSpec, const std::filesystem::path& path, bool overwriteExisting) {
	std::shared_ptr<packed_stream> provider;

	auto extensionLower = path.extension().u8string();
	for (auto& c : extensionLower)
		if (u8'A' <= c && c <= u8'Z')
			c += 'a' - 'A';

	if (file_size(path) == 0) {
		provider = std::make_shared<placeholder_packed_stream>(std::move(pathSpec));
	} else if (extensionLower == u8".tex" || extensionLower == u8".atex") {
		provider = std::make_shared<passthrough_packed_stream<texture_passthrough_packer>>(std::move(pathSpec), std::make_shared<file_stream>(path));
	} else if (extensionLower == u8".mdl") {
		provider = std::make_shared<passthrough_packed_stream<model_passthrough_packer>>(std::move(pathSpec), std::make_shared<file_stream>(path));
	} else {
		provider = std::make_shared<passthrough_packed_stream<standard_passthrough_packer>>(std::move(pathSpec), std::make_shared<file_stream>(path));
	}

	return add(provider, overwriteExisting);
}

void xivres::sqpack::generator::reserve_space(path_spec pathSpec, uint32_t size) {
	if (const auto it = m_hashOnlyEntries.find(pathSpec); it != m_hashOnlyEntries.end()) {
		it->second.reserve_entry_size(size);
		if (!it->second.path_spec().has_original() && pathSpec.has_original()) {
			it->second.update_path_spec(pathSpec);
			auto node = m_hashOnlyEntries.extract(it);
			node.key() = pathSpec;
			m_fullEntries.insert(std::move(node));
		}
	} else if (const auto itFull = m_fullEntries.find(pathSpec); itFull != m_fullEntries.end()) {
		itFull->second.reserve_entry_size(size);
	} else {
		const auto key = pathSpec;
		auto baseStream = std::make_shared<placeholder_packed_stream>(key);
		if (key.has_original())
			m_fullEntries.try_emplace(key, std::move(pathSpec), std::move(baseStream)).first->second.reserve_entry_size(size);
		else
			m_hashOnlyEntries.try_emplace(key, std::move(pathSpec), std::move(baseStream)).first->second.reserve_entry_size(size);
	}
}

namespace {
	template<xivres::sqpack::sqindex::sqindex_type TSqIndex, typename TFileSegmentType, typename TTextSegmentType, bool UseFolders>
	std::vector<uint8_t> export_index_file_data(
		size_t dataFilesCount,
		std::vector<TFileSegmentType> fileSegment,
		const std::vector<TTextSegmentType>& conflictSegment,
		const std::vector<xivres::sqpack::sqindex::segment_3_entry>& segment3,
		std::vector<xivres::sqpack::sqindex::path_hash_locator> folderSegment = {},
		bool strict = false
	) {
		using namespace xivres;

		std::vector<uint8_t> data;
		data.reserve(sizeof(sqpack::header)
			+ sizeof(sqpack::sqindex::header)
			+ std::span(fileSegment).size_bytes()
			+ std::span(conflictSegment).size_bytes()
			+ std::span(segment3).size_bytes()
			+ std::span(folderSegment).size_bytes());

		data.resize(sizeof(sqpack::header) + sizeof(sqpack::sqindex::header));
		auto& header1 = *reinterpret_cast<sqpack::header*>(data.data());
		std::memcpy(header1.Signature, sqpack::header::Signature_Value, sizeof(sqpack::header::Signature_Value));
		header1.HeaderSize = sizeof(sqpack::header);
		header1.Unknown1 = sqpack::header::Unknown1_Value;
		header1.Type = sqpack::file_type::SqIndex;
		header1.Unknown2 = sqpack::header::Unknown2_Value;
		if (strict)
			header1.Sha1.set_from_span(reinterpret_cast<char*>(&header1), offsetof(sqpack::header, Sha1));

		auto& header2 = *reinterpret_cast<sqpack::sqindex::header*>(&data[sizeof(sqpack::header)]);
		std::sort(fileSegment.begin(), fileSegment.end());
		header2.HeaderSize = sizeof(sqpack::sqindex::header);
		header2.Type = TSqIndex;
		header2.HashLocatorSegment.Count = 1;
		header2.HashLocatorSegment.Offset = header1.HeaderSize + header2.HeaderSize;
		header2.HashLocatorSegment.Size = static_cast<uint32_t>(std::span(fileSegment).size_bytes());
		header2.TextLocatorSegment.Count = static_cast<uint32_t>(dataFilesCount);
		header2.TextLocatorSegment.Offset = header2.HashLocatorSegment.Offset + header2.HashLocatorSegment.Size;
		header2.TextLocatorSegment.Size = static_cast<uint32_t>(std::span(conflictSegment).size_bytes());
		header2.UnknownSegment3.Count = 0;
		header2.UnknownSegment3.Offset = header2.TextLocatorSegment.Offset + header2.TextLocatorSegment.Size;
		header2.UnknownSegment3.Size = static_cast<uint32_t>(std::span(segment3).size_bytes());
		header2.PathHashLocatorSegment.Count = 0;
		header2.PathHashLocatorSegment.Offset = header2.UnknownSegment3.Offset + header2.UnknownSegment3.Size;
		if constexpr (UseFolders) {
			for (size_t i = 0; i < fileSegment.size(); ++i) {
				const auto& entry = fileSegment[i];
				if (folderSegment.empty() || folderSegment.back().PathHash != entry.PathHash) {
					folderSegment.emplace_back(
						entry.PathHash,
						static_cast<uint32_t>(header2.HashLocatorSegment.Offset + i * sizeof entry),
						static_cast<uint32_t>(sizeof entry),
						0);
				} else {
					folderSegment.back().PairHashLocatorSize = folderSegment.back().PairHashLocatorSize + sizeof entry;
				}
			}
			header2.PathHashLocatorSegment.Size = static_cast<uint32_t>(std::span(folderSegment).size_bytes());
		}

		if (strict) {
			header2.Sha1.set_from_span(reinterpret_cast<char*>(&header2), offsetof(sqpack::sqindex::header, Sha1));
			if (!fileSegment.empty())
				header2.HashLocatorSegment.Sha1.set_from_span(reinterpret_cast<const uint8_t*>(&fileSegment.front()), header2.HashLocatorSegment.Size);
			if (!conflictSegment.empty())
				header2.TextLocatorSegment.Sha1.set_from_span(reinterpret_cast<const uint8_t*>(&conflictSegment.front()), header2.TextLocatorSegment.Size);
			if (!segment3.empty())
				header2.UnknownSegment3.Sha1.set_from_span(reinterpret_cast<const uint8_t*>(&segment3.front()), header2.UnknownSegment3.Size);
			if constexpr (UseFolders) {
				if (!folderSegment.empty())
					header2.PathHashLocatorSegment.Sha1.set_from_span(reinterpret_cast<const uint8_t*>(&folderSegment.front()), header2.PathHashLocatorSegment.Size);
			}

		}
		if (!fileSegment.empty())
			data.insert(data.end(), reinterpret_cast<const uint8_t*>(&fileSegment.front()), reinterpret_cast<const uint8_t*>(&fileSegment.back() + 1));
		if (!conflictSegment.empty())
			data.insert(data.end(), reinterpret_cast<const uint8_t*>(&conflictSegment.front()), reinterpret_cast<const uint8_t*>(&conflictSegment.back() + 1));
		if (!segment3.empty())
			data.insert(data.end(), reinterpret_cast<const uint8_t*>(&segment3.front()), reinterpret_cast<const uint8_t*>(&segment3.back() + 1));

		if constexpr (UseFolders) {
			if (!folderSegment.empty())
				data.insert(data.end(), reinterpret_cast<const uint8_t*>(&folderSegment.front()), reinterpret_cast<const uint8_t*>(&folderSegment.back() + 1));
		}

		return data;
	}

	std::vector<uint8_t> ConcatDataHeaders(const xivres::sqpack::header& header, const xivres::sqdata::header& subheader) {
		std::vector<uint8_t> buffer;
		buffer.reserve(sizeof header + sizeof subheader);
		buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&header), reinterpret_cast<const uint8_t*>(&header + 1));
		buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&subheader), reinterpret_cast<const uint8_t*>(&subheader + 1));
		return buffer;
	}

	void ReadOriginalOrZeroes(const xivres::stream* original, uint64_t originalSize, uint64_t offset, std::span<uint8_t> out) {
		if (original && offset < originalSize) {
			const auto available = static_cast<size_t>((std::min<uint64_t>)(out.size(), originalSize - offset));
			original->read_fully(static_cast<std::streamoff>(offset), out.data(), static_cast<std::streamsize>(available));
			out = out.subspan(available);
		}
		std::ranges::fill(out, 0);
	}
}

xivres::sqpack::generator::data_view_stream::data_view_stream(const header& header, const sqdata::header& subheader, std::span<entry_info*> entries, std::shared_ptr<const stream> original, std::shared_ptr<sqpack_view_entry_cache> buffer, bool streamed)
	: m_header(ConcatDataHeaders(header, subheader))
	, m_entries(entries)
	, m_original(std::move(original))
	, m_originalSize(m_original ? static_cast<uint64_t>(m_original->size()) : 0)
	, m_size(sizeof header + sizeof subheader + subheader.DataSize)
	, m_buffer(std::move(buffer))
	, m_streamed(streamed) {
}

std::span<xivres::sqpack::generator::entry_info*>::iterator xivres::sqpack::generator::data_view_stream::entry_at_or_before(uint64_t offset) const {
	const auto next = std::ranges::upper_bound(m_entries, offset, {}, [](const entry_info* e) { return e->locator().offset(); });
	return next == m_entries.begin() ? m_entries.end() : std::prev(next);
}

std::streamsize xivres::sqpack::generator::data_view_stream::read(std::streamoff offset, void* buf, std::streamsize length) const {
	if (offset < 0 || length <= 0 || std::cmp_greater_equal(offset, m_size))
		return 0;

	auto pos = static_cast<uint64_t>(offset);
	auto out = std::span(static_cast<uint8_t*>(buf), static_cast<size_t>((std::min<uint64_t>)(length, m_size - pos)));
	const auto total = out.size();

	if (pos < m_header.size()) {
		const auto available = (std::min)(out.size(), static_cast<size_t>(m_header.size() - pos));
		std::copy_n(&m_header[static_cast<size_t>(pos)], available, out.begin());
		out = out.subspan(available);
		pos += available;
	}

	while (!out.empty()) {
		const auto cur = entry_at_or_before(pos);
		if (cur != m_entries.end() && pos < (*cur)->locator().offset() + (*cur)->entry_size()) {
			const auto& entry = **cur;
			const auto relativeOffset = pos - entry.locator().offset();
			const auto available = (std::min)(out.size(), static_cast<size_t>(entry.entry_size() - relativeOffset));
			if (entry.keeps_original_place() && !entry.swapped())
				ReadOriginalOrZeroes(m_original.get(), m_originalSize, pos, out.subspan(0, available));
			else if (!m_buffer || !m_buffer->read(entry, m_streamed, relativeOffset, out.subspan(0, available)))
				entry.read_fully(static_cast<std::streamoff>(relativeOffset), out.data(), static_cast<std::streamsize>(available));
			out = out.subspan(available);
			pos += available;

		} else {
			// between entries, which only happens where the layout of the original is kept.
			const auto next = cur == m_entries.end() ? m_entries.begin() : std::next(cur);
			const auto gapEnd = next == m_entries.end() ? m_size : (*next)->locator().offset();
			const auto available = (std::min)(out.size(), static_cast<size_t>(gapEnd - pos));
			ReadOriginalOrZeroes(m_original.get(), m_originalSize, pos, out.subspan(0, available));
			out = out.subspan(available);
			pos += available;
		}
	}

	return static_cast<std::streamsize>(total);
}

const xivres::sqpack::generator::entry_info* xivres::sqpack::generator::data_view_stream::entry_at(uint64_t offset) const {
	const auto it = entry_at_or_before(offset);
	if (it == m_entries.end() || offset >= (*it)->locator().offset() + (*it)->entry_size())
		return nullptr;
	return *it;
}

std::span<const xivres::sqpack::generator::entry_info* const> xivres::sqpack::generator::data_view_stream::entries_in(uint64_t offset, uint64_t length) const {
	auto first = entry_at_or_before(offset);
	if (first == m_entries.end())
		first = m_entries.begin();
	else if ((*first)->locator().offset() + (*first)->entry_size() <= offset)
		++first;

	const auto last = std::ranges::lower_bound(first, m_entries.end(), offset + length, {}, [](const entry_info* e) { return e->locator().offset(); });
	return {std::to_address(first), static_cast<size_t>(last - first)};
}

bool xivres::sqpack::generator::data_view_stream::reads_original(uint64_t offset, uint64_t length) const {
	if (!m_original || offset < m_header.size() || length > m_originalSize || offset > m_originalSize - length)
		return false;

	const auto end = offset + length;
	auto it = entry_at_or_before(offset);
	if (it == m_entries.end())
		it = m_entries.begin();
	for (; it != m_entries.end() && (*it)->locator().offset() < end; ++it) {
		const auto& entry = **it;
		if (entry.locator().offset() + entry.entry_size() <= offset)
			continue;
		if (!entry.keeps_original_place() || entry.swapped())
			return false;
	}
	return true;
}

xivres::sqpack::generator::sqpack_views xivres::sqpack::generator::export_to_views(bool strict, const std::shared_ptr<sqpack_view_entry_cache>& dataBuffer, bool streamed) {
	header dataHeader{};
	std::vector<sqdata::header> dataSubheaders;
	std::vector<std::pair<size_t, size_t>> dataEntryRanges;

	auto res = sqpack_views{
		.Index1 = {},
		.Index2 = {},
		.Data = {},
		.Entries = {},
		.HashOnlyEntries = std::move(m_hashOnlyEntries),
		.FullPathEntries = std::move(m_fullEntries),
	};

	res.Entries.reserve(res.HashOnlyEntries.size() + res.FullPathEntries.size());
	for (auto& entry : res.HashOnlyEntries | std::views::values)
		res.Entries.emplace_back(&entry);
	for (auto& entry : res.FullPathEntries | std::views::values)
		res.Entries.emplace_back(&entry);

	std::map<std::pair<uint32_t, uint32_t>, std::vector<entry_info*>> pairHashes;
	std::map<uint32_t, std::vector<entry_info*>> fullHashes;
	for (const auto& entry : res.Entries) {
		const auto& pathSpec = entry->path_spec();
		pairHashes[std::make_pair(pathSpec.path_hash(), pathSpec.name_hash())].emplace_back(entry);
		fullHashes[pathSpec.full_path_hash()].emplace_back(entry);
	}

	constexpr uint64_t dataHeaderSize = sizeof header + sizeof(sqdata::header);
	const auto keepLayout = !strict && !m_originalData.empty();

	std::vector<std::vector<entry_info*>> datEntries;
	std::vector<uint64_t> datEnds;
	if (keepLayout) {
		for (const auto& data : m_originalData) {
			datEntries.emplace_back();
			datEnds.emplace_back(align<uint64_t>((std::max<uint64_t>)(dataHeaderSize, data->size())).Alloc);
		}
	} else {
		datEntries.emplace_back();
		datEnds.emplace_back(dataHeaderSize);
	}

	size_t datIndex = 0;
	for (size_t i = 0; i < res.Entries.size(); ++i) {
		ProgressCallback(i, res.Entries.size());
		auto& entry = *res.Entries[i];

		if (keepLayout && entry.try_keep_original_place()) {
			datEntries[entry.locator().DatFileIndex].emplace_back(&entry);
			continue;
		}

		entry.finalize_entry_size();
		while (datEnds[datIndex] > dataHeaderSize && datEnds[datIndex] + entry.entry_size() > m_maxFileSize) {
			if (++datIndex < datEnds.size())
				continue;
			if (datIndex >= 8)
				throw std::runtime_error("Entries do not fit in 8 .dat files.");
			datEntries.emplace_back();
			datEnds.emplace_back(dataHeaderSize);
		}

		entry.locator({static_cast<uint32_t>(datIndex), datEnds[datIndex]});
		datEntries[datIndex].emplace_back(&entry);
		datEnds[datIndex] += entry.entry_size();
	}

	res.Entries.clear();
	for (size_t i = 0; i < datEntries.size(); ++i) {
		auto& entries = datEntries[i];
		std::ranges::sort(entries, [](const entry_info* l, const entry_info* r) {
			if (l->locator().offset() != r->locator().offset())
				return l->locator().offset() < r->locator().offset();
			return l->entry_size() < r->entry_size();
		});
		dataEntryRanges.emplace_back(res.Entries.size(), entries.size());
		res.Entries.insert(res.Entries.end(), entries.begin(), entries.end());

		dataSubheaders.emplace_back(sqdata::header{
			.HeaderSize = sizeof(sqdata::header),
			.Null1 = 0,
			.Unknown1 = sqdata::header::Unknown1_Value,
			.DataSize = {},
			.SpanIndex = static_cast<uint32_t>(i),
			.Null2 = 0,
			.MaxFileSize = m_maxFileSize,
			.DataSha1 = {},
			.Sha1 = {},
		});
		dataSubheaders.back().DataSize = datEnds[i] - dataHeaderSize;

		if (strict) {
			util::hash_sha1 sha1;
			for (const auto& provider : entries) {
				const auto length = provider->size();
				uint8_t buf[4096];
				for (std::streamoff j = 0; j < length; j += sizeof buf) {
					const auto readlen = static_cast<size_t>((std::min<uint64_t>)(sizeof buf, length - j));
					provider->read_fully(j, buf, static_cast<std::streamsize>(readlen));
					sha1.process_bytes(buf, readlen);
				}
			}
			sha1.get_digest_bytes(dataSubheaders.back().DataSha1.Value);
			dataSubheaders.back().Sha1.set_from_span(reinterpret_cast<char*>(&dataSubheaders.back()), offsetof(sqdata::header, Sha1));
		}
	}

	std::vector<sqindex::pair_hash_locator> fileEntries1;
	std::vector<sqindex::pair_hash_with_text_locator> conflictEntries1;
	for (const auto& [pairHash, correspondingEntries] : pairHashes) {
		if (correspondingEntries.size() == 1) {
			fileEntries1.emplace_back(sqindex::pair_hash_locator{.NameHash = pairHash.second, .PathHash = pairHash.first, .Locator = correspondingEntries.front()->locator(), .Padding = 0});
		} else {
			fileEntries1.emplace_back(sqindex::pair_hash_locator{.NameHash = pairHash.second, .PathHash = pairHash.first, .Locator = sqindex::data_locator::Synonym(), .Padding = 0});
			uint32_t i = 0;
			for (const auto& entry : correspondingEntries) {
				conflictEntries1.emplace_back(sqindex::pair_hash_with_text_locator{
					.NameHash = pairHash.second,
					.PathHash = pairHash.first,
					.Locator = entry->locator(),
					.ConflictIndex = i++,
					.FullPath = {},
				});
				const auto& path = entry->path_spec().text();
				strncpy_s(conflictEntries1.back().FullPath, path.c_str(), path.size());
			}
		}
	}
	conflictEntries1.emplace_back(sqindex::pair_hash_with_text_locator{
		.NameHash = sqindex::pair_hash_with_text_locator::EndOfList,
		.PathHash = sqindex::pair_hash_with_text_locator::EndOfList,
		.Locator = 0,
		.ConflictIndex = sqindex::pair_hash_with_text_locator::EndOfList,
		.FullPath = {},
	});

	std::vector<sqindex::full_hash_locator> fileEntries2;
	std::vector<sqindex::full_hash_with_text_locator> conflictEntries2;
	for (const auto& [fullHash, correspondingEntries] : fullHashes) {
		if (correspondingEntries.size() == 1) {
			fileEntries2.emplace_back(sqindex::full_hash_locator{.FullPathHash = fullHash, .Locator = correspondingEntries.front()->locator()});
		} else {
			fileEntries2.emplace_back(sqindex::full_hash_locator{.FullPathHash = fullHash, .Locator = sqindex::data_locator::Synonym()});
			uint32_t i = 0;
			for (const auto& entry : correspondingEntries) {
				conflictEntries2.emplace_back(sqindex::full_hash_with_text_locator{
					.FullPathHash = fullHash,
					.UnusedHash = 0,
					.Locator = entry->locator(),
					.ConflictIndex = i++,
					.FullPath = {},
				});
				const auto& path = entry->path_spec().text();
				strncpy_s(conflictEntries2.back().FullPath, path.c_str(), path.size());
			}
		}
	}
	conflictEntries2.emplace_back(sqindex::full_hash_with_text_locator{
		.FullPathHash = sqindex::full_hash_with_text_locator::EndOfList,
		.UnusedHash = sqindex::full_hash_with_text_locator::EndOfList,
		.Locator = 0,
		.ConflictIndex = sqindex::full_hash_with_text_locator::EndOfList,
		.FullPath = {},
	});

	std::memcpy(dataHeader.Signature, header::Signature_Value, sizeof(header::Signature_Value));
	dataHeader.HeaderSize = sizeof header;
	dataHeader.Unknown1 = header::Unknown1_Value;
	dataHeader.Type = file_type::SqData;
	dataHeader.Unknown2 = header::Unknown2_Value;
	if (strict)
		dataHeader.Sha1.set_from_span(reinterpret_cast<char*>(&dataHeader), offsetof(sqpack::header, Sha1));

	res.Index1 = std::make_shared<memory_stream>(export_index_file_data<sqindex::sqindex_type::Index, sqindex::pair_hash_locator, sqindex::pair_hash_with_text_locator, true>(
		dataSubheaders.size(), std::move(fileEntries1), conflictEntries1, m_sqpackIndexSegment3, std::vector<sqindex::path_hash_locator>(), strict));
	res.Index2 = std::make_shared<memory_stream>(export_index_file_data<sqindex::sqindex_type::Index, sqindex::full_hash_locator, sqindex::full_hash_with_text_locator, false>(
		dataSubheaders.size(), std::move(fileEntries2), conflictEntries2, m_sqpackIndex2Segment3, std::vector<sqindex::path_hash_locator>(), strict));
	for (size_t i = 0; i < dataSubheaders.size(); ++i)
		res.Data.emplace_back(std::make_shared<data_view_stream>(
			dataHeader,
			dataSubheaders[i],
			std::span(res.Entries).subspan(dataEntryRanges[i].first, dataEntryRanges[i].second),
			keepLayout && i < m_originalData.size() ? m_originalData[i] : nullptr,
			dataBuffer,
			streamed));

	return res;
}

void xivres::sqpack::generator::export_to_files(const std::filesystem::path& dir, bool strict, size_t /*cores*/) {
	header dataHeader{};
	std::memcpy(dataHeader.Signature, header::Signature_Value, sizeof(header::Signature_Value));
	dataHeader.HeaderSize = sizeof header;
	dataHeader.Unknown1 = header::Unknown1_Value;
	dataHeader.Type = file_type::SqData;
	dataHeader.Unknown2 = header::Unknown2_Value;
	if (strict)
		dataHeader.Sha1.set_from_span(reinterpret_cast<char*>(&dataHeader), offsetof(sqpack::header, Sha1));

	std::vector<sqdata::header> dataSubheaders;

	auto fullEntries = std::move(m_fullEntries);
	auto hashOnlyEntries = std::move(m_hashOnlyEntries);
	m_fullEntries.clear();
	m_hashOnlyEntries.clear();

	std::vector<entry_info*> entries;
	entries.reserve(fullEntries.size() + hashOnlyEntries.size());
	for (auto& val : fullEntries | std::views::values)
		entries.emplace_back(&val);
	for (auto& val : hashOnlyEntries | std::views::values)
		entries.emplace_back(&val);

	std::map<std::pair<uint32_t, uint32_t>, std::vector<entry_info*>> pairHashes;
	std::map<uint32_t, std::vector<entry_info*>> fullHashes;
	for (const auto entry : entries) {
		const auto& pathSpec = entry->path_spec();
		pairHashes[std::make_pair(pathSpec.path_hash(), pathSpec.name_hash())].emplace_back(entry);
		fullHashes[pathSpec.full_path_hash()].emplace_back(entry);
	}

	std::vector<sqindex::data_locator> locators;

	{
		util::thread_pool::task_waiter<std::pair<size_t, std::vector<char>>> waiter;
		std::fstream dataFile;

		for (size_t i = 0;;) {
			for (; i < entries.size() && waiter.pending() < (std::max<size_t>)(8, 2 * waiter.pool().concurrency()); ++i) {
				waiter.submit([i, entry = entries[i]](const util::thread_pool::base_task& task) {
					task.throw_if_cancelled();
					return std::make_pair(i, entry->base_stream()->read_vector<char>());
				});
				ProgressCallback(i, entries.size());
			}

			const auto resultPair = waiter.get();
			if (!resultPair)
				break;

			auto& entry = *entries[resultPair->first];
			auto& data = resultPair->second;
			const auto entrySize = static_cast<uint32_t>(entry.base_stream()->size());
			entry.reset_base_stream(nullptr);

			if (dataSubheaders.empty() ||
				sizeof header + sizeof(sqdata::header) + dataSubheaders.back().DataSize + entrySize > dataSubheaders.back().MaxFileSize) {
				if (!dataSubheaders.empty() && dataFile.is_open()) {
					if (strict) {
						std::vector<char> buf(65536);
						util::hash_sha1 sha1;
						dataFile.seekg(sizeof header + sizeof(sqdata::header), std::ios::beg);
						align<uint64_t>(dataSubheaders.back().DataSize, buf.size()).iterate_chunks([&](uint64_t /*index*/, uint64_t /*offset*/, uint64_t size) {
							dataFile.read(buf.data(), static_cast<std::streamsize>(size));
							if (!dataFile)
								throw std::runtime_error("Failed to read from output data file.");
							sha1.process_bytes(buf.data(), static_cast<size_t>(size));
						}, sizeof header + sizeof(sqdata::header));

						sha1.get_digest_bytes(dataSubheaders.back().DataSha1.Value);
						dataSubheaders.back().Sha1.set_from_span(reinterpret_cast<char*>(&dataSubheaders.back()), offsetof(sqdata::header, Sha1));
					}

					dataFile.seekp(0, std::ios::beg);
					dataFile.write(reinterpret_cast<const char*>(&dataHeader), sizeof dataHeader);
					dataFile.write(reinterpret_cast<const char*>(&dataSubheaders.back()), sizeof dataSubheaders.back());
					dataFile.close();
				}

				dataFile.open(dir / std::format("{}.win32.dat{}", DatName, dataSubheaders.size()), std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
				dataSubheaders.emplace_back(sqdata::header{
					.HeaderSize = sizeof(sqdata::header),
					.Null1 = 0,
					.Unknown1 = sqdata::header::Unknown1_Value,
					.DataSize = {},
					.SpanIndex = static_cast<uint32_t>(dataSubheaders.size()),
					.Null2 = 0,
					.MaxFileSize = m_maxFileSize,
					.DataSha1 = {},
					.Sha1 = {},
				});
			}

			entry.locator({static_cast<uint32_t>(dataSubheaders.size() - 1), sizeof header + sizeof(sqdata::header) + dataSubheaders.back().DataSize});
			dataFile.seekg(static_cast<std::streamoff>(entry.locator().offset()), std::ios::beg);
			dataFile.write(data.data(), static_cast<std::streamsize>(data.size()));
			if (!dataFile)
				throw std::runtime_error("Failed to write to output data file.");

			dataSubheaders.back().DataSize = dataSubheaders.back().DataSize + entrySize;
		}

		if (!dataSubheaders.empty() && dataFile.is_open()) {
			if (strict) {
				std::vector<char> buf(65536);
				util::hash_sha1 sha1;
				dataFile.seekg(sizeof header + sizeof(sqdata::header), std::ios::beg);
				align<uint64_t>(dataSubheaders.back().DataSize, buf.size()).iterate_chunks([&](uint64_t /*index*/, uint64_t /*offset*/, uint64_t size) {
					dataFile.read(buf.data(), static_cast<std::streamsize>(size));
					if (!dataFile)
						throw std::runtime_error("Failed to read from output data file.");
					sha1.process_bytes(buf.data(), static_cast<size_t>(size));
				}, sizeof header + sizeof(sqdata::header));

				sha1.get_digest_bytes(dataSubheaders.back().DataSha1.Value);
				dataSubheaders.back().Sha1.set_from_span(reinterpret_cast<char*>(&dataSubheaders.back()), offsetof(sqdata::header, Sha1));
			}

			dataFile.seekp(0, std::ios::beg);
			dataFile.write(reinterpret_cast<const char*>(&dataHeader), sizeof dataHeader);
			dataFile.write(reinterpret_cast<const char*>(&dataSubheaders.back()), sizeof dataSubheaders.back());
			dataFile.close();
		}
	}

	std::vector<sqindex::pair_hash_locator> fileEntries1;
	std::vector<sqindex::pair_hash_with_text_locator> conflictEntries1;
	for (const auto& [pairHash, correspondingEntries] : pairHashes) {
		if (correspondingEntries.size() == 1) {
			fileEntries1.emplace_back(sqindex::pair_hash_locator{.NameHash = pairHash.second, .PathHash = pairHash.first, .Locator = correspondingEntries.front()->locator(), .Padding = 0});
		} else {
			fileEntries1.emplace_back(sqindex::pair_hash_locator{.NameHash = pairHash.second, .PathHash = pairHash.first, .Locator = sqindex::data_locator::Synonym(), .Padding = 0});
			uint32_t i = 0;
			for (const auto& entry : correspondingEntries) {
				conflictEntries1.emplace_back(sqindex::pair_hash_with_text_locator{
					.NameHash = pairHash.second,
					.PathHash = pairHash.first,
					.Locator = entry->locator(),
					.ConflictIndex = i++,
					.FullPath = {},
				});
				const auto& path = entry->path_spec().text();
				strncpy_s(conflictEntries1.back().FullPath, path.c_str(), path.size());
			}
		}
	}
	conflictEntries1.emplace_back(sqindex::pair_hash_with_text_locator{
		.NameHash = sqindex::pair_hash_with_text_locator::EndOfList,
		.PathHash = sqindex::pair_hash_with_text_locator::EndOfList,
		.Locator = 0,
		.ConflictIndex = sqindex::pair_hash_with_text_locator::EndOfList,
		.FullPath = {},
	});

	std::vector<sqindex::full_hash_locator> fileEntries2;
	std::vector<sqindex::full_hash_with_text_locator> conflictEntries2;
	for (const auto& [fullHash, correspondingEntries] : fullHashes) {
		if (correspondingEntries.size() == 1) {
			fileEntries2.emplace_back(sqindex::full_hash_locator{.FullPathHash = fullHash, .Locator = correspondingEntries.front()->locator()});
		} else {
			fileEntries2.emplace_back(sqindex::full_hash_locator{.FullPathHash = fullHash, .Locator = sqindex::data_locator::Synonym()});
			uint32_t i = 0;
			for (const auto& entry : correspondingEntries) {
				conflictEntries2.emplace_back(sqindex::full_hash_with_text_locator{
					.FullPathHash = fullHash,
					.UnusedHash = 0,
					.Locator = entry->locator(),
					.ConflictIndex = i++,
					.FullPath = {},
				});
				const auto& path = entry->path_spec().text();
				strncpy_s(conflictEntries2.back().FullPath, path.c_str(), path.size());
			}
		}
	}
	conflictEntries2.emplace_back(sqindex::full_hash_with_text_locator{
		.FullPathHash = sqindex::full_hash_with_text_locator::EndOfList,
		.UnusedHash = sqindex::full_hash_with_text_locator::EndOfList,
		.Locator = 0,
		.ConflictIndex = sqindex::full_hash_with_text_locator::EndOfList,
		.FullPath = {},
	});

	auto indexData = export_index_file_data<sqindex::sqindex_type::Index, sqindex::pair_hash_locator, sqindex::pair_hash_with_text_locator, true>(
		dataSubheaders.size(), std::move(fileEntries1), conflictEntries1, m_sqpackIndexSegment3, std::vector<sqindex::path_hash_locator>(), strict);
	std::ofstream(dir / std::format("{}.win32.index", DatName), std::ios::binary).write(reinterpret_cast<const char*>(indexData.data()), static_cast<std::streamsize>(indexData.size()));

	indexData = export_index_file_data<sqindex::sqindex_type::Index, sqindex::full_hash_locator, sqindex::full_hash_with_text_locator, false>(
		dataSubheaders.size(), std::move(fileEntries2), conflictEntries2, m_sqpackIndex2Segment3, std::vector<sqindex::path_hash_locator>(), strict);
	std::ofstream(dir / std::format("{}.win32.index2", DatName), std::ios::binary).write(reinterpret_cast<const char*>(indexData.data()), static_cast<std::streamsize>(indexData.size()));
}

std::unique_ptr<xivres::default_base_stream> xivres::sqpack::generator::get(const path_spec& pathSpec) const {
	if (const auto it = m_hashOnlyEntries.find(pathSpec); it != m_hashOnlyEntries.end())
		return std::make_unique<unpacked_stream>(it->second.base_stream());
	if (const auto it = m_fullEntries.find(pathSpec); it != m_fullEntries.end())
		return std::make_unique<unpacked_stream>(it->second.base_stream());
	throw std::out_of_range(std::format("path_spec({}) not found", pathSpec));
}

std::vector<xivres::path_spec> xivres::sqpack::generator::all_path_spec() const {
	std::vector<path_spec> res;
	res.reserve(m_hashOnlyEntries.size() + m_fullEntries.size());
	for (const auto& entry : m_hashOnlyEntries | std::views::keys)
		res.emplace_back(entry);
	for (const auto& entry : m_fullEntries | std::views::keys)
		res.emplace_back(entry);
	return res;
}
