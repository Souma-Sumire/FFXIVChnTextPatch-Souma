#include "../include/xivres/sqpack.reader.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <utility>

#include <zlib.h>

namespace {
	// The length of the entry at offset in strm, from its own header, or limit if the header doesn't tell.
	uint64_t ReadEntryLength(const xivres::stream& strm, std::streamoff offset, uint64_t limit) {
		xivres::packed::file_header header;
		if (limit < sizeof header || strm.read(offset, &header, sizeof header) != sizeof header || *header.HeaderSize < sizeof header)
			return limit;
		return (std::min)(limit, header.occupied_size());
	}

	// For crc32_combine_op, by the length of what is appended.
	const std::array<uLong, 256>& CombineOps() {
		static const auto s_ops = [] {
			std::array<uLong, 256> ops{};
			for (size_t i = 0; i < ops.size(); i++)
				ops[i] = crc32_combine_gen(static_cast<z_off_t>(i));
			return ops;
		}();
		return s_ops;
	}

	// Entries sharing a location sort in any order in either index, so those of the second are put in the order of the first, by which
	// full path hash can be of which pair of hashes.
	template<typename TOffsets1, typename TOffsets2>
	void PairSharedLocations(const TOffsets1& offsets1, TOffsets2& offsets2) {
		const auto& ops = CombineOps();
		for (size_t begin = 0, end; begin < offsets1.size(); begin = end) {
			end = begin + 1;
			while (end < offsets1.size() && offsets1[end].first == offsets1[begin].first)
				++end;
			if (end - begin < 2 || end > offsets2.size() || offsets2[begin].first != offsets1[begin].first || offsets2[end - 1].first != offsets1[begin].first)
				continue;

			std::multimap<uint32_t, size_t> byFullPathHash;
			for (auto i = begin; i < end; ++i)
				byFullPathHash.emplace(std::get<0>(offsets2[i].second), i);

			std::vector<size_t> order(end - begin, SIZE_MAX);
			for (auto i = begin; i < end; ++i) {
				// The hash of the folder and a slash, to which a name of each length is appended in turn.
				const auto folder = crc32_combine_op(~std::get<0>(offsets1[i].second), ~xivres::path_spec::SlashHashValue, ops[1]);
				const auto name = ~std::get<1>(offsets1[i].second);
				auto found = byFullPathHash.end();
				for (size_t length = 0; length < ops.size() && found == byFullPathHash.end(); ++length)
					found = byFullPathHash.find(~static_cast<uint32_t>(crc32_combine_op(folder, name, ops[length])));
				if (found == byFullPathHash.end())
					continue;
				order[i - begin] = found->second;
				byFullPathHash.erase(found);
			}

			auto rest = byFullPathHash.begin();
			std::vector<typename TOffsets2::value_type> ordered;
			ordered.reserve(order.size());
			for (const auto index : order)
				ordered.emplace_back(offsets2[index != SIZE_MAX ? index : (rest++)->second]);
			std::ranges::copy(ordered, offsets2.begin() + static_cast<std::ptrdiff_t>(begin));
		}
	}

	// An entry in a .dat file. Its size is read from its header on first use, as the space up to the next entry of the index may hold
	// entries the index doesn't refer to.
	class entry_view_stream : public xivres::default_base_stream {
		const std::shared_ptr<const xivres::stream> m_stream;
		const std::streamoff m_offset;
		const std::streamsize m_limit;
		mutable std::atomic<std::streamsize> m_size = -1;

	public:
		entry_view_stream(std::shared_ptr<const xivres::stream> strm, std::streamoff offset, std::streamsize limit)
			: m_stream(std::move(strm))
			, m_offset(offset)
			, m_limit(limit) {
		}

		[[nodiscard]] std::streamsize size() const override {
			auto size = m_size.load(std::memory_order_relaxed);
			if (size < 0) {
				size = static_cast<std::streamsize>(ReadEntryLength(*m_stream, m_offset, static_cast<uint64_t>(m_limit)));
				m_size.store(size, std::memory_order_relaxed);
			}
			return size;
		}

		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override {
			const auto available = size();
			if (offset >= available)
				return 0;
			return m_stream->read(m_offset + offset, buf, (std::min)(length, available - offset));
		}

	protected:
		[[nodiscard]] xivres::stream_source source_impl() const override {
			auto result = m_stream->source();
			result.Offset += m_offset;
			return result;
		}
	};
}

std::span<const xivres::sqpack::sqindex::path_hash_locator> xivres::sqpack::reader::sqindex_1_type::pair_hash_locators() const {
	return util::span_cast<sqindex::path_hash_locator>(Data, index_header().PathHashLocatorSegment.Offset, index_header().PathHashLocatorSegment.Size, 1);
}

