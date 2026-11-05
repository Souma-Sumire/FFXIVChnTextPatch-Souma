#ifndef XIVRES_SCD_H_
#define XIVRES_SCD_H_

#include <chrono>
#include <cstring>
#include <map>
#include <optional>
#include <vector>

#include "stream.h"
#include "util.byte_order.h"

namespace xivres::sound {
	enum class endianness : uint8_t {
		LittleEndian = 0,
		BigEndian = 1,
	};

	struct header {
		static constexpr char SedbSignature_Value[4]{ 'S', 'E', 'D', 'B' };
		static constexpr char SscfSignature_Value[4]{ 'S', 'S', 'C', 'F' };
		static constexpr uint32_t SedbVersion_FFXIV = 3;
		static constexpr uint16_t SscfVersion_FFXIV = 4;

		char SedbSignature[4]{};
		char SscfSignature[4]{};
		LE<uint32_t> SedbVersion;
		endianness EndianFlag{};
		uint8_t SscfVersion{};
		LE<uint16_t> HeaderSize;
		LE<uint32_t> FileSize;
		uint8_t Padding_0x014[0x1C]{};
	};

	static_assert(sizeof header == 0x30);

	struct offsets {
		LE<uint16_t> Table1And4EntryCount;
		LE<uint16_t> Table2EntryCount;
		LE<uint16_t> SoundEntryCount;
		LE<uint16_t> Unknown_0x006;
		LE<uint32_t> Table2Offset;
		LE<uint32_t> SoundEntryOffset;
		LE<uint32_t> Table4Offset;
		LE<uint32_t> Padding_0x014;
		LE<uint32_t> Table5Offset;
		LE<uint32_t> Unknown_0x01C;
	};

	// Table 1's entries, named after Lumina's SoundBasicDesc (Data/Parsing/Scd/ScdSound.cs).
	// `Type` is the useful part here: it distinguishes a genuine multichannel mix from the
	// engine-switched multi-stem streams the game's music uses. Measured across FFXIV's 2175
	// music .scd files: every 4- and 6-channel entry is DynamixStream (21 of them, which is
	// exactly the set this project had been identifying by channel count alone), and every 1-
	// and 2-channel entry is Normal.
	enum class sound_type : uint8_t {
		Normal = 1,
		Random = 2,
		Stereo = 3,
		Cycle = 4,
		Order = 5,
		FourChannelSurround = 6,
		Engine = 7,
		Dialog = 8,
		FixedPosition = 10,
		DynamixStream = 11,
		GroupRandom = 12,
		GroupOrder = 13,
		Atomosgear = 14,
		ConditionalJump = 15,
		Empty = 16,
		MidiMusic = 128,
	};

	struct sound_descriptor_header {
		uint8_t TrackCount;
		uint8_t BusNumber;
		uint8_t Priority;
		sound_type Type;
		LE<uint32_t> Attribute;
		LE<float> Volume;
		LE<uint16_t> LocalNumber;
		uint8_t UserId;
		int8_t PlayHistory;
	};

	static_assert(sizeof sound_descriptor_header == 0x10);

	enum class sound_entry_format : uint32_t {
		WaveFormatPcm = 0x01,
		Ogg = 0x06,
		WaveFormatAdpcm = 0x0C,
		Empty = 0xFFFFFFFF,
	};

	// The last word of sound_entry_header, named after Lumina's AudioFlag
	// (Data/Parsing/Scd/ScdAudio.cs). An earlier version of this reader guessed the low half
	// was a count of aux chunks and walked that many; the two readings agree only while the
	// value is 0 or 1, which across every sound entry of FFXIV's 2175 music .scd files it
	// always is -- 0 in 2123 of them and 1 in 35. The format only ever describes one marker
	// chunk, so the flag is what decides whether it is there.
	enum class sound_entry_flags : uint32_t {
		None = 0,
		MarkerChunk = 0x01,
		MonoSplit = 0x02,
		VersionShiftBit = 0x01000000,
	};

	constexpr sound_entry_flags operator|(sound_entry_flags a, sound_entry_flags b) {
		return static_cast<sound_entry_flags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
	}

	constexpr sound_entry_flags operator&(sound_entry_flags a, sound_entry_flags b) {
		return static_cast<sound_entry_flags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
	}

	constexpr sound_entry_flags operator~(sound_entry_flags a) {
		return static_cast<sound_entry_flags>(~static_cast<uint32_t>(a));
	}

