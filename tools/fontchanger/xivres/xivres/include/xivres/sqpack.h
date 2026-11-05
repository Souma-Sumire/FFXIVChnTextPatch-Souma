#ifndef XIVRES_SQPACK_H_
#define XIVRES_SQPACK_H_

#include <format>
#include <limits>
#include <zlib.h>

#include "common.h"
#include "util.byte_order.h"
#include "util.sha1.h"

namespace xivres::sqpack {
	enum class file_type : uint32_t {
		Unspecified = UINT32_MAX,
		SqDatabase = 0,
		SqData = 1,
		SqIndex = 2,
	};

	struct header {
		static constexpr uint32_t Unknown1_Value = 1;
		static constexpr uint32_t Unknown2_Value = 0xFFFFFFFFUL;
		static constexpr char Signature_Value[12] = {
			'S', 'q', 'P', 'a', 'c', 'k', 0, 0, 0, 0, 0, 0,
		};

		char Signature[12]{};
		LE<uint32_t> HeaderSize;
		LE<uint32_t> Unknown1; // 1
		LE<file_type> Type;
		LE<uint32_t> YYYYMMDD;
		LE<uint32_t> Time;
		LE<uint32_t> Unknown2; // Intl: 0xFFFFFFFF, KR/CN: 1
		char Padding_0x024[0x3c0 - 0x024]{};
		sha1_value Sha1;
		char Padding_0x3D4[0x2c]{};

		void verify_or_throw(file_type supposedType) const;
	};

	static_assert(offsetof(header, Sha1) == 0x3c0, "Bad sqpack::header definition");
	static_assert(sizeof(header) == 1024);
}

namespace xivres::sqpack::sqindex {
	struct segment_descriptor {
		LE<uint32_t> Count;
		LE<uint32_t> Offset;
		LE<uint32_t> Size;
		sha1_value Sha1;
		char Padding_0x020[0x28]{};
	};

	static_assert(sizeof segment_descriptor == 0x48);

	union data_locator {
		uint32_t Value;

		struct {
			uint32_t IsSynonym : 1;
			uint32_t DatFileIndex : 3;
			uint32_t DatFileUnitOffset : 28;
		};

		static data_locator Synonym() {
			return {1};
		}

		data_locator(uint32_t value = 0)
			: Value(value) {
		}

		data_locator(uint32_t index, uint64_t offset)
			: IsSynonym(0)
			, DatFileIndex(index)
			, DatFileUnitOffset(static_cast<uint32_t>(offset / EntryAlignment)) {
			if (offset % EntryAlignment)
				throw std::invalid_argument("Offset must be a multiple of 128.");
			if (offset / 8 > UINT32_MAX)
				throw std::invalid_argument("Offset is too big.");
		}
		
		data_locator(data_locator&&) = default;
		data_locator(const data_locator&) = default;
		data_locator& operator=(data_locator&&) = default;
		data_locator& operator=(const data_locator&) = default;
		~data_locator() = default;

		[[nodiscard]] uint64_t offset() const {
			return 1ULL * DatFileUnitOffset * EntryAlignment;
		}

		void offset(uint64_t value) {
			if (value % EntryAlignment)
				throw std::invalid_argument("Offset must be a multiple of 128.");
			if (value / 8 > UINT32_MAX)
				throw std::invalid_argument("Offset is too big.");
			DatFileUnitOffset = static_cast<uint32_t>(value / EntryAlignment);
		}

		bool operator<(const data_locator& r) const {
			return Value < r.Value;
		}

		bool operator>(const data_locator& r) const {
			return Value > r.Value;
		}

		bool operator==(const data_locator& r) const {
			if (IsSynonym || r.IsSynonym)
				return IsSynonym == r.IsSynonym;
			return Value == r.Value;
		}
	};

	struct pair_hash_locator {
		LE<uint32_t> NameHash;
		LE<uint32_t> PathHash;
		data_locator Locator;
		LE<uint32_t> Padding;

		bool operator<(const pair_hash_locator& r) const {
			if (PathHash == r.PathHash)
				return NameHash < r.NameHash;
			else
				return PathHash < r.PathHash;
		}
	};

	struct full_hash_locator {
		LE<uint32_t> FullPathHash;
		data_locator Locator;

		bool operator<(const full_hash_locator& r) const {
			return FullPathHash < r.FullPathHash;
		}
	};

	struct segment_3_entry {
		LE<uint32_t> Unknown1;
		LE<uint32_t> Unknown2;
		LE<uint32_t> Unknown3;
		LE<uint32_t> Unknown4;
	};