std::span<const xivres::sqpack::sqindex::pair_hash_locator> xivres::sqpack::reader::sqindex_1_type::find_pair_hash_locators_for_path(uint32_t pathHash) const {
	const auto it = std::lower_bound(pair_hash_locators().begin(), pair_hash_locators().end(), pathHash, path_spec::LocatorComparator());
	if (it == pair_hash_locators().end() || it->PathHash != pathHash)
		return {};

	return util::span_cast<sqindex::pair_hash_locator>(Data, it->PairHashLocatorOffset, it->PairHashLocatorSize, 1);
}

std::span<const xivres::sqpack::sqindex::pair_hash_locator> xivres::sqpack::reader::sqindex_1_type::pair_hash_locators_for_path(uint32_t pathHash) const {
	if (const auto res = find_pair_hash_locators_for_path(pathHash); !res.empty())
		return res;

	throw std::out_of_range(std::format("PathHash {:08x} not found", pathHash));
}

const xivres::sqpack::sqindex::data_locator* xivres::sqpack::reader::sqindex_1_type::find_data_locator(uint32_t pathHash, uint32_t nameHash) const {
	const auto locators = find_pair_hash_locators_for_path(pathHash);
	if (locators.empty())
		return nullptr;
	
	const auto it = std::ranges::lower_bound(locators, nameHash, {}, [](const sqindex::pair_hash_locator& l) -> uint32_t { return l.NameHash; });
	if (it == locators.end() || it->NameHash != nameHash)
		return nullptr;

	return &it->Locator;
}

const xivres::sqpack::sqindex::data_locator& xivres::sqpack::reader::sqindex_1_type::data_locator(uint32_t pathHash, uint32_t nameHash) const {
	if (const auto res = find_data_locator(pathHash, nameHash))
		return *res;
	
	throw std::out_of_range(std::format("NameHash {:08x} in PathHash {:08x} not found", nameHash, pathHash));
}

xivres::sqpack::reader::sqindex_1_type::sqindex_1_type(std::vector<uint8_t> data, bool strictVerify)
	: sqindex_type(std::move(data), strictVerify) {
	if (strictVerify) {
		if (index_header().PathHashLocatorSegment.Size % sizeof(sqindex::path_hash_locator))
			throw bad_data_error("PathHashLocators has an invalid size alignment");
		index_header().PathHashLocatorSegment.Sha1.verify(pair_hash_locators(), "PathHashLocatorSegment has invalid data SHA-1");
	}
}

xivres::sqpack::reader::sqindex_1_type::sqindex_1_type(const stream& strm, bool strictVerify)
	: sqindex_type(strm, strictVerify) {
	if (strictVerify) {
		if (index_header().PathHashLocatorSegment.Size % sizeof(sqindex::path_hash_locator))
			throw bad_data_error("PathHashLocators has an invalid size alignment");
		index_header().PathHashLocatorSegment.Sha1.verify(pair_hash_locators(), "PathHashLocatorSegment has invalid data SHA-1");
	}
}

const xivres::sqpack::sqindex::data_locator* xivres::sqpack::reader::sqindex_2_type::find_data_locator(uint32_t fullPathHash) const {
	const auto it = std::lower_bound(hash_locators().begin(), hash_locators().end(), fullPathHash, path_spec::LocatorComparator());
	if (it == hash_locators().end() || it->FullPathHash != fullPathHash)
		return nullptr;
	return &it->Locator;
}

const xivres::sqpack::sqindex::data_locator& xivres::sqpack::reader::sqindex_2_type::data_locator(uint32_t fullPathHash) const {
	if (const auto res = find_data_locator(fullPathHash))
		return *res;

	throw std::out_of_range(std::format("FullPathHash {:08x} not found", fullPathHash));
}

xivres::sqpack::reader::sqdata_type::sqdata_type(std::shared_ptr<stream> strm, const uint32_t datIndex, bool strictVerify)
	: Stream(std::move(strm)) {
	// The following line loads both Header and DataHeader as they are adjacent to each other
	Stream->read_fully(0, &Header, sizeof Header + sizeof DataHeader);
	if (strictVerify) {
		if (datIndex == 0) {
			Header.verify_or_throw(file_type::SqData);
			DataHeader.verify_or_throw(datIndex + 1);
		}
	}

	if (strictVerify) {
		const auto dataFileLength = Stream->size();
		if (datIndex == 0) {
			if (std::cmp_not_equal(dataFileLength, 0ULL + Header.HeaderSize + DataHeader.HeaderSize + DataHeader.DataSize))
				throw bad_data_error("Invalid file size");
		}
	}
}