	constexpr bool has_flag(sound_entry_flags value, sound_entry_flags flag) {
		return (value & flag) != sound_entry_flags::None;
	}

	struct sound_entry_header {
		LE<uint32_t> StreamSize;
		LE<uint32_t> ChannelCount;
		LE<uint32_t> SamplingRate;
		LE<sound_entry_format> Format;
		LE<uint32_t> LoopStartOffset;
		LE<uint32_t> LoopEndOffset;
		LE<uint32_t> StreamOffset;   // Lumina calls this SubInfoSize
		LE<sound_entry_flags> Flags;
	};

	static_assert(sizeof sound_entry_header == 0x20);

	static_assert(sizeof sound_entry_header == 0x20);

	struct sound_entry_aux_chunk {
		static constexpr char Name_Mark[4]{ 'M', 'A', 'R', 'K' };

		struct mark_chunk_data {
			LE<uint32_t> LoopStartSampleBlockIndex;
			LE<uint32_t> LoopEndSampleBlockIndex;
			LE<uint32_t> Count;
			LE<uint32_t> SampleBlockIndices[1];
		};

		union aux_chunk_data {
			mark_chunk_data Mark;
		};

		char Name[4]{};
		LE<uint32_t> ChunkSize;
		aux_chunk_data Data;

		[[nodiscard]] std::span<const uint8_t> data_span() const {
			return util::span_cast<const uint8_t>(ChunkSize, &Data);
		}
	};

	struct sound_entry_ogg_header {
		static const uint8_t Version3XorTable[256];

		uint8_t Version{};
		uint8_t HeaderSize{};
		uint8_t EncodeByte{};
		uint8_t Padding_0x003{};
		LE<uint32_t> Unknown_0x004;
		LE<uint32_t> Unknown_0x008;
		LE<uint32_t> Unknown_0x00C;
		LE<uint32_t> SeekTableSize;
		LE<uint32_t> VorbisHeaderSize;
		uint32_t Unknown_0x018{};
		uint8_t Padding_0x01C[4]{};
	};

	// NOLINTNEXTLINE(performance-enum-size)
	enum class wave_format_tag : uint16_t {
		Pcm = 1,
		Adpcm = 2,
	};

#pragma pack(push, 1)
	struct wave_format_ex {
		wave_format_tag wFormatTag;
		uint16_t nChannels;
		uint32_t nSamplesPerSec;
		uint32_t nAvgBytesPerSec;
		uint16_t nBlockAlign;
		uint16_t wBitsPerSample;
		uint16_t cbSize;
	};

	struct adpcm_coef_set {
		short iCoef1;
		short iCoef2;
	};

	struct adpcm_wave_format {
		wave_format_ex wfx;
		short wSamplesPerBlock;
		short wNumCoef;
		adpcm_coef_set aCoef[32];
	};

	struct riff_chunk_header {
		static constexpr char Id_Riff[4]{ 'R', 'I', 'F', 'F' };
		static constexpr char Id_Format[4]{ 'f', 'm', 't', ' ' };
		static constexpr char Id_Data[4]{ 'd', 'a', 't', 'a' };

		char Id[4]{};
		LE<uint32_t> Size;
	};

	struct riff_wave_header {
		static constexpr char Format_Wave[4]{ 'W', 'A', 'V', 'E' };

		riff_chunk_header Riff;
		char Format[4]{};
	};

	struct flac_magic_and_stream_info {
		static constexpr char Magic_Value[4]{ 'f', 'L', 'a', 'C' };
		static constexpr uint32_t BlockType_StreamInfo = 0;

		char Magic[4]{};
		BE<uint32_t> BlockHeader;
		// - 1: is it last block?
		// - 7: block type
		// - 24: block length
		BE<uint16_t> MinBlockSize;
		BE<uint16_t> MaxBlockSize;
		uint8_t MinFrameSize[3]{};
		uint8_t MaxFrameSize[3]{};
		BE<uint64_t> Format;
		// - sampling rate: 20
		// - channels: 3
		// - bits per sample: 5
		// - sample count: 36
		uint8_t Md5[16]{};