	struct path_hash_locator {
		LE<uint32_t> PathHash;
		LE<uint32_t> PairHashLocatorOffset;
		LE<uint32_t> PairHashLocatorSize;
		LE<uint32_t> Padding;

		void verify_or_throw() const;
	};

	struct pair_hash_with_text_locator {
		static constexpr uint32_t EndOfList = 0xFFFFFFFFU;

		// TODO: following two can actually be in reverse order; find it out when the game data file actually contains a conflict in .index file
		LE<uint32_t> NameHash;
		LE<uint32_t> PathHash;
		data_locator Locator;
		LE<uint32_t> ConflictIndex;
		char FullPath[0xF0];

		[[nodiscard]] bool end_of_list() const {
			return NameHash == EndOfList && PathHash == EndOfList && Locator.Value == 0 && ConflictIndex == EndOfList && FullPath[0] == 0;
		}
	};

	struct full_hash_with_text_locator {
		static constexpr uint32_t EndOfList = 0xFFFFFFFFU;

		LE<uint32_t> FullPathHash;
		LE<uint32_t> UnusedHash;
		data_locator Locator;
		LE<uint32_t> ConflictIndex;
		char FullPath[0xF0];

		[[nodiscard]] bool end_of_list() const {
			return FullPathHash == EndOfList && Locator.Value == 0 && ConflictIndex == EndOfList && FullPath[0] == 0;
		}
	};

	enum class sqindex_type : uint32_t {
		Unspecified = UINT32_MAX,
		Index = 0,
		Index2 = 2,
	};

	/*
	 * Segment 1
	 * * Stands for files
	 * * Descriptor.Count = 1
	 *
	 * Segment 2
	 * * Descriptor.Count stands for number of .dat files
	 * * Descriptor.Size is multiple of 0x100; each entry is sized 0x100
	 * * Data is always 8x00s, 4xFFs, and the rest is 0x00s
	 *
	 * Segment 3
	 * * Descriptor.Count = 0
	 *
	 * Segment 4
	 * * Stands for folders
	 * * Descriptor.Count = 0
	 */
	struct header {
		LE<uint32_t> HeaderSize;
		segment_descriptor HashLocatorSegment;
		char Padding_0x04C[4]{};
		segment_descriptor TextLocatorSegment;
		segment_descriptor UnknownSegment3;
		segment_descriptor PathHashLocatorSegment;
		char Padding_0x128[4]{};
		LE<sqindex_type> Type;
		char Padding_0x130[0x3c0 - 0x130]{};
		sha1_value Sha1;
		char Padding_0x3D4[0x2c]{};

		void verify_or_throw(sqindex_type expectedIndexType) const;
	};

	static_assert(sizeof(header) == 1024);
}

namespace xivres::sqdata {
	struct header {
		static constexpr uint64_t MaxFileSize_Value = 0x77359400ULL; // 2GB
		static constexpr uint64_t MaxFileSize_MaxValue = 0x800000000ULL; // 32GiB, maximum addressable via how LEDataLocator works
		static constexpr uint32_t Unknown1_Value = 0x10;

		LE<uint32_t> HeaderSize;
		LE<uint32_t> Null1;
		LE<uint32_t> Unknown1;

		union DataSizeDivBy8Type {
			LE<uint32_t> RawValue;

			DataSizeDivBy8Type& operator=(uint64_t value) {
				if (value % EntryAlignment)
					throw std::invalid_argument("Value must be a multiple of 8.");
				if (value / EntryAlignment > UINT32_MAX)
					throw std::invalid_argument("Value too big.");
				RawValue = static_cast<uint32_t>(value / EntryAlignment);
				return *this;
			}

			operator uint64_t() const {
				return value();
			}

			[[nodiscard]] uint64_t value() const {
				return 1ULL * RawValue * EntryAlignment;
			}
		} DataSize; // From end of this header to EOF
		LE<uint32_t> SpanIndex; // 0x01 = .dat0, 0x02 = .dat1, 0x03 = .dat2, ...
		LE<uint32_t> Null2;
		LE<uint64_t> MaxFileSize;
		sha1_value DataSha1; // From end of this header to EOF
		char Padding_0x034[0x3c0 - 0x034]{};
		sha1_value Sha1;
		char Padding_0x3D4[0x2c]{};

		void verify_or_throw(uint32_t expectedSpanIndex) const;
	};

	static_assert(offsetof(header, Sha1) == 0x3c0, "Bad SqDataHeader definition");
}