xivres::sqpack::reader::reader(const std::string& fileName, const stream& indexStream1, const stream& indexStream2, std::vector<std::shared_ptr<stream>> dataStreams, bool strictVerify)
	: Index1(indexStream1, strictVerify)
	, Index2(indexStream2, strictVerify)
	, CategoryId(static_cast<uint8_t>(std::strtol(fileName.substr(0, 2).c_str(), nullptr, 16)))
	, ExpacId(static_cast<uint8_t>(std::strtol(fileName.substr(2, 2).c_str(), nullptr, 16)))
	, PartId(static_cast<uint8_t>(std::strtol(fileName.substr(4, 2).c_str(), nullptr, 16))) {
	std::vector<std::pair<sqindex::data_locator, std::tuple<uint32_t, uint32_t, const char*>>> offsets1;
	offsets1.reserve(
		(std::max)(Index1.hash_locators().size() + Index1.text_locators().size(), Index2.hash_locators().size() + Index2.text_locators().size())
		+ Index1.index_header().TextLocatorSegment.Count
	);
	for (const auto& item : Index1.hash_locators()) {
		if (!item.Locator.IsSynonym)
			offsets1.emplace_back(item.Locator, std::make_tuple(item.PathHash, item.NameHash, static_cast<const char*>(nullptr)));
	}
	for (const auto& item : Index1.text_locators()) {
		if (item.end_of_list())
			break;
		offsets1.emplace_back(item.Locator, std::make_tuple(item.PathHash, item.NameHash, item.FullPath));
	}

	std::vector<std::pair<sqindex::data_locator, std::tuple<uint32_t, const char*>>> offsets2;
	for (const auto& item : Index2.hash_locators()) {
		if (!item.Locator.IsSynonym)
			offsets2.emplace_back(item.Locator, std::make_tuple(item.FullPathHash, static_cast<const char*>(nullptr)));
	}
	for (const auto& item : Index2.text_locators()) {
		if (item.end_of_list())
			break;
		offsets2.emplace_back(item.Locator, std::make_tuple(item.FullPathHash, item.FullPath));
	}

	if (offsets1.size() != offsets2.size() && !offsets1.empty() && !offsets2.empty())
		throw bad_data_error(".index and .index2 do not have the same number of files contained");

	Data.reserve(dataStreams.size());
	for (uint32_t i = 0; i < dataStreams.size(); ++i) {
		Data.emplace_back(
			dataStreams[i],
			i,
			strictVerify
		);
		if (!offsets1.empty())
			offsets1.emplace_back(sqindex::data_locator(i, Data[i].Stream->size()), std::make_tuple(UINT32_MAX, UINT32_MAX, static_cast<const char*>(nullptr)));
		if (!offsets2.empty())
			offsets2.emplace_back(sqindex::data_locator(i, Data[i].Stream->size()), std::make_tuple(UINT32_MAX, static_cast<const char*>(nullptr))); 
		TotalDataSize += Data[i].Stream->size();
	}

	struct Comparator {
		bool operator()(const sqindex::data_locator& l, const sqindex::data_locator& r) const {
			if (l.DatFileIndex != r.DatFileIndex)
				return l.DatFileIndex < r.DatFileIndex;
			if (l.offset() != r.offset())
				return l.offset() < r.offset();
			return false;
		}

		bool operator()(const std::pair<sqindex::data_locator, std::tuple<uint32_t, uint32_t, const char*>>& l, const std::pair<sqindex::data_locator, std::tuple<uint32_t, uint32_t, const char*>>& r) const {
			return (*this)(l.first, r.first);
		}

		bool operator()(const std::pair<sqindex::data_locator, std::tuple<uint32_t, const char*>>& l, const std::pair<sqindex::data_locator, std::tuple<uint32_t, const char*>>& r) const {
			return (*this)(l.first, r.first);
		}

		bool operator()(const entry_info& l, const entry_info& r) const {
			return l.Locator < r.Locator;
		}
	};

	std::ranges::sort(offsets1, Comparator());
	std::ranges::sort(offsets2, Comparator());
	Entries.reserve(offsets1.size());
	if (offsets1.size() == offsets2.size())
		PairSharedLocations(offsets1, offsets2);

	// The space up to the next location in the same .dat file. Entries may share a location, and then share the space.
	const auto spacesOf = [](const auto& offsets) {
		std::vector<uint64_t> spaces(offsets.size());
		for (auto i = offsets.size(); i-- > 1;) {
			const auto& prev = offsets[i - 1].first;
			const auto& curr = offsets[i].first;
			if (prev.DatFileIndex != curr.DatFileIndex)
				continue;
			spaces[i - 1] = prev.offset() == curr.offset() ? spaces[i] : curr.offset() - prev.offset();
		}
		return spaces;
	};
	const auto spaces = offsets1.empty() ? spacesOf(offsets2) : spacesOf(offsets1);

	if (strictVerify && !offsets1.empty() && !offsets2.empty()) {
		for (size_t i = 0; i < offsets1.size(); ++i) {
			if (offsets1[i].first != offsets2[i].first)
				throw bad_data_error(".index and .index2 have items with different locators");
			if (offsets1[i].first.IsSynonym)
				throw bad_data_error("Synonym remains after conflict resolution");
		}
	}

	if (!offsets1.empty() && !offsets2.empty()) {
		for (size_t curr = 1, prev = 0; curr < offsets1.size(); ++curr, ++prev) {

			// Skip dummy items to mark end of individual .dat file.
			if (offsets1[prev].first.DatFileIndex != offsets1[curr].first.DatFileIndex)
				continue;

			Entries.emplace_back(entry_info{.Locator = offsets1[prev].first, .PathSpec = {}, .Allocation = spaces[prev]});
			if (std::get<2>(offsets1[prev].second))
				Entries.back().PathSpec = path_spec(std::get<2>(offsets1[prev].second));
			else if (std::get<1>(offsets2[prev].second))
				Entries.back().PathSpec = path_spec(std::get<1>(offsets2[prev].second));
			else
				Entries.back().PathSpec = path_spec(
					std::get<0>(offsets1[prev].second),
					std::get<1>(offsets1[prev].second),
					std::get<0>(offsets2[prev].second),
					CategoryId,
					ExpacId,
					PartId);
		}
	} else if (!offsets1.empty()) {
		for (size_t curr = 1, prev = 0; curr < offsets1.size(); ++curr, ++prev) {

			// Skip dummy items to mark end of individual .dat file.
			if (offsets1[prev].first.DatFileIndex != offsets1[curr].first.DatFileIndex)
				continue;

			Entries.emplace_back(entry_info{.Locator = offsets1[prev].first, .PathSpec = {}, .Allocation = spaces[prev]});
			if (std::get<2>(offsets1[prev].second))
				Entries.back().PathSpec = path_spec(std::get<2>(offsets1[prev].second));
			else
				Entries.back().PathSpec = path_spec(
					std::get<0>(offsets1[prev].second),
					std::get<1>(offsets1[prev].second),
					path_spec::EmptyHashValue,
					CategoryId,
					ExpacId,
					PartId);
		}
	} else if (!offsets2.empty()) {
		for (size_t curr = 1, prev = 0; curr < offsets2.size(); ++curr, ++prev) {

			// Skip dummy items to mark end of individual .dat file.
			if (offsets2[prev].first.DatFileIndex != offsets2[curr].first.DatFileIndex)
				continue;

			Entries.emplace_back(entry_info{.Locator = offsets2[prev].first, .PathSpec = {}, .Allocation = spaces[prev]});
			if (std::get<1>(offsets2[prev].second))
				Entries.back().PathSpec = path_spec(std::get<1>(offsets2[prev].second));
			else
				Entries.back().PathSpec = path_spec(
					path_spec::EmptyHashValue,
					path_spec::EmptyHashValue,
					std::get<0>(offsets2[prev].second),
					CategoryId,
					ExpacId,
					PartId);
		}
	}

	std::ranges::sort(Entries, Comparator());
}