		[[nodiscard]] bool is_last_block() const { return *BlockHeader >> 31; }
		[[nodiscard]] uint32_t block_type() const { return (*BlockHeader >> 24) & 0x7F; }
		[[nodiscard]] uint32_t block_length() const { return *BlockHeader & 0xFFFFFF; }
		[[nodiscard]] uint32_t sampling_rate() const { return static_cast<uint32_t>(*Format >> 44); }
		[[nodiscard]] uint32_t channels() const { return static_cast<uint32_t>((*Format >> 41) & 0x7) + 1; }
		[[nodiscard]] uint32_t bits_per_sample() const { return static_cast<uint32_t>((*Format >> 36) & 0x1F) + 1; }
		[[nodiscard]] uint64_t sample_count() const { return *Format & 0xF'FFFF'FFFFULL; }
	};

	// Followed by SegmentCount bytes of segment table.
	struct ogg_page_header {
		static constexpr char Magic_Value[4]{ 'O', 'g', 'g', 'S' };
		static constexpr uint64_t GranulePosition_None = UINT64_MAX;

		char Magic[4]{};
		uint8_t Version{};
		uint8_t HeaderType{};
		LE<uint64_t> GranulePosition;
		LE<uint32_t> SerialNumber;
		LE<uint32_t> SequenceNumber;
		LE<uint32_t> Checksum;
		uint8_t SegmentCount{};
	};

	struct vorbis_identification_header {
		static constexpr uint8_t PacketType_Identification = 1;
		static constexpr char Magic_Value[6]{ 'v', 'o', 'r', 'b', 'i', 's' };

		uint8_t PacketType{};
		char Magic[6]{};
		LE<uint32_t> Version;
		uint8_t Channels{};
		LE<uint32_t> SamplingRate;
		LE<int32_t> BitrateMaximum;
		LE<int32_t> BitrateNominal;
		LE<int32_t> BitrateMinimum;
		uint8_t BlockSizes{};
		uint8_t Framing{};
	};
#pragma pack(pop)

	static_assert(sizeof riff_chunk_header == 8);
	static_assert(sizeof riff_wave_header == 12);
	static_assert(sizeof flac_magic_and_stream_info == 4 + 4 + 34);
	static_assert(sizeof ogg_page_header == 27);
	static_assert(sizeof vorbis_identification_header == 30);

	class reader {
		const std::shared_ptr<stream> m_stream;
		const std::vector<uint8_t> m_headerBuffer;
		const header& m_header;
		const offsets& m_offsets;

		const std::span<const uint32_t> m_offsetsTable1;
		const std::span<const uint32_t> m_offsetsTable2;
		const std::span<const uint32_t> m_soundEntryOffsets;
		const std::span<const uint32_t> m_offsetsTable4;
		const std::span<const uint32_t> m_offsetsTable5;

		const uint32_t m_endOfSoundEntries;
		const uint32_t m_endOfTable5;
		const uint32_t m_endOfTable2;
		const uint32_t m_endOfTable1;
		const uint32_t m_endOfTable4;

		[[nodiscard]] std::vector<uint8_t> read_entry(const std::span<const uint32_t>& offsets, uint32_t endOffset, size_t index) const;

		[[nodiscard]] std::vector<std::vector<uint8_t>> read_table(const std::span<const uint32_t>& offsets, uint32_t endOffset) const;

		[[nodiscard]] static std::vector<uint8_t> get_header_bytes(const stream& strm);

	public:
		reader(std::shared_ptr<stream> strm);

		struct sound_item {
			std::vector<uint8_t> Buffer;
			sound_entry_header* Header;
			std::vector<sound_entry_aux_chunk*> AuxChunks;
			std::span<uint8_t> ExtraData;
			std::span<uint8_t> Data;

			[[nodiscard]] std::vector<uint32_t> marked_sample_block_indices() const;

			[[nodiscard]] const wave_format_ex& get_wav_header() const;

			[[nodiscard]] const adpcm_wave_format& get_adpcm_wav_header() const;

			[[nodiscard]] std::vector<uint8_t> get_wav_file() const;

			[[nodiscard]] const sound_entry_ogg_header& get_ogg_seek_table_header() const;

			[[nodiscard]] std::span<const uint32_t> get_ogg_seek_table() const;

			[[nodiscard]] std::vector<uint8_t> get_ogg_file() const;

			struct audio_info {
				size_t Channels;
				size_t SamplingRate;
				size_t LoopStartBlockIndex;
				size_t LoopEndBlockIndex;
				std::vector<uint8_t> Data;
			};

			[[nodiscard]] audio_info get_ogg_decoded() const;
			
			[[nodiscard]] static audio_info decode_ogg(const std::vector<uint8_t>& oggf);
		};