namespace xivres::packed {
	enum class type : uint32_t {
		none = 0,
		placeholder = 1,
		standard = 2,
		model = 3,
		texture = 4,
		invalid = (std::numeric_limits<uint32_t>::max)(),
	};

	struct file_header {
		LE<uint32_t> HeaderSize;
		LE<type> Type;
		LE<uint32_t> DecompressedSize;
		LE<uint32_t> AllocatedSpaceUnitCount; // (Allocation - HeaderSize) / OffsetUnit
		LE<uint32_t> OccupiedSpaceUnitCount;
		LE<uint32_t> BlockCountOrVersion;

		static file_header new_empty(uint64_t decompressedSize = 0, uint64_t compressedSize = 0) {
			file_header res{
				.HeaderSize = static_cast<uint32_t>(align(sizeof file_header)),
				.Type = type::placeholder,
				.DecompressedSize = static_cast<uint32_t>(decompressedSize),
				.AllocatedSpaceUnitCount = 0,
				.OccupiedSpaceUnitCount = 0,
				.BlockCountOrVersion = static_cast<uint32_t>(compressedSize),
			};
			res.set_space_units(compressedSize);
			return res;
		}

		void set_space_units(uint64_t dataSize) {
			AllocatedSpaceUnitCount = OccupiedSpaceUnitCount = align<uint64_t, uint32_t>(dataSize, EntryAlignment).Count;
		}

		[[nodiscard]] uint64_t packed_size() const {
			return 1ULL * OccupiedSpaceUnitCount * EntryAlignment;
		}

		[[nodiscard]] uint64_t occupied_size() const {
			return HeaderSize + packed_size();
		}
	};

	struct block_header {
		static constexpr uint32_t CompressedSizeNotCompressed = 32000;
		LE<uint32_t> HeaderSize;
		LE<uint32_t> Version;
		LE<uint32_t> CompressedSize;
		LE<uint32_t> DecompressedSize;

		[[nodiscard]] bool compressed() const {
			return *CompressedSize != CompressedSizeNotCompressed;
		}

		[[nodiscard]] uint32_t packed_data_size() const {
			return *CompressedSize == CompressedSizeNotCompressed ? DecompressedSize : CompressedSize;
		}

		[[nodiscard]] uint32_t total_block_size() const {
			return sizeof block_header + packed_data_size();
		}
	};

	struct standard_block_locator {
		LE<uint32_t> Offset;
		LE<uint16_t> BlockSize;
		LE<uint16_t> DecompressedDataSize;
	};

	struct mipmap_block_locator {
		LE<uint32_t> CompressedOffset;
		LE<uint32_t> CompressedSize;
		LE<uint32_t> DecompressedSize;
		LE<uint32_t> FirstBlockIndex;
		LE<uint32_t> BlockCount;
	};

	struct model_block_locator {
		static constexpr size_t EntryIndexMap[11] = {
			0, 1, 2, 5, 8, 3, 6, 9, 4, 7, 10,
		};

		template<typename T>
		struct ChunkInfo {
			T Stack;
			T Runtime;
			T Vertex[3];
			T EdgeGeometryVertex[3];
			T Index[3];

			[[nodiscard]] const T& at(size_t i) const { return (&Stack)[EntryIndexMap[i]]; }
			T& at(size_t i) { return (&Stack)[EntryIndexMap[i]]; }
		};

		ChunkInfo<LE<uint32_t>> AlignedDecompressedSizes;
		ChunkInfo<LE<uint32_t>> ChunkSizes;
		ChunkInfo<LE<uint32_t>> FirstBlockOffsets;
		ChunkInfo<LE<uint16_t>> FirstBlockIndices;
		ChunkInfo<LE<uint16_t>> BlockCount;
		LE<uint16_t> VertexDeclarationCount;
		LE<uint16_t> MaterialCount;
		LE<uint8_t> LodCount;
		LE<uint8_t> EnableIndexBufferStreaming;
		LE<uint8_t> EnableEdgeGeometry;
		LE<uint8_t> Padding;
	};

	static_assert(sizeof model_block_locator == 184);

	static constexpr uint16_t MaxBlockDataSize = 16000;
	static constexpr uint16_t MaxBlockValidSize = MaxBlockDataSize + sizeof block_header;
	static constexpr uint16_t MaxBlockPadSize = (EntryAlignment - MaxBlockValidSize) % EntryAlignment;
	static constexpr uint16_t MaxBlockSize = MaxBlockValidSize + MaxBlockPadSize;
}

#endif