xivres::sqpack::reader xivres::sqpack::reader::from_path(const std::filesystem::path& indexFile, bool strictVerify) {
	static constexpr char emptyIndex[] = "\x53\x71\x50\x61\x63\x6b\x00\x00\x00\x00\x00\x00\x00\x04\x00\x00\x01\x00\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xff\xff\xff\xff\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x14\x03\x16\xfb\x3d\x2f\x7a\x61\xd8\xd9\x51\x20\x12\xe4\x4a\xf6\xa1\xe1\x45\x2e\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x04\x00\x00\x01\x00\x00\x00\x00\x08\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x08\x00\x00\x00\x01\x00\x00\x5e\x9d\x28\xd0\x48\x5d\xa8\x38\xf6\x2d\x71\x3c\x3d\xb6\x96\x1a\x6e\x13\xd8\x3b\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x09\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x57\x7f\x4d\xc3\x47\x77\xce\x82\xb2\xe9\xfe\xd5\x36\xe9\xf8\xb1\x49\x2b\xd9\x30\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xff\xff\xff\xff\xff\xff\xff\xff\x00\x00\x00\x00\xff\xff\xff\xff\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00";

	std::vector<std::shared_ptr<stream>> dataStreams;
	for (int i = 0; i < 8; ++i) {
		auto dataPath = std::filesystem::path(indexFile);
		dataPath.replace_extension(std::format(".dat{}", i));
		if (!exists(dataPath))
			break;
		dataStreams.emplace_back(std::make_shared<file_stream>(dataPath));
	}

	const auto indexPath = std::filesystem::path(indexFile).replace_extension(".index");
	const auto index2Path = std::filesystem::path(indexFile).replace_extension(".index2");
	return {
		indexFile.filename().string(),
		exists(indexPath) ? file_stream(indexPath) : memory_stream(emptyIndex),
		exists(index2Path) ? file_stream(index2Path) : memory_stream(emptyIndex),
		std::move(dataStreams),
		strictVerify
	};
}