		[[nodiscard]] std::vector<std::vector<uint8_t>> read_table_1() const { return read_table(m_offsetsTable1, m_endOfTable1); }

		[[nodiscard]] std::optional<sound_descriptor_header> read_sound_descriptor(size_t index) const {
			if (index >= m_offsetsTable1.size())
				return std::nullopt;
			const auto entry = read_entry(m_offsetsTable1, m_endOfTable1, index);
			if (entry.size() < sizeof(sound_descriptor_header))
				return std::nullopt;
			sound_descriptor_header header;
			std::memcpy(&header, entry.data(), sizeof header);
			return header;
		}

		[[nodiscard]] std::vector<std::vector<uint8_t>> read_table_2() const { return read_table(m_offsetsTable2, m_endOfTable2); }

		[[nodiscard]] std::vector<sound_item> read_sound_table() const {
			std::vector<sound_item> res;
			for (size_t i = 0; i < sound_item_count(); ++i)
				res.push_back(read_sound_item(i));
			return res;
		}

		[[nodiscard]] std::vector<std::vector<uint8_t>> read_table_4() const { return read_table(m_offsetsTable4, m_endOfTable4); }

		[[nodiscard]] std::vector<std::vector<uint8_t>> read_table_5() const { return read_table(m_offsetsTable5, m_endOfTable5); }

		[[nodiscard]] size_t sound_item_count() const { return m_soundEntryOffsets.size(); }

		[[nodiscard]] sound_item read_sound_item(size_t entryIndex) const;

		[[nodiscard]] std::optional<double> stream_bytes_per_second(size_t entryIndex) const;
	};

	class writer {
	public:
		struct sound_item {
			sound_entry_header Header;
			std::map<std::string, std::vector<uint8_t>> AuxChunks;
			std::vector<uint8_t> ExtraData;
			std::vector<uint8_t> Data;

			[[nodiscard]] wave_format_ex& as_wave_format_ex();
			[[nodiscard]] const wave_format_ex& as_wave_format_ex() const;

			[[nodiscard]] size_t calculate_entry_size() const;
			void export_to(std::vector<uint8_t>& res) const;

			void set_mark_chunks(uint32_t loopStartSampleBlockIndex, uint32_t loopEndSampleBlockIndex, std::span<const uint32_t> marks);

			static sound_item make_from_reader_sound_item(const reader::sound_item& item); 

			static sound_item make_from_wave(const linear_reader<uint8_t>& reader);

			static sound_item make_from_ogg_encode(
				size_t channels,
				size_t samplingRate,
				size_t loopStartBlockIndex,
				size_t loopEndBlockIndex,
				const linear_reader<uint8_t>& floatSamplesReader,
				const std::function<bool(size_t blockIndex)>& progressCallback,
				std::span<const uint32_t> markIndices,
				float baseQuality = 1.f);

			static sound_item make_from_ogg(const linear_reader<uint8_t>& reader);

			static sound_item make_from_ogg(
				const std::vector<uint8_t>& headerPages,
				std::vector<uint8_t> dataPages,
				uint32_t channels,
				uint32_t samplingRate,
				uint32_t loopStartOffset,
				uint32_t loopEndOffset,
				std::span<uint32_t> seekTable
			);

			static sound_item make_empty(std::optional<std::chrono::milliseconds> duration = std::nullopt);
		};

	private:
		std::vector<std::vector<uint8_t>> m_table1;
		std::vector<std::vector<uint8_t>> m_table2;
		std::vector<sound_item> m_soundEntries;
		std::vector<std::vector<uint8_t>> m_table4;
		std::vector<std::vector<uint8_t>> m_table5;

	public:
		void set_table_1(std::vector<std::vector<uint8_t>> t) {
			m_table1 = std::move(t);
		}

		void set_table_2(std::vector<std::vector<uint8_t>> t) {
			m_table2 = std::move(t);
		}

		void set_table_4(std::vector<std::vector<uint8_t>> t) {
			m_table4 = std::move(t);
		}

		void set_table_5(std::vector<std::vector<uint8_t>> t) {
			// Apparently the game still plays sounds without this table

			m_table5 = std::move(t);
		}

		void set_sound_item(size_t index, sound_item entry) {
			if (m_soundEntries.size() <= index)
				m_soundEntries.resize(index + 1);
			m_soundEntries[index] = std::move(entry);
		}

		[[nodiscard]] std::vector<uint8_t> export_to_bytes() const;
	};
}

#endif
