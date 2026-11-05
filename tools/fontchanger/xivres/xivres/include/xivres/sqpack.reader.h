#ifndef XIVRES_SQPACKREADER_H_
#define XIVRES_SQPACKREADER_H_

#include "unpacked_stream.h"
#include "sqpack.h"

namespace xivres::sqpack {
	class reader {
	public:
		template<typename HashLocatorT, typename TextLocatorT> 
		struct sqindex_type {
			const std::vector<uint8_t> Data;

			sqindex_type(std::vector<uint8_t> data, bool strictVerify)
				: Data(std::move(data)) {

				if (strictVerify) {
					header().verify_or_throw(file_type::SqIndex);
					index_header().verify_or_throw(sqindex::sqindex_type::Index);
					if (index_header().HashLocatorSegment.Size % sizeof(HashLocatorT))
						throw bad_data_error("HashLocators has an invalid size alignment");
					if (index_header().TextLocatorSegment.Size % sizeof(TextLocatorT))
						throw bad_data_error("TextLocators has an invalid size alignment");
					if (index_header().UnknownSegment3.Size % sizeof(sqindex::segment_3_entry))
						throw bad_data_error("Segment3 has an invalid size alignment");
					index_header().HashLocatorSegment.Sha1.verify(hash_locators(), "HashLocatorSegment has invalid data SHA-1");
					index_header().TextLocatorSegment.Sha1.verify(text_locators(), "TextLocatorSegment has invalid data SHA-1");
					index_header().UnknownSegment3.Sha1.verify(segment_3(), "UnknownSegment3 has invalid data SHA-1");
				}
			}

			sqindex_type(const stream& strm, bool strictVerify)
				: sqindex_type(strm.read_vector<uint8_t>(), strictVerify) {
			}

			[[nodiscard]] const header& header() const {
				return *reinterpret_cast<const sqpack::header*>(Data.data());
			}

			[[nodiscard]] const sqindex::header& index_header() const {
				return *reinterpret_cast<const sqindex::header*>(&Data[header().HeaderSize]);
			}

			[[nodiscard]] std::span<const HashLocatorT> hash_locators() const {
				return util::span_cast<HashLocatorT>(Data, index_header().HashLocatorSegment.Offset, index_header().HashLocatorSegment.Size, 1);
			}

			[[nodiscard]] std::span<const TextLocatorT> text_locators() const {
				return util::span_cast<TextLocatorT>(Data, index_header().TextLocatorSegment.Offset, index_header().TextLocatorSegment.Size, 1);
			}

			[[nodiscard]] std::span<const sqindex::segment_3_entry> segment_3() const {
				return util::span_cast<sqindex::segment_3_entry>(Data, index_header().UnknownSegment3.Offset, index_header().UnknownSegment3.Size, 1);
			}

			const sqindex::data_locator* find_data_locator(const char* fullPath) const {
				const auto it = std::ranges::find_if(text_locators(), [fullPath](const auto& l) { return _strcmpi(l.FullPath, fullPath) == 0; });
				if (it == text_locators().end())
					return nullptr;
				return &it->Locator;
			}

			const sqindex::data_locator& data_locator(const char* fullPath) const {
				if (const auto res = find_data_locator(fullPath))
					return *res;
				throw std::out_of_range(std::format("Entry {} not found", fullPath));
			}
		};

		struct sqindex_1_type : sqindex_type<sqindex::pair_hash_locator, sqindex::pair_hash_with_text_locator> {
			sqindex_1_type(std::vector<uint8_t> data, bool strictVerify);
			sqindex_1_type(const stream& strm, bool strictVerify);

			[[nodiscard]] std::span<const sqindex::path_hash_locator> pair_hash_locators() const;

			[[nodiscard]] std::span<const sqindex::pair_hash_locator> find_pair_hash_locators_for_path(uint32_t pathHash) const;

			[[nodiscard]] std::span<const sqindex::pair_hash_locator> pair_hash_locators_for_path(uint32_t pathHash) const;

			using sqindex_type::find_data_locator;
			using sqindex_type::data_locator;
			
			[[nodiscard]] const sqindex::data_locator* find_data_locator(uint32_t pathHash, uint32_t nameHash) const;
			[[nodiscard]] const sqindex::data_locator& data_locator(uint32_t pathHash, uint32_t nameHash) const;
		};

		struct sqindex_2_type : sqindex_type<sqindex::full_hash_locator, sqindex::full_hash_with_text_locator> {
			using sqindex_type::sqindex_type;

			using sqindex_type::find_data_locator;
			using sqindex_type::data_locator;
			
			[[nodiscard]] const sqindex::data_locator* find_data_locator(uint32_t fullPathHash) const;
			[[nodiscard]] const sqindex::data_locator& data_locator(uint32_t fullPathHash) const;
		};

		struct sqdata_type {
			header Header{};
			sqdata::header DataHeader{};
			std::shared_ptr<stream> Stream;

			sqdata_type(std::shared_ptr<stream> strm, uint32_t datIndex, bool strictVerify);
		};

		struct entry_info {
			sqindex::data_locator Locator;
			path_spec PathSpec;
			uint64_t Allocation;
		};

		sqindex_1_type Index1;
		sqindex_2_type Index2;
		std::vector<sqdata_type> Data;
		std::vector<entry_info> Entries;

		size_t TotalDataSize{};

		uint8_t CategoryId;
		uint8_t ExpacId;
		uint8_t PartId;

		reader(const std::string& fileName, const stream& indexStream1, const stream& indexStream2, std::vector<std::shared_ptr<stream>> dataStreams, bool strictVerify = false);

		static reader from_path(const std::filesystem::path& indexFile, bool strictVerify = false);

		[[nodiscard]] uint32_t pack_id() const { return (CategoryId << 16) | (ExpacId << 8) | PartId; }

		[[nodiscard]] const sqindex::data_locator* find_data_locator_from_index1(const path_spec& pathSpec) const;

		[[nodiscard]] const sqindex::data_locator& data_locator_from_index1(const path_spec& pathSpec) const;

		[[nodiscard]] const sqindex::data_locator* find_data_locator_from_index2(const path_spec& pathSpec) const;

		[[nodiscard]] const sqindex::data_locator& data_locator_from_index2(const path_spec& pathSpec) const;

		[[nodiscard]] size_t find_entry_index(const path_spec& pathSpec) const;

		[[nodiscard]] size_t get_entry_index(const path_spec& pathSpec) const;

		[[nodiscard]] std::shared_ptr<packed_stream> packed_at(const entry_info& info) const;

		[[nodiscard]] std::shared_ptr<packed_stream> packed_at(const path_spec& pathSpec) const;

		[[nodiscard]] std::shared_ptr<unpacked_stream> at(const entry_info& info, std::span<uint8_t> obfuscatedHeaderRewrite = {}) const;

		[[nodiscard]] std::shared_ptr<unpacked_stream> at(const path_spec& pathSpec, std::span<uint8_t> obfuscatedHeaderRewrite = {}) const;
	};
}

#endif