const xivres::sqpack::sqindex::data_locator* xivres::sqpack::reader::find_data_locator_from_index1(const path_spec& pathSpec) const {
	const auto locator = Index1.find_data_locator(pathSpec.path_hash(), pathSpec.name_hash());
	if (locator && locator->IsSynonym)
		return Index1.find_data_locator(pathSpec.text().c_str());
	return locator;
}

const xivres::sqpack::sqindex::data_locator& xivres::sqpack::reader::data_locator_from_index1(const path_spec& pathSpec) const {
	if (const auto res = find_data_locator_from_index1(pathSpec))
		return *res;
	throw std::out_of_range("File does not exist");
}

const xivres::sqpack::sqindex::data_locator* xivres::sqpack::reader::find_data_locator_from_index2(const path_spec& pathSpec) const {
	const auto locator = Index2.find_data_locator(pathSpec.full_path_hash());
	if (locator && locator->IsSynonym)
		return Index2.find_data_locator(pathSpec.text().c_str());
	return locator;
}

const xivres::sqpack::sqindex::data_locator& xivres::sqpack::reader::data_locator_from_index2(const path_spec& pathSpec) const {
	if (const auto res = find_data_locator_from_index2(pathSpec))
		return *res;
	throw std::out_of_range("File does not exist");
}

size_t xivres::sqpack::reader::find_entry_index(const path_spec& pathSpec) const {
	const auto locator = find_data_locator_from_index1(pathSpec);
	if (!locator)
		return (std::numeric_limits<size_t>::max)();

	auto entryInfo = std::ranges::lower_bound(Entries, *locator, std::less(), &entry_info::Locator);

	// Of the entries sharing the location, the one of this path.
	for (auto same = entryInfo; same != Entries.end() && same->Locator == *locator; ++same) {
		if (same->PathSpec == pathSpec) {
			entryInfo = same;
			break;
		}
	}
	return static_cast<size_t>(std::distance(Entries.begin(), entryInfo));
}

size_t xivres::sqpack::reader::get_entry_index(const path_spec& pathSpec) const {
	const auto res = find_entry_index(pathSpec);
	if (res == (std::numeric_limits<size_t>::max)())
		throw std::out_of_range("File does not exist");
	return res;
}

std::shared_ptr<xivres::packed_stream> xivres::sqpack::reader::packed_at(const entry_info& info) const {
	return packed_at(info, info.PathSpec);
}

std::shared_ptr<xivres::packed_stream> xivres::sqpack::reader::packed_at(const path_spec& pathSpec) const {
	// The path asked for may have its text, which the entry's doesn't.
	return packed_at(Entries[get_entry_index(pathSpec)], pathSpec);
}

std::shared_ptr<xivres::packed_stream> xivres::sqpack::reader::packed_at(const entry_info& info, const path_spec& pathSpec) const {
	return std::make_unique<stream_as_packed_stream>(pathSpec, std::make_shared<entry_view_stream>(Data.at(info.Locator.DatFileIndex).Stream, static_cast<std::streamoff>(info.Locator.offset()), static_cast<std::streamsize>(info.Allocation)));
}

std::shared_ptr<xivres::unpacked_stream> xivres::sqpack::reader::at(const entry_info& info, std::span<uint8_t> obfuscatedHeaderRewrite) const {
	return std::make_shared<unpacked_stream>(packed_at(info), obfuscatedHeaderRewrite);
}

std::shared_ptr<xivres::unpacked_stream> xivres::sqpack::reader::at(const path_spec& pathSpec, std::span<uint8_t> obfuscatedHeaderRewrite) const {
	return std::make_shared<unpacked_stream>(packed_at(pathSpec), obfuscatedHeaderRewrite);
}
