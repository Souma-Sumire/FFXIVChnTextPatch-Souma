#ifndef XIVRES_FONTGENERATOR_TRUETYPEUTILS_H_
#define XIVRES_FONTGENERATOR_TRUETYPEUTILS_H_

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "xivres/util.byte_order.h"
#include "xivres/util.unicode.h"

#pragma pack(push, 2)
namespace FontChanger::FixedSizeFont::truetype {
	using xivres::BE;
	using xivres::LE;
	using xivres::RNE;

	union TagStruct {
		char Tag[4];
		uint32_t NativeValue;
		RNE<uint32_t> ReverseNativeValue;

		bool operator==(const TagStruct& r) const {
			return NativeValue == r.NativeValue;
		}
	};

	struct Fixed {
		BE<uint16_t> Major;
		BE<uint16_t> Minor;
	};
	static_assert(sizeof(Fixed) == 0x04);

	struct OffsetTableStruct {
		Fixed SfntVersion;
		BE<uint16_t> TableCount;
		BE<uint16_t> SearchRange;
		BE<uint16_t> EntrySelector;
		BE<uint16_t> RangeShift;
	};
	static_assert(sizeof(OffsetTableStruct) == 0x0C);

	struct DirectoryTableEntry {
		TagStruct Tag;
		BE<uint32_t> Checksum;
		BE<uint32_t> Offset;
		BE<uint32_t> Length;
	};
	static_assert(sizeof(DirectoryTableEntry) == 0x10);

	enum class PlatformId : uint16_t {
		Unicode = 0,
		Macintosh = 1,  // discouraged
		Iso = 2,  // deprecated
		Windows = 3,
		Custom = 4,  // OTF Windows NT compatibility mapping
	};

	enum class UnicodePlatformEncodingId : uint16_t {
		Unicode_1_0 = 0,  // deprecated
		Unicode_1_1 = 1,  // deprecated
		IsoIec_10646 = 2,  // deprecated
		Unicode_2_0_Bmp = 3,
		Unicode_2_0_Full = 4,
		UnicodeVariationSequences = 5,
		UnicodeFullRepertoire = 6,
	};

	enum class MacintoshPlatformEncodingId : uint16_t {
		Roman = 0,
	};

	enum class IsoPlatformEncodingId : uint16_t {
		Ascii = 0,
		Iso_10646 = 1,
		Iso_8859_1 = 2,
	};

	enum class WindowsPlatformEncodingId : uint16_t {
		Symbol = 0,
		UnicodeBmp = 1,
		ShiftJis = 2,
		Prc = 3,
		Big5 = 4,
		Wansung = 5,
		Johab = 6,
		UnicodeFullRepertoire = 10,
	};

	struct FeatureTable {
		BE<uint16_t> FeatureParamsOffset;
		BE<uint16_t> LookupIndexCount;
		BE<uint16_t> LookupListIndices[1];
	};

	struct FeatureRecord {
		TagStruct FeatureTag;
		BE<uint16_t> FeatureOffset;
	};

	struct FeatureList {
		BE<uint16_t> Count;
		FeatureRecord Records[1];

		class View {
			union {
				const FeatureList* m_obj;
				const char* m_bytes;
			};
			size_t m_length;

		public:
			View() : m_obj(nullptr), m_length(0) {}
			View(std::nullptr_t) : View() {}
			View(decltype(m_obj) pObject, size_t length)
				: m_obj(pObject), m_length(length) {}
			View(View&&) = default;
			View(const View&) = default;
			View& operator=(View&&) = default;
			View& operator=(const View&) = default;
			View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; return *this; }
			View(const void* pData, size_t length) : View(std::span(static_cast<const char*>(pData), length)) {}
			template<typename T>
			View(std::span<T> data) : View() {
				const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

				if (data.size_bytes() < sizeof(uint16_t))
					return;

				if (sizeof(uint16_t) + sizeof(FeatureRecord) * *obj->Count > data.size_bytes())
					return;

				m_obj = obj;
				m_length = data.size_bytes();
			}

			operator bool() const {
				return !!m_obj;
			}

			decltype(m_obj) operator*() const {
				return m_obj;
			}

			decltype(m_obj) operator->() const {
				return m_obj;
			}

			// Returns the lookup indices of the feature, or an empty span if the feature table is out of bounds.
			[[nodiscard]] std::span<const BE<uint16_t>> LookupIndices(size_t featureIndex) const {
				const auto offset = static_cast<size_t>(*m_obj->Records[featureIndex].FeatureOffset);
				if (offset + offsetof(FeatureTable, LookupListIndices) > m_length)
					return {};
				const auto& feature = *reinterpret_cast<const FeatureTable*>(m_bytes + offset);
				const auto count = static_cast<size_t>(*feature.LookupIndexCount);
				if (offset + offsetof(FeatureTable, LookupListIndices) + sizeof(uint16_t) * count > m_length)
					return {};
				return { feature.LookupListIndices, count };
			}

			[[nodiscard]] std::span<const FeatureRecord> Records() const {
				return { m_obj->Records, *m_obj->Count };
			}
		};
	};
	
	struct LookupList {
		BE<uint16_t> Count;
		BE<uint16_t> Offsets[1];

		class View {
			union {
				const LookupList* m_obj;
				const char* m_bytes;
			};
			size_t m_length;

		public:
			View() : m_obj(nullptr), m_length(0) {}
			View(std::nullptr_t) : View() {}
			View(decltype(m_obj) pObject, size_t length)
				: m_obj(pObject), m_length(length) {}
			View(View&&) = default;
			View(const View&) = default;
			View& operator=(View&&) = default;
			View& operator=(const View&) = default;
			View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; return *this; }
			View(const void* pData, size_t length) : View(std::span(static_cast<const char*>(pData), length)) {}
			template<typename T>
			View(std::span<T> data) : View() {
				const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

				if (data.size_bytes() < sizeof(uint16_t))
					return;

				if (2 * (*obj->Count + 1) > data.size_bytes())
					return;

				m_obj = obj;
				m_length = data.size_bytes();
			}

			operator bool() const {
				return !!m_obj;
			}

			decltype(m_obj) operator*() const {
				return m_obj;
			}

			decltype(m_obj) operator->() const {
				return m_obj;
			}

			[[nodiscard]] std::span<const BE<uint16_t>> Offsets() const {
				return { m_obj->Offsets, *m_obj->Count };
			}
		};
	};

	enum class LookupType : uint16_t {
		SingleAdjustment = 1,
		PairAdjustment = 2,
		CursiveAttachment = 3,
		MarkToBaseAttachment = 4,
		MarkToLigatureAttachment = 5,
		MarkToMarkAttachment = 6,
		ContextPositioning = 7,
		ChainedContextPositioning = 8,
		ExtensionPositioning = 9,
	};

	struct LookupTable {
		struct LookupFlags {
			uint8_t RightToLeft : 1;
			uint8_t IgnoreBaseGlyphs : 1;
			uint8_t IgnoreLigatures : 1;
			uint8_t IgnoreMarks : 1;
			uint8_t UseMarkFilteringSet : 1;
			uint8_t Reserved : 3;
		};
		static_assert(sizeof(LookupFlags) == 1);

		struct LookupTableHeader {
			BE<LookupType> LookupType;
			uint8_t MarkAttachmentType;
			LookupFlags LookupFlag;
			BE<uint16_t> SubtableCount;
		};

		LookupTableHeader Header;
		BE<uint16_t> SubtableOffsets[1];

		class View {
			union {
				const LookupTable* m_obj;
				const char* m_bytes;
			};
			size_t m_length;

		public:
			View() : m_obj(nullptr), m_length(0) {}
			View(std::nullptr_t) : View() {}
			View(decltype(m_obj) pObject, size_t length)
				: m_obj(pObject), m_length(length) {}
			View(View&&) = default;
			View(const View&) = default;
			View& operator=(View&&) = default;
			View& operator=(const View&) = default;
			View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; return *this; }
			View(const void* pData, size_t length) : View(std::span(static_cast<const char*>(pData), length)) {}
			template<typename T>
			View(std::span<T> data) : View() {
				if (data.size_bytes() < sizeof(Header))
					return;

				const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

				if (data.size_bytes() < sizeof(Header) + static_cast<size_t>(2) * (*obj->Header.SubtableCount) + (obj->Header.LookupFlag.UseMarkFilteringSet ? 2 : 0))
					return;

				m_obj = obj;
				m_length = data.size_bytes();
			}

			operator bool() const {
				return !!m_obj;
			}

			decltype(m_obj) operator*() const {
				return m_obj;
			}

			decltype(m_obj) operator->() const {
				return m_obj;
			}

			[[nodiscard]] std::span<const BE<uint16_t>> SubtableOffsets() const {
				return { m_obj->SubtableOffsets, *m_obj->Header.SubtableCount };
			}

			[[nodiscard]] std::span<const char> SubtableSpan(size_t index) const {
				const auto offset = static_cast<size_t>(*m_obj->SubtableOffsets[index]);
				if (offset >= m_length)
					return {};
				return { m_bytes + offset, m_length - offset };
			}

			[[nodiscard]] uint16_t MarkFilteringSet() const {
				if (m_obj->Header.LookupFlag.UseMarkFilteringSet)
					return m_obj->SubtableOffsets[*m_obj->Header.SubtableCount];
				return (std::numeric_limits<uint16_t>::max)();
			}
		};
	};

	struct CoverageTable {
		struct FormatHeader {
			BE<uint16_t> FormatId;
			BE<uint16_t> Count;
		};

		struct RangeRecord {
			BE<uint16_t> StartGlyphId;
			BE<uint16_t> EndGlyphId;
			BE<uint16_t> StartCoverageIndex;
		};

		FormatHeader Header;
		union {
			BE<uint16_t> Glyphs[1];
			RangeRecord RangeRecords[1];
		};

		class View {
			union {
				const CoverageTable* m_obj;
				const char* m_bytes;
			};
			size_t m_length;

		public:
			View() : m_obj(nullptr), m_length(0) {}
			View(std::nullptr_t) : View() {}
			View(decltype(m_obj) pObject, size_t length)
				: m_obj(pObject), m_length(length) {}
			View(View&&) = default;
			View(const View&) = default;
			View& operator=(View&&) = default;
			View& operator=(const View&) = default;
			View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; return *this; }
			View(const void* pData, size_t length) : View(std::span(static_cast<const char*>(pData), length)) {}
			template<typename T>
			View(std::span<T> data) : View() {
				if (data.size_bytes() < sizeof(FormatHeader))
					return;

				const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

				const auto count = static_cast<size_t>(*obj->Header.Count);
				switch (obj->Header.FormatId) {
					case 1:  // NOLINT(bugprone-branch-clone)
						if (data.size_bytes() < sizeof(FormatHeader) + sizeof(uint16_t) * count)
							return;
						break;

					case 2:
						if (data.size_bytes() < sizeof(FormatHeader) + sizeof(RangeRecord) * count)
							return;
						break;

					default:
						return;
				}

				m_obj = obj;
				m_length = data.size_bytes();
			}

			operator bool() const {
				return !!m_obj;
			}

			decltype(m_obj) operator*() const {
				return m_obj;
			}

			decltype(m_obj) operator->() const {
				return m_obj;
			}

			[[nodiscard]] std::span<const BE<uint16_t>> GlyphSpan() const {
				return { m_obj->Glyphs, m_obj->Header.Count };
			}

			[[nodiscard]] std::span<const RangeRecord> RangeRecordSpan() const {
				return { m_obj->RangeRecords, m_obj->Header.Count };
			}

			[[nodiscard]] size_t GetCoverageIndex(size_t glyphId) const {
				switch (m_obj->Header.FormatId) {
					case 1:
					{
						const auto glyphSpan = GlyphSpan();
						const auto bec = BE<uint16_t>(static_cast<uint16_t>(glyphId));
						const auto it = std::ranges::lower_bound(glyphSpan, bec, [](const BE<uint16_t>& l, const BE<uint16_t>& r) { return *l < *r; });
						if (it != glyphSpan.end() && **it == glyphId)
							return it - glyphSpan.begin();

						break;
					}

					case 2:
					{
						// First range whose end is not before glyphId.
						const auto rangeSpan = RangeRecordSpan();
						const auto it = std::ranges::lower_bound(rangeSpan, glyphId, {}, [](const RangeRecord& r) { return static_cast<size_t>(*r.EndGlyphId); });
						if (it != rangeSpan.end() && *it->StartGlyphId <= glyphId)
							return *it->StartCoverageIndex + glyphId - *it->StartGlyphId;

						break;
					}
				}

				return (std::numeric_limits<size_t>::max)();
			}
		};
	};

	struct ClassDefTable {
		struct Format1ClassArray {
			struct FormatHeader {
				BE<uint16_t> FormatId;
				BE<uint16_t> StartGlyphId;
				BE<uint16_t> GlyphCount;
			};

			FormatHeader Header;
			BE<uint16_t> ClassValueArray[1];
		};

		struct Format2ClassRanges {
			struct FormatHeader {
				BE<uint16_t> FormatId;
				BE<uint16_t> ClassRangeCount;
			};

			struct ClassRangeRecord {
				BE<uint16_t> StartGlyphId;
				BE<uint16_t> EndGlyphId;
				BE<uint16_t> Class;
			};

			FormatHeader Header;
			ClassRangeRecord ClassValueArray[1];
		};

		union {
			BE<uint16_t> FormatId;
			Format1ClassArray Format1;
			Format2ClassRanges Format2;
		};

		class View {
			union {
				const ClassDefTable* m_obj;
				const char* m_bytes;
			};
			size_t m_length;

		public:
			View() : m_obj(nullptr), m_length(0) {}
			View(std::nullptr_t) : View() {}
			View(decltype(m_obj) pObject, size_t length)
				: m_obj(pObject), m_length(length) {}
			View(View&&) = default;
			View(const View&) = default;
			View& operator=(View&&) = default;
			View& operator=(const View&) = default;
			View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; return *this; }
			View(const void* pData, size_t length) : View(std::span(static_cast<const char*>(pData), length)) {}
			template<typename T>
			View(std::span<T> data) : View() {
				if (data.size_bytes() < sizeof(FormatId))
					return;

				const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

				switch (*obj->FormatId) {
					case 1:
						if (data.size_bytes() < sizeof(Format1ClassArray::FormatHeader) + sizeof(BE<uint16_t>) * (*obj->Format1.Header.GlyphCount))
							return;
						break;

					case 2:
						if (data.size_bytes() < sizeof(Format2ClassRanges::FormatHeader) + sizeof(Format2ClassRanges::ClassRangeRecord) * (*obj->Format2.Header.ClassRangeCount))
							return;
						break;

					default:
						return;
				}

				m_obj = obj;
				m_length = data.size_bytes();
			}

			operator bool() const {
				return !!m_obj;
			}

			decltype(m_obj) operator*() const {
				return m_obj;
			}

			decltype(m_obj) operator->() const {
				return m_obj;
			}

			[[nodiscard]] std::map<uint16_t, std::set<uint16_t>> ClassToGlyphMap() const {
				std::map<uint16_t, std::set<uint16_t>> res;
				switch (m_obj->FormatId) {
					case 1:
					{
						const auto startId = *m_obj->Format1.Header.StartGlyphId;
						const auto count = *m_obj->Format1.Header.GlyphCount;
						for (auto i = 0; i < count; i++)
							res[*m_obj->Format1.ClassValueArray[i]].insert(static_cast<uint16_t>(startId + i));
						break;
					}

					case 2:
					{
						for (const auto& range : std::span(m_obj->Format2.ClassValueArray, m_obj->Format2.Header.ClassRangeCount)) {
							auto& target = res[*range.Class];
							for (uint32_t i = *range.StartGlyphId, i_ = *range.EndGlyphId; i <= i_; i++)
								target.insert(static_cast<uint16_t>(i));
						}
						break;
					}
				}
				return res;
			}

			[[nodiscard]] std::map<uint16_t, uint16_t> GlyphToClassMap() const {
				std::map<uint16_t, uint16_t> res;
				switch (m_obj->FormatId) {
					case 1:
					{
						const auto startId = *m_obj->Format1.Header.StartGlyphId;
						const auto count = *m_obj->Format1.Header.GlyphCount;
						for (auto i = 0; i < count; i++)
							res[static_cast<uint16_t>(startId + i)] = *m_obj->Format1.ClassValueArray[i];
						break;
					}

					case 2:
					{
						for (const auto& range : std::span(m_obj->Format2.ClassValueArray, m_obj->Format2.Header.ClassRangeCount)) {
							const auto classValue = *range.Class;
							for (uint32_t i = *range.StartGlyphId, i_ = *range.EndGlyphId; i <= i_; i++)
								res[static_cast<uint16_t>(i)] = classValue;
						}
						break;
					}
				}
				return res;
			}

			[[nodiscard]] uint16_t GetClass(uint16_t glyphId) const {
				switch (m_obj->FormatId) {
					case 1:
					{
						const auto startId = *m_obj->Format1.Header.StartGlyphId;
						if (startId <= glyphId && glyphId < startId + *m_obj->Format1.Header.GlyphCount)
							return m_obj->Format1.ClassValueArray[glyphId - startId];

						return 0;
					}

					case 2:
					{
						// First range whose end is not before glyphId.
						const auto rangeSpan = std::span(m_obj->Format2.ClassValueArray, m_obj->Format2.Header.ClassRangeCount);
						const auto it = std::ranges::lower_bound(rangeSpan, glyphId, {}, [](const Format2ClassRanges::ClassRangeRecord& r) { return *r.EndGlyphId; });
						if (it != rangeSpan.end() && *it->StartGlyphId <= glyphId)
							return *it->Class;

						return 0;
					}
				}

				return 0;
			}
		};
	};

	struct Head {
		// https://docs.microsoft.com/en-us/typography/opentype/spec/head
		// https://developer.apple.com/fonts/TrueType-Reference-Manual/RM06/Chap6head.html

		static constexpr TagStruct DirectoryTableTag{ { 'h', 'e', 'a', 'd' } };
		static constexpr uint32_t MagicNumberValue = 0x5F0F3CF5;

		struct HeadFlags {
			uint16_t BaselineForFontAtZeroY : 1;
			uint16_t LeftSideBearingAtZeroX : 1;
			uint16_t InstructionsDependOnPointSize : 1;
			uint16_t ForcePpemsInteger : 1;

			uint16_t InstructionsAlterAdvanceWidth : 1;
			uint16_t VerticalLayout : 1;
			uint16_t Reserved6 : 1;
			uint16_t RequiresLayoutForCorrectLinguisticRendering : 1;

			uint16_t IsAatFont : 1;
			uint16_t ContainsRtlGlyph : 1;
			uint16_t ContainsIndicStyleRearrangementEffects : 1;
			uint16_t Lossless : 1;

			uint16_t ProduceCompatibleMetrics : 1;
			uint16_t OptimizedForClearType : 1;
			uint16_t IsLastResortFont : 1;
			uint16_t Reserved15 : 1;
		};
		static_assert(sizeof(HeadFlags) == 2);

		struct MacStyleFlags {
			uint16_t Bold : 1;
			uint16_t Italic : 1;
			uint16_t Underline : 1;
			uint16_t Outline : 1;
			uint16_t Shadow : 1;
			uint16_t Condensed : 1;
			uint16_t Extended : 1;
			uint16_t Reserved : 9;
		};
		static_assert(sizeof(MacStyleFlags) == 2);

		Fixed Version;
		Fixed FontRevision;
		BE<uint32_t> ChecksumAdjustment;
		BE<uint32_t> MagicNumber;
		BE<HeadFlags> Flags;
		BE<uint16_t> UnitsPerEm;
		BE<uint64_t> CreatedTimestamp;
		BE<uint64_t> ModifiedTimestamp;
		BE<int16_t> MinX;
		BE<int16_t> MinY;
		BE<int16_t> MaxX;
		BE<int16_t> MaxY;
		BE<MacStyleFlags> MacStyle;
		BE<uint16_t> LowestRecommendedPpem;
		BE<int16_t> FontDirectionHint;
		BE<int16_t> IndexToLocFormat;
		BE<int16_t> GlyphDataFormat;

		class View {
			union {
				const Head* m_obj;
				const char* m_bytes;
			};
			size_t m_length;

		public:
			View() : m_obj(nullptr), m_length(0) {}
			View(std::nullptr_t) : View() {}
			View(decltype(m_obj) pObject, size_t length)
				: m_obj(pObject), m_length(length) {}
			View(View&&) = default;
			View(const View&) = default;
			View& operator=(View&&) = default;
			View& operator=(const View&) = default;
			View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; return *this; }
			View(const void* pData, size_t length) : View(std::span(static_cast<const char*>(pData), length)) {}
			template<typename T>
			View(std::span<T> data) : View() {
				if (sizeof(*m_obj) > data.size_bytes())
					return;

				const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

				if (obj->Version.Major != 1)
					return;
				if (obj->MagicNumber != MagicNumberValue)
					return;

				m_obj = obj;
				m_length = data.size_bytes();
			}

			operator bool() const {
				return !!m_obj;
			}

			decltype(m_obj) operator*() const {
				return m_obj;
			}

			decltype(m_obj) operator->() const {
				return m_obj;
			}
		};
	};

	struct Name {
		// https://docs.microsoft.com/en-us/typography/opentype/spec/name
		// https://developer.apple.com/fonts/TrueType-Reference-Manual/RM06/Chap6name.html

		static constexpr TagStruct DirectoryTableTag{ { 'n', 'a', 'm', 'e' } };

		enum class NameId : uint16_t {
			CopyrightNotice = 0,
			FamilyName = 1,
			SubfamilyName = 2,
			UniqueId = 3,
			FullFontName = 4,
			VersionString = 5,
			PostScriptName = 6,
			Trademark = 7,
			Manufacturer = 8,
			Designer = 9,
			Description = 10,
			UrlVendor = 11,
			UrlDesigner = 12,
			LicenseDescription = 13,
			LicenseInfoUrl = 14,
			TypographicFamilyName = 16,
			TypographicSubfamilyName = 17,
			CompatibleFullMac = 18,
			SampleText = 19,
			PoscSriptCidFindFontName = 20,
			WwsFamilyName = 21,
			WwsSubfamilyName = 22,
			LightBackgroundPalette = 23,
			DarkBackgroundPalette = 24,
			VariationPostScriptNamePrefix = 25,
		};

		struct NameHeader {
			BE<uint16_t> Version;
			BE<uint16_t> Count;
			BE<uint16_t> StorageOffset;
		};

		struct NameRecord {
			BE<PlatformId> Platform;
			union {
				BE<uint16_t> EncodingId;
				BE<UnicodePlatformEncodingId> UnicodeEncoding;
				BE<MacintoshPlatformEncodingId> MacintoshEncoding;
				BE<IsoPlatformEncodingId> IsoEncoding;
				BE<WindowsPlatformEncodingId> WindowsEncoding;
			};
			BE<uint16_t> LanguageId;
			BE<NameId> NameId;
			BE<uint16_t> Length;
			BE<uint16_t> StringOffset;
		};

		struct LanguageHeader {
			BE<uint16_t> Count;
		};

		struct LanguageRecord {
			BE<uint16_t> Length;
			BE<uint16_t> LanguageTagOffset;
		};

		NameHeader Header;
		NameRecord Record[1];

		class View {
			union {
				const Name* m_obj;
				const char* m_bytes;
			};
			size_t m_length;

		public:
			View() : m_obj(nullptr), m_length(0) {}
			View(std::nullptr_t) : View() {}
			View(decltype(m_obj) pObject, size_t length) : m_obj(pObject), m_length(length) {}
			View(View&&) = default;
			View(const View&) = default;
			View& operator=(View&&) = default;
			View& operator=(const View&) = default;
			View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; return *this; }
			View(const void* pData, size_t length) : View(std::span(static_cast<const char*>(pData), length)) {}
			template<typename T>
			View(std::span<T> data) : View() {
				if (data.size_bytes() < sizeof(NameHeader))
					return;

				const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

				m_obj = obj;
				m_length = data.size_bytes();
			}

			operator bool() const {
				return !!m_obj;
			}

			decltype(m_obj) operator*() const {
				return m_obj;
			}

			decltype(m_obj) operator->() const {
				return m_obj;
			}

			[[nodiscard]] std::span<const NameRecord> RecordSpan() const {
				return { m_obj->Record, *m_obj->Header.Count };
			}

			[[nodiscard]] uint16_t LanguageCount() const {
				return *m_obj->Header.Version >= 1 ? **reinterpret_cast<const BE<uint16_t>*>(&m_obj->Record[*m_obj->Header.Count]) : 0;
			}

			[[nodiscard]] std::span<const LanguageRecord> LanguageSpan() const {
				if (*m_obj->Header.Version == 0)
					return {};
				return { reinterpret_cast<const LanguageRecord*>(reinterpret_cast<const char*>(&m_obj->Record[*m_obj->Header.Count]) + 2), LanguageCount() };
			}

			[[nodiscard]] const LanguageRecord& Language(size_t i) {
				return reinterpret_cast<const LanguageRecord*>(reinterpret_cast<const char*>(&m_obj->Record[*m_obj->Header.Count]) + 2)[i];
			}

			template<typename TUnicodeString = std::string>
			[[nodiscard]] TUnicodeString GetUnicodeName(uint16_t preferredLanguageId, NameId nameId) const {
				const auto recordSpans = RecordSpan();
				for (const auto usePreferredLanguageId : { true, false }) {
					for (const auto& record : recordSpans) {
						if (record.LanguageId != preferredLanguageId && usePreferredLanguageId)
							continue;

						if (record.NameId != nameId)
							continue;

						const auto offsetStart = static_cast<size_t>(*m_obj->Header.StorageOffset) + *record.StringOffset;
						const auto offsetEnd = offsetStart + *record.Length;
						if (offsetEnd > m_length)
							continue;

						switch (record.Platform) {
							case PlatformId::Unicode:
								switch (*record.UnicodeEncoding) {
									case UnicodePlatformEncodingId::Unicode_2_0_Bmp:
									case UnicodePlatformEncodingId::Unicode_2_0_Full:
										goto decode_utf16be;
								}
								break;

							case PlatformId::Macintosh:
								switch (*record.MacintoshEncoding) {
									case MacintoshPlatformEncodingId::Roman:
										goto decode_ascii;
								}
								break;

							case PlatformId::Windows:
								switch (*record.WindowsEncoding) {
									case WindowsPlatformEncodingId::Symbol:
									case WindowsPlatformEncodingId::UnicodeBmp:
									case WindowsPlatformEncodingId::UnicodeFullRepertoire:
										goto decode_utf16be;
								}
								break;
						}

						continue;

					decode_ascii:
						{
							const auto pString = &m_bytes[offsetStart];
							const auto pStringEnd = &m_bytes[offsetEnd];
							std::u8string res;
							res.reserve(pStringEnd - pString);
							for (auto p = pString; p < pStringEnd; p++) {
								const auto x = static_cast<uint8_t>(*p);
								if (0 < x && x < 0x80 && x != '\\') {
									res.push_back(x);
								} else if (x == '\\') {
									res.push_back('\\');
									res.push_back('\\');
								} else {
									res.push_back('\\');
									res.push_back('x');
									res.push_back(((x >> 4) > 10) ? ('A' + (x >> 4) - 10) : ('0' + (x >> 4)));
									res.push_back(((x & 0xF) > 10) ? ('A' + (x & 0xF) - 10) : ('0' + (x & 0xF)));
								}
							}

							return xivres::util::unicode::convert<TUnicodeString>(res);
						}

					decode_utf16be:
						{
							const auto pString = reinterpret_cast<const BE<char16_t>*>(&m_bytes[offsetStart]);
							const auto pStringEnd = reinterpret_cast<const BE<char16_t>*>(&m_bytes[offsetEnd]);
							std::u16string u16;
							u16.reserve(pStringEnd - pString);
							for (auto x = pString; x < pStringEnd; x++)
								u16.push_back(*x);

							return xivres::util::unicode::convert<TUnicodeString>(u16);
						}
					}
				}
				return {};
			}

			template<typename TUnicodeString = std::string>
			[[nodiscard]] TUnicodeString GetPreferredFamilyName(uint16_t preferredLanguageId) const {
				auto r = GetUnicodeName<TUnicodeString>(preferredLanguageId, NameId::TypographicFamilyName);
				if (!r.empty())
					return r;
				return GetUnicodeName<TUnicodeString>(preferredLanguageId, NameId::FamilyName);
			}

			template<typename TUnicodeString = std::string>
			[[nodiscard]] TUnicodeString GetPreferredSubfamilyName(uint16_t preferredLanguageId) const {
				auto r = GetUnicodeName<TUnicodeString>(preferredLanguageId, NameId::TypographicSubfamilyName);
				if (!r.empty())
					return r;
				return GetUnicodeName<TUnicodeString>(preferredLanguageId, NameId::SubfamilyName);
			}
		};
	};

	struct Cmap {
		// https://docs.microsoft.com/en-us/typography/opentype/spec/cmap
		// https://developer.apple.com/fonts/TrueType-Reference-Manual/RM06/Chap6cmap.html

		static constexpr TagStruct DirectoryTableTag{ { 'c', 'm', 'a', 'p' } };

		struct CmapHeader {
			BE<uint16_t> Version;
			BE<uint16_t> SubtableCount;
		};

		struct EncodingRecord {
			BE<PlatformId> Platform;
			union {
				BE<uint16_t> EncodingId;
				BE<UnicodePlatformEncodingId> UnicodeEncoding;
				BE<MacintoshPlatformEncodingId> MacintoshEncoding;
				BE<IsoPlatformEncodingId> IsoEncoding;
				BE<WindowsPlatformEncodingId> WindowsEncoding;
			};
			BE<uint32_t> SubtableOffset;
		};

		union Format {
			BE<uint16_t> FormatId;

			struct MapGroup {
				BE<uint32_t> StartCharCode;
				BE<uint32_t> EndCharCode;
				BE<uint32_t> GlyphId;

				// Adds mappings for this group, ignoring codepoints past U+10FFFF and glyph IDs that do not fit in result.
				void AddToGlyphToCharMap(std::vector<std::set<char32_t>>& result, bool sequentialGlyphs) const {
					const auto first = static_cast<uint64_t>(*StartCharCode);
					const auto last = (std::min<uint64_t>)(*EndCharCode, 0x10FFFF);
					for (auto c = first; c <= last; c++) {
						const auto glyphId = static_cast<uint64_t>(*GlyphId) + (sequentialGlyphs ? c - first : 0);
						if (glyphId >= result.size())
							break;
						result[static_cast<size_t>(glyphId)].insert(static_cast<char32_t>(c));
					}
				}
			};

			class IFormatView {
			public:
				virtual ~IFormatView() = default;

				virtual void GetGlyphToCharMap(std::vector<std::set<char32_t>>& result) const = 0;

				virtual operator bool() const = 0;
			};

			class ValidButUnsupportedFormatView : public IFormatView {
			public:
				void GetGlyphToCharMap(std::vector<std::set<char32_t>>& result) const override {}
				operator bool() const override { return true; }
			};

			struct Format0 {
				static constexpr uint16_t FormatId_Value = 0;

				struct FormatHeader {
					BE<uint16_t> FormatId;
					BE<uint16_t> Length;
					BE<uint16_t> Language;  // Only used for Macintosh platforms
				};

				FormatHeader Header;
				uint8_t GlyphIdArray[256];

				class View : public IFormatView {
					union {
						const Format0* m_obj;
						const char* m_bytes;
					};
					size_t m_length;

				public:
					View() : m_obj(nullptr), m_length(0) {}
					View(std::nullptr_t) : View() {}
					View(decltype(m_obj) pObject, size_t length)
						: m_obj(pObject), m_length(length) {}
					View(View&&) = default;
					View(const View&) = default;
					View& operator=(View&&) = default;
					View& operator=(const View&) = default;
					View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; return *this; }
					View(const void* pData, size_t length) : View(std::span(static_cast<const char*>(pData), length)) {}
					template<typename T>
					View(std::span<T> data) : View() {
						if (data.size_bytes() < sizeof(FormatHeader))
							return;

						const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

						if (*obj->Header.FormatId != FormatId_Value || data.size_bytes() < *obj->Header.Length)
							return;

						if (data.size_bytes() < sizeof(Format0))
							return;

						m_obj = obj;
						m_length = data.size_bytes();
					}

					operator bool() const override {
						return !!m_obj;
					}

					decltype(m_obj) operator*() const {
						return m_obj;
					}

					decltype(m_obj) operator->() const {
						return m_obj;
					}

					[[nodiscard]] uint16_t CharToGlyph(uint32_t c) const {
						return c >= 256 ? 0 : m_obj->GlyphIdArray[c];
					}

					void GetGlyphToCharMap(std::vector<std::set<char32_t>>& result) const override {
						for (char32_t i = 0; i < 256; i++)
							if (m_obj->GlyphIdArray[i])
								result[m_obj->GlyphIdArray[i]].insert(i);
					}
				};
			};

			struct Format2 {
				static constexpr uint16_t FormatId_Value = 2;

				struct FormatHeader {
					BE<uint16_t> FormatId;
					BE<uint16_t> Length;
					BE<uint16_t> Language;  // Only used for Macintosh platforms
					BE<uint16_t> SubHeaderKeys[256];
				};

				struct SubHeader {
					BE<uint16_t> FirstCode;
					BE<uint16_t> EntryCount;
					BE<int16_t> IdDelta;
					BE<uint16_t> IdRangeOffset;
				};

				FormatHeader Header;
				SubHeader SubHeaders[1];

				class View : public IFormatView {
					union {
						const Format2* m_obj;
						const char* m_bytes;
					};
					size_t m_length;

				public:
					View() : m_obj(nullptr), m_length(0) {}
					View(std::nullptr_t) : View() {}
					View(decltype(m_obj) pObject, size_t length)
						: m_obj(pObject), m_length(length) {}
					View(View&&) = default;
					View(const View&) = default;
					View& operator=(View&&) = default;
					View& operator=(const View&) = default;
					View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; return *this; }
					View(const void* pData, size_t length) : View(std::span(static_cast<const char*>(pData), length)) {}
					template<typename T>
					View(std::span<T> data) : View() {
						if (data.size_bytes() < sizeof(FormatHeader))
							return;

						const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

						if (*obj->Header.FormatId != FormatId_Value || data.size_bytes() < *obj->Header.Length)
							return;

						m_obj = obj;
						m_length = data.size_bytes();
					}

					operator bool() const override {
						return !!m_obj;
					}

					decltype(m_obj) operator*() const {
						return m_obj;
					}

					decltype(m_obj) operator->() const {
						return m_obj;
					}

					[[nodiscard]] uint16_t CharToGlyph(uint32_t c) const {
						if (c >= 0x10000)
							return 0;

						const auto& subHeader = m_obj->SubHeaders[m_obj->Header.SubHeaderKeys[c >> 8] / sizeof(SubHeader)];
						if (reinterpret_cast<const char*>(&subHeader) + sizeof(subHeader) > m_bytes + *m_obj->Header.Length)
							return 0; // overflow

						c = c & 0xFF;
						if (c < *subHeader.FirstCode || c >= static_cast<uint32_t>(*subHeader.FirstCode + *subHeader.EntryCount))
							return 0;

						const auto glyphArray = reinterpret_cast<const BE<uint16_t>*>(reinterpret_cast<const char*>(&subHeader.IdRangeOffset) + sizeof(subHeader).IdRangeOffset + subHeader.IdRangeOffset);
						const auto pGlyphIndex = &glyphArray[c - subHeader.FirstCode];
						if (reinterpret_cast<const char*>(pGlyphIndex) + sizeof(*pGlyphIndex) > m_bytes + *m_obj->Header.Length)
							return 0; // overflow

						c = **pGlyphIndex;
						return c == 0 ? 0 : (c + subHeader.IdDelta) & 0xFFFF;
					}

					void GetGlyphToCharMap(std::vector<std::set<char32_t>>& result) const override {
						for (char32_t baseChar = 0; baseChar < 0x10000; baseChar += 0x100) {
							const auto& subHeader = m_obj->SubHeaders[m_obj->Header.SubHeaderKeys[baseChar >> 8] / sizeof(SubHeader)];
							if (reinterpret_cast<const char*>(&subHeader) + sizeof(subHeader) > m_bytes + *m_obj->Header.Length)
								continue;  // overflow

							const auto glyphArray = reinterpret_cast<const BE<uint16_t>*>(reinterpret_cast<const char*>(&subHeader.IdRangeOffset) + sizeof(subHeader).IdRangeOffset + subHeader.IdRangeOffset);
							for (char32_t c = baseChar + subHeader.FirstCode, c2_ = c + subHeader.EntryCount; c < c2_; c++) {
								const auto pGlyphIndex = &glyphArray[baseChar - subHeader.FirstCode];
								if (reinterpret_cast<const char*>(pGlyphIndex) + sizeof(*pGlyphIndex) > m_bytes + *m_obj->Header.Length)
									continue; // overflow

								const auto glyphIndex = **pGlyphIndex;
								if (glyphIndex)
									result[(baseChar + subHeader.IdDelta) & 0xFFFF].insert(c);
							}
						}
					}
				};
			};

			struct Format4 {
				static constexpr uint16_t FormatId_Value = 4;

				struct FormatHeader {
					BE<uint16_t> FormatId;
					BE<uint16_t> Length;
					BE<uint16_t> Language;  // Only used for Macintosh platforms
					BE<uint16_t> SegCountX2;
					BE<uint16_t> SearchRange;
					BE<uint16_t> EntrySelector;
					BE<uint16_t> RangeShift;

					[[nodiscard]] size_t SegCount() const {
						return *SegCountX2 / 2;
					}
				};

				FormatHeader Header;
				BE<uint16_t> Data[1];

				class View : public IFormatView {
					union {
						const Format4* m_obj;
						const char* m_bytes;
					};
					size_t m_length;

				public:
					View() : m_obj(nullptr), m_length(0) {}
					View(std::nullptr_t) : View() {}
					View(decltype(m_obj) pObject, size_t length)
						: m_obj(pObject), m_length(length) {}
					View(View&&) = default;
					View(const View&) = default;
					View& operator=(View&&) = default;
					View& operator=(const View&) = default;
					View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; return *this; }
					View(const void* pData, size_t length) : View(std::span(static_cast<const char*>(pData), length)) {}
					template<typename T>
					View(std::span<T> data) : View() {
						if (data.size_bytes() < sizeof(FormatHeader))
							return;

						const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

						if (*obj->Header.FormatId != FormatId_Value || data.size_bytes() < *obj->Header.Length)
							return;

						if (sizeof(Header) + 4 + static_cast<size_t>(*obj->Header.SegCountX2) * 3 > data.size_bytes())
							return;

						if (reinterpret_cast<const char*>(&obj->Data[1 + obj->Header.SegCount() * 4]) > reinterpret_cast<const char*>(obj) + data.size_bytes())
							return;

						m_obj = obj;
						m_length = data.size_bytes();
					}

					operator bool() const override {
						return !!m_obj;
					}

					decltype(m_obj) operator*() const {
						return m_obj;
					}

					decltype(m_obj) operator->() const {
						return m_obj;
					}

					[[nodiscard]] const BE<uint16_t>& EndCode(size_t i) const { return m_obj->Data[0 + m_obj->Header.SegCount() * 0 + i]; }
					[[nodiscard]] const BE<uint16_t>& StartCode(size_t i) const { return m_obj->Data[1 + m_obj->Header.SegCount() * 1 + i]; }
					[[nodiscard]] const BE<uint16_t>& IdDelta(size_t i) const { return m_obj->Data[1 + m_obj->Header.SegCount() * 2 + i]; }
					[[nodiscard]] const BE<uint16_t>& IdRangeOffset(size_t i) const { return m_obj->Data[1 + m_obj->Header.SegCount() * 3 + i]; }
					[[nodiscard]] const BE<uint16_t>& GlyphIndex(size_t i) const { return m_obj->Data[1 + m_obj->Header.SegCount() * 4 + i]; }

					[[nodiscard]] std::span<const BE<uint16_t>> EndCodeSpan() const { return { &EndCode(0), m_obj->Header.SegCount() }; }
					[[nodiscard]] std::span<const BE<uint16_t>> StartCodeSpan() const { return { &StartCode(0), m_obj->Header.SegCount() }; }
					[[nodiscard]] std::span<const BE<uint16_t>> IdDeltaSpan() const { return { &IdDelta(0), m_obj->Header.SegCount() }; }
					[[nodiscard]] std::span<const BE<uint16_t>> IdRangeOffsetSpan() const { return { &IdRangeOffset(0), m_obj->Header.SegCount() }; }
					[[nodiscard]] std::span<const BE<uint16_t>> GlyphIndexSpan() const { return { &GlyphIndex(0), static_cast<size_t>((m_bytes + m_length - reinterpret_cast<const char*>(&GlyphIndex(0))) / 2) }; }

					[[nodiscard]] uint16_t CharToGlyph(uint32_t c) const {
						if (c >= 0x10000)
							return 0;

						// First segment whose end is not before c.
						const auto i = std::ranges::lower_bound(EndCodeSpan(), c, {}, [](const BE<uint16_t>& v) { return static_cast<uint32_t>(*v); }) - EndCodeSpan().begin();
						if (i >= static_cast<ptrdiff_t>(EndCodeSpan().size()))
							return 0;

						const auto startCode = *StartCode(i);
						if (c < startCode || c > EndCode(i))
							return 0;

						const auto pIdRangeOffset = &IdRangeOffset(i);
						if (reinterpret_cast<const char*>(pIdRangeOffset) + sizeof(*pIdRangeOffset) > m_bytes + *m_obj->Header.Length)
							return 0; // overflow

						const auto idRangeOffset = **pIdRangeOffset;
						const auto idDelta = *IdDelta(i);
						if (idRangeOffset == 0)
							return (idDelta + c) & 0xFFFF;

						const auto pGlyphIndex = &pIdRangeOffset[idRangeOffset / 2 + c - startCode];
						if (reinterpret_cast<const char*>(pGlyphIndex) + sizeof(*pGlyphIndex) > m_bytes + *m_obj->Header.Length)
							return 0; // overflow

						const auto glyphIndex = **pGlyphIndex;
						return glyphIndex == 0 ? 0 : (idDelta + glyphIndex) & 0xFFFF;
					}

					void GetGlyphToCharMap(std::vector<std::set<char32_t>>& result) const override {
						const auto startCodes = StartCodeSpan();
						const auto endCodes = EndCodeSpan();
						const auto idDeltas = IdDeltaSpan();
						const auto idRangeOffsets = IdRangeOffsetSpan();
						const auto glyphIndices = GlyphIndexSpan();

						for (size_t i = 0, i_ = m_obj->Header.SegCount(); i < i_; i++) {
							const auto startCode = static_cast<char32_t>(*startCodes[i]);
							const auto endCode = static_cast<char32_t>(*endCodes[i]);
							const auto idDelta = static_cast<size_t>(*idDeltas[i]);
							const auto idRangeOffset = static_cast<size_t>(*idRangeOffsets[i]);

							if (idRangeOffset == 0) {
								for (auto c = startCode; c <= endCode; c++) {
									const auto glyphId = (idDelta + c) & 0xFFFF;
									result[glyphId].insert(c);
								}

							} else {
								const auto pIdRangeOffset = &IdRangeOffset(i);

								if (reinterpret_cast<const char*>(pIdRangeOffset) + sizeof(*pIdRangeOffset) > m_bytes + *m_obj->Header.Length)
									continue; // overflow

								for (auto c = startCode; c <= endCode; c++) {
									const auto pGlyphIndex = &pIdRangeOffset[idRangeOffset / 2 + c - startCode];
									if (reinterpret_cast<const char*>(pGlyphIndex) + sizeof(*pGlyphIndex) > m_bytes + *m_obj->Header.Length)
										break; // overflow

									const auto glyphIndex = **pGlyphIndex;
									if (!glyphIndex)
										continue;

									const auto glyphId = (idDelta + glyphIndex) & 0xFFFF;
									result[glyphId].insert(c);
								}
							}
						}
					}
				};
			};

			struct Format6 {
				static constexpr uint16_t FormatId_Value = 6;

				struct FormatHeader {
					BE<uint16_t> FormatId;
					BE<uint16_t> Length;
					BE<uint16_t> Language;  // Only used for Macintosh platforms
					BE<uint16_t> FirstCode;
					BE<uint16_t> EntryCount;
				};

				FormatHeader Header;
				BE<uint16_t> GlyphId[1];

				class View : public IFormatView {
					union {
						const Format6* m_obj;
						const char* m_bytes;
					};
					size_t m_length;

				public:
					View() : m_obj(nullptr), m_length(0) {}
					View(std::nullptr_t) : View() {}
					View(decltype(m_obj) pObject, size_t length)
						: m_obj(pObject), m_length(length) {}
					View(View&&) = default;
					View(const View&) = default;
					View& operator=(View&&) = default;
					View& operator=(const View&) = default;
					View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; return *this; }
					View(const void* pData, size_t length) : View(std::span(static_cast<const char*>(pData), length)) {}
					template<typename T>
					View(std::span<T> data) : View() {
						if (data.size_bytes() < sizeof(FormatHeader))
							return;

						const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

						if (*obj->Header.FormatId != FormatId_Value || data.size_bytes() < *obj->Header.Length)
							return;

						if (reinterpret_cast<const char*>(&obj->GlyphId[*obj->Header.EntryCount]) > reinterpret_cast<const char*>(obj) + data.size_bytes())
							return;

						m_obj = obj;
						m_length = data.size_bytes();
					}

					operator bool() const override {
						return !!m_obj;
					}

					decltype(m_obj) operator*() const {
						return m_obj;
					}

					decltype(m_obj) operator->() const {
						return m_obj;
					}

					[[nodiscard]] std::span<const BE<uint16_t>> GlyphIdSpan() const { return { m_obj->GlyphId, *m_obj->Header.EntryCount }; }

					[[nodiscard]] uint16_t CharToGlyph(uint32_t c) const {
						if (c < *m_obj->Header.FirstCode || c >= static_cast<uint32_t>(*m_obj->Header.FirstCode + *m_obj->Header.EntryCount))
							return 0;

						return m_obj->GlyphId[c - m_obj->Header.FirstCode];
					}

					void GetGlyphToCharMap(std::vector<std::set<char32_t>>& result) const override {
						const auto firstCode = static_cast<char32_t>(*m_obj->Header.FirstCode);
						for (size_t i = 0, i_ = *m_obj->Header.EntryCount; i < i_; i++)
							if (const auto glyphId = *m_obj->GlyphId[i]; glyphId && glyphId < result.size())
								result[glyphId].insert(static_cast<char32_t>(firstCode + i));
					}
				};
			};

			struct Format8 {
				static constexpr uint16_t FormatId_Value = 8;

				struct FormatHeader {
					BE<uint16_t> FormatId;
					BE<uint16_t> Reserved;
					BE<uint32_t> Length;
					BE<uint32_t> Language;  // Only used for Macintosh platforms
					uint8_t Is32[8192];
					BE<uint32_t> GroupCount;
				};

				FormatHeader Header;
				MapGroup Group[1];

				class View : public IFormatView {
					union {
						const Format8* m_obj;
						const char* m_bytes;
					};
					size_t m_length;

				public:
					View() : m_obj(nullptr), m_length(0) {}
					View(std::nullptr_t) : View() {}
					View(decltype(m_obj) pObject, size_t length)
						: m_obj(pObject), m_length(length) {}
					View(View&&) = default;
					View(const View&) = default;
					View& operator=(View&&) = default;
					View& operator=(const View&) = default;
					View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; return *this; }
					View(const void* pData, size_t length) : View(std::span(static_cast<const char*>(pData), length)) {}
					template<typename T>
					View(std::span<T> data) : View() {
						if (data.size_bytes() < sizeof(FormatHeader))
							return;

						const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

						if (*obj->Header.FormatId != FormatId_Value || data.size_bytes() < *obj->Header.Length)
							return;

						if (reinterpret_cast<const char*>(&obj->Group[*obj->Header.GroupCount]) > reinterpret_cast<const char*>(obj) + data.size_bytes())
							return;

						m_obj = obj;
						m_length = data.size_bytes();
					}

					operator bool() const override {
						return !!m_obj;
					}

					decltype(m_obj) operator*() const {
						return m_obj;
					}

					decltype(m_obj) operator->() const {
						return m_obj;
					}

					[[nodiscard]] std::span<const MapGroup> GroupSpan() const { return { const_cast<MapGroup*>(m_obj->Group), static_cast<size_t>(*m_obj->Header.GroupCount) }; }

					[[nodiscard]] uint16_t CharToGlyph(uint32_t c) const {
						// First group whose end is not before c.
						const auto groups = GroupSpan();
						const auto it = std::ranges::lower_bound(groups, c, {}, [](const MapGroup& g) { return static_cast<uint32_t>(*g.EndCharCode); });
						if (it == groups.end() || c < *it->StartCharCode)
							return 0;

						return static_cast<uint16_t>(*it->GlyphId + c - *it->StartCharCode);
					}

					void GetGlyphToCharMap(std::vector<std::set<char32_t>>& result) const override {
						for (const auto& group : GroupSpan())
							group.AddToGlyphToCharMap(result, true);
					}
				};
			};

			struct Format10 {
				static constexpr uint16_t FormatId_Value = 10;

				struct FormatHeader {
					BE<uint16_t> FormatId;
					BE<uint16_t> Reserved;
					BE<uint32_t> Length;
					BE<uint32_t> Language;  // Only used for Macintosh platforms
					BE<uint32_t> FirstCode;
					BE<uint32_t> EntryCount;
				};

				FormatHeader Header;
				BE<uint16_t> GlyphId[1];

				class View : public IFormatView {
					union {
						const Format10* m_obj;
						const char* m_bytes;
					};
					size_t m_length;

				public:
					View() : m_obj(nullptr), m_length(0) {}
					View(std::nullptr_t) : View() {}
					View(decltype(m_obj) pObject, size_t length)
						: m_obj(pObject), m_length(length) {}
					View(View&&) = default;
					View(const View&) = default;
					View& operator=(View&&) = default;
					View& operator=(const View&) = default;
					View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; return *this; }
					View(const void* pData, size_t length) : View(std::span(static_cast<const char*>(pData), length)) {}
					template<typename T>
					View(std::span<T> data) : View() {
						if (data.size_bytes() < sizeof(FormatHeader))
							return;

						const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

						if (*obj->Header.FormatId != FormatId_Value || data.size_bytes() < *obj->Header.Length)
							return;

						if (reinterpret_cast<const char*>(&obj->GlyphId[*obj->Header.EntryCount]) > reinterpret_cast<const char*>(obj) + data.size_bytes())
							return;

						m_obj = obj;
						m_length = data.size_bytes();
					}

					operator bool() const override {
						return !!m_obj;
					}

					decltype(m_obj) operator*() const {
						return m_obj;
					}

					decltype(m_obj) operator->() const {
						return m_obj;
					}

					[[nodiscard]] std::span<const BE<uint16_t>> GlyphIdSpan() const { return { m_obj->GlyphId, *m_obj->Header.EntryCount }; }

					[[nodiscard]] uint16_t CharToGlyph(uint32_t c) const {
						if (c < m_obj->Header.FirstCode || c >= m_obj->Header.FirstCode + m_obj->Header.EntryCount)
							return 0;

						return m_obj->GlyphId[c - m_obj->Header.FirstCode];
					}

					void GetGlyphToCharMap(std::vector<std::set<char32_t>>& result) const override {
						const auto firstCode = static_cast<uint64_t>(*m_obj->Header.FirstCode);
						for (size_t i = 0, i_ = *m_obj->Header.EntryCount; i < i_ && firstCode + i <= 0x10FFFF; i++)
							if (const auto glyphId = *m_obj->GlyphId[i]; glyphId && glyphId < result.size())
								result[glyphId].insert(static_cast<char32_t>(firstCode + i));
					}
				};
			};

			struct Format12And13 {
				static constexpr uint16_t FormatId_Values[]{ 12, 13 };

				struct FormatHeader {
					BE<uint16_t> FormatId;
					BE<uint16_t> Reserved;
					BE<uint32_t> Length;
					BE<uint32_t> Language;  // Only used for Macintosh platforms
					BE<uint32_t> GroupCount;
				};

				FormatHeader Header;
				MapGroup MapGroups[1];

				class View : public IFormatView {
					union {
						const Format12And13* m_obj;
						const char* m_bytes;
					};
					size_t m_length;

				public:
					View() : m_obj(nullptr), m_length(0) {}
					View(std::nullptr_t) : View() {}
					View(decltype(m_obj) pObject, size_t length)
						: m_obj(pObject), m_length(length) {}
					View(View&&) = default;
					View(const View&) = default;
					View& operator=(View&&) = default;
					View& operator=(const View&) = default;
					View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; return *this; }
					View(const void* pData, size_t length) : View(std::span(static_cast<const char*>(pData), length)) {}
					template<typename T>
					View(std::span<T> data) : View() {
						if (data.size_bytes() < sizeof(FormatHeader))
							return;

						const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

						if ((*obj->Header.FormatId != FormatId_Values[0] && *obj->Header.FormatId != FormatId_Values[1]) || data.size_bytes() < *obj->Header.Length)
							return;

						if (reinterpret_cast<const char*>(&obj->MapGroups[*obj->Header.GroupCount]) > reinterpret_cast<const char*>(obj) + data.size_bytes())
							return;

						m_obj = obj;
						m_length = data.size_bytes();
					}

					operator bool() const override {
						return !!m_obj;
					}

					decltype(m_obj) operator*() const {
						return m_obj;
					}

					decltype(m_obj) operator->() const {
						return m_obj;
					}

					[[nodiscard]] std::span<const MapGroup> MapGroupSpan() const { return { const_cast<MapGroup*>(m_obj->MapGroups), *m_obj->Header.GroupCount }; }

					[[nodiscard]] uint16_t CharToGlyph(uint32_t c) const {
						// First group whose end is not before c.
						const auto groups = MapGroupSpan();
						const auto it = std::ranges::lower_bound(groups, c, {}, [](const MapGroup& g) { return static_cast<uint32_t>(*g.EndCharCode); });
						if (it == groups.end() || c < *it->StartCharCode)
							return 0;

						if (*m_obj->Header.FormatId == 12)
							return static_cast<uint16_t>(*it->GlyphId + c - *it->StartCharCode);
						else
							return static_cast<uint16_t>(*it->GlyphId);
					}

					void GetGlyphToCharMap(std::vector<std::set<char32_t>>& result) const override {
						const auto sequentialGlyphs = *m_obj->Header.FormatId == 12;
						for (const auto& group : MapGroupSpan())
							group.AddToGlyphToCharMap(result, sequentialGlyphs);
					}
				};
			};

			static std::unique_ptr<IFormatView> GetFormatView(const void* pData, size_t length) {
				if (length < sizeof(FormatId))
					return nullptr;
				switch (*static_cast<const Format*>(pData)->FormatId) {
					case 0: return TryMakeUniqueFormatView<Format::Format0::View>(pData, length);
					case 2: return TryMakeUniqueFormatView<Format::Format2::View>(pData, length);
					case 4: return TryMakeUniqueFormatView<Format::Format4::View>(pData, length);
					case 6: return TryMakeUniqueFormatView<Format::Format6::View>(pData, length);
					case 8: return TryMakeUniqueFormatView<Format::Format8::View>(pData, length);
					case 10: return TryMakeUniqueFormatView<Format::Format10::View>(pData, length);
					case 12:
					case 13: return TryMakeUniqueFormatView<Format::Format12And13::View>(pData, length);
					default: return std::make_unique<ValidButUnsupportedFormatView>();
				}
			}

			template<typename T>
			static std::unique_ptr<IFormatView> GetFormatView(std::span<T> data) { return GetFormatView(&data[0], data.size_bytes()); }

		private:
			template<typename TFormatView>
			static std::unique_ptr<IFormatView> TryMakeUniqueFormatView(const void* pData, size_t length) {
				std::unique_ptr<IFormatView> p = std::make_unique<TFormatView>(pData, length);
				if (p && *p)
					return p;
				return nullptr;
			}
		};

		CmapHeader Header;
		EncodingRecord EncodingRecords[1];

		class View {
			union {
				const Cmap* m_obj;
				const char* m_bytes;
			};
			size_t m_length;

		public:
			View() : m_obj(nullptr), m_length(0) {}
			View(std::nullptr_t) : View() {}
			View(decltype(m_obj) pObject, size_t length)
				: m_obj(pObject), m_length(length) {}
			View(View&&) = default;
			View(const View&) = default;
			View& operator=(View&&) = default;
			View& operator=(const View&) = default;
			View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; return *this; }
			View(const void* pData, size_t length) : View(std::span(static_cast<const char*>(pData), length)) {}
			template<typename T>
			View(std::span<T> data) : View() {
				if (data.size_bytes() < sizeof(*m_obj))
					return;

				const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

				if (data.size_bytes() < sizeof(OffsetTableStruct) + sizeof(EncodingRecord) * obj->Header.SubtableCount)
					return;

				for (size_t i = 0, i_ = *obj->Header.SubtableCount; i < i_; ++i) {
					const auto& encRec = obj->EncodingRecords[i];
					if (encRec.SubtableOffset + sizeof(uint16_t) > data.size_bytes())
						return;

					const auto pFormatView = Format::GetFormatView(std::span(reinterpret_cast<const char*>(obj), data.size_bytes()).subspan(*encRec.SubtableOffset));
					if (!*pFormatView)
						return;
				}

				m_obj = obj;
				m_length = data.size_bytes();
			}

			operator bool() const {
				return !!m_obj;
			}

			decltype(m_obj) operator*() const {
				return m_obj;
			}

			decltype(m_obj) operator->() const {
				return m_obj;
			}

			[[nodiscard]] std::vector<std::set<char32_t>> GetGlyphToCharMap() const {
				std::vector<std::set<char32_t>> result;
				result.resize(65536);

				for (size_t i = 0, i_ = *m_obj->Header.SubtableCount; i < i_; ++i) {
					const auto& encRec = m_obj->EncodingRecords[i];
					if (false
						|| (encRec.Platform == PlatformId::Unicode)
						|| (encRec.Platform == PlatformId::Windows && encRec.WindowsEncoding == WindowsPlatformEncodingId::UnicodeBmp)
						|| (encRec.Platform == PlatformId::Windows && encRec.WindowsEncoding == WindowsPlatformEncodingId::UnicodeFullRepertoire)) {
						Format::GetFormatView(std::span(m_bytes, m_length).subspan(*encRec.SubtableOffset))->GetGlyphToCharMap(result);
					}
				}

				return result;
			}
		};
	};

	struct Kern {
		// https://docs.microsoft.com/en-us/typography/opentype/spec/kern
		// https://developer.apple.com/fonts/TrueType-Reference-Manual/RM06/Chap6kern.html

		static constexpr TagStruct DirectoryTableTag{ { 'k', 'e', 'r', 'n' } };

		struct Format0 {
			struct Header {
				BE<uint16_t> PairCount;
				BE<uint16_t> SearchRange;
				BE<uint16_t> EntrySelector;
				BE<uint16_t> RangeShift;
			};

			struct Pair {
				BE<uint16_t> Left;
				BE<uint16_t> Right;
				BE<int16_t> Value;
			};

			Header Header;
			Pair Pairs[1];

			class View {
				union {
					const Format0* m_obj;
					const char* m_bytes;
				};
				size_t m_length;

			public:
				View() : m_obj(nullptr), m_length(0) {}
				View(std::nullptr_t) : View() {}
				View(decltype(m_obj) pObject, size_t length)
					: m_obj(pObject), m_length(length) {}
				View(View&&) = default;
				View(const View&) = default;
				View& operator=(View&&) = default;
				View& operator=(const View&) = default;
				View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; return *this; }
				View(const void* pData, size_t length) : View(std::span(static_cast<const char*>(pData), length)) {}
				template<typename T>
				View(std::span<T> data) : View() {
					if (sizeof(Header) > data.size_bytes())
						return;

					const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

					if (reinterpret_cast<const char*>(&obj->Pairs[obj->Header.PairCount]) > reinterpret_cast<const char*>(obj) + data.size_bytes())
						return;

					m_obj = obj;
					m_length = data.size_bytes();
				}

				operator bool() const {
					return !!m_obj;
				}

				decltype(m_obj) operator*() const {
					return m_obj;
				}

				decltype(m_obj) operator->() const {
					return m_obj;
				}

				void Parse(
					std::map<std::pair<char32_t, char32_t>, int>& result,
					const std::vector<std::set<char32_t>>& glyphToCharMap,
					bool cumulative
				) const {
					for (auto pPair = m_obj->Pairs, pPair_ = m_obj->Pairs + m_obj->Header.PairCount; pPair < pPair_; pPair++) {
						if (*pPair->Left >= glyphToCharMap.size() || *pPair->Right >= glyphToCharMap.size())
							continue;
						for (const auto l : glyphToCharMap[*pPair->Left]) {
							for (const auto r : glyphToCharMap[*pPair->Right]) {
								auto& target = result[std::make_pair(l, r)];
								if (cumulative)
									target += *pPair->Value;
								else
									target = *pPair->Value;
							}
						}
					}
				}
			};
		};

		struct Version0 {
			struct KernHeader {
				BE<uint16_t> Version;
				BE<uint16_t> SubtableCount;
			};

			struct CoverageBitpacked {
				uint16_t Horizontal : 1;
				uint16_t Minimum : 1;
				uint16_t CrossStream : 1;
				uint16_t Override : 1;
				uint16_t Reserved1 : 4;
				uint16_t Format : 8;
			};

			struct SubtableHeader {
				BE<uint16_t> Version;
				BE<uint16_t> Length;
				BE<CoverageBitpacked> Coverage;
			};

			KernHeader Header;
			SubtableHeader FirstSubtable;

			class View {
				union {
					const Version0* m_obj;
					const char* m_bytes;
				};
				size_t m_length;

			public:
				View() : m_obj(nullptr), m_length(0) {}
				View(std::nullptr_t) : View() {}
				View(decltype(m_obj) pObject, size_t length)
					: m_obj(pObject), m_length(length) {}
				View(View&&) = default;
				View(const View&) = default;
				View& operator=(View&&) = default;
				View& operator=(const View&) = default;
				View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; return *this; }
				View(const void* pData, size_t length) : View(std::span(static_cast<const char*>(pData), length)) {}
				template<typename T>
				View(std::span<T> data) : View() {
					if (data.size_bytes() < sizeof(KernHeader))
						return;

					const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

					if (obj->Header.Version != 0)
						return;

					m_obj = obj;
					m_length = data.size_bytes();
				}

				operator bool() const {
					return !!m_obj;
				}

				decltype(m_obj) operator*() const {
					return m_obj;
				}

				decltype(m_obj) operator->() const {
					return m_obj;
				}

				void Parse(
					std::map<std::pair<char32_t, char32_t>, int>& result,
					const std::vector<std::set<char32_t>>& glyphToCharMap
				) {
					std::span<const char> data{ reinterpret_cast<const char*>(&m_obj->FirstSubtable), m_length - sizeof(KernHeader) };

					for (size_t i = 0; i < m_obj->Header.SubtableCount; ++i) {
						if (data.size_bytes() < sizeof(SubtableHeader))
							return;  // invalid kern table

						const auto& kernSubtableHeader = *reinterpret_cast<const SubtableHeader*>(data.data());
						if (data.size_bytes() < kernSubtableHeader.Length || kernSubtableHeader.Length < sizeof(kernSubtableHeader))
							return;  // invalid kern table

						const auto coverage = *kernSubtableHeader.Coverage;
						if (kernSubtableHeader.Version == 0 && coverage.Horizontal && !coverage.Minimum && !coverage.CrossStream) {
							const auto formatData = data.subspan(sizeof(kernSubtableHeader), kernSubtableHeader.Length - sizeof(kernSubtableHeader));
							switch (coverage.Format) {
								case 0:
									if (Format0::View view(formatData); view)
										view.Parse(result, glyphToCharMap, !coverage.Override);
									break;
							}
						}

						data = data.subspan(kernSubtableHeader.Length);
					}
				}
			};
		};

		struct Version1 {
			struct KernHeader {
				BE<uint32_t> Version;
				BE<uint32_t> SubtableCount;
			};

			// Apple defines the flags from the most significant bit: 0x8000 vertical, 0x4000 cross-stream, 0x2000 variation; the low byte is the format.
			struct Coverage {
				uint16_t Format : 8;
				uint16_t Reserved1 : 5;
				uint16_t Variation : 1;
				uint16_t CrossStream : 1;
				uint16_t Vertical : 1;
			};

			struct SubtableHeader {
				BE<uint32_t> Length;
				BE<Coverage> Coverage;
				BE<uint16_t> TupleIndex;
			};

			KernHeader Header;
			SubtableHeader FirstSubtable;

			class View {
				union {
					const Version1* m_obj;
					const char* m_bytes;
				};
				size_t m_length;

			public:
				View() : m_obj(nullptr), m_length(0) {}
				View(std::nullptr_t) : View() {}
				View(decltype(m_obj) pObject, size_t length)
					: m_obj(pObject), m_length(length) {}
				View(View&&) = default;
				View(const View&) = default;
				View& operator=(View&&) = default;
				View& operator=(const View&) = default;
				View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; return *this; }
				View(const void* pData, size_t length) : View(std::span(static_cast<const char*>(pData), length)) {}
				template<typename T>
				View(std::span<T> data) : View() {
					if (data.size_bytes() < sizeof(KernHeader))
						return;

					const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

					if (obj->Header.Version != 0x00010000)
						return;

					m_obj = obj;
					m_length = data.size_bytes();
				}

				operator bool() const {
					return !!m_obj;
				}

				decltype(m_obj) operator*() const {
					return m_obj;
				}

				decltype(m_obj) operator->() const {
					return m_obj;
				}

				void Parse(
					std::map<std::pair<char32_t, char32_t>, int>& result,
					const std::vector<std::set<char32_t>>& glyphToCharMap
				) {
					// Untested

					std::span<const char> data{ reinterpret_cast<const char*>(&m_obj->FirstSubtable), m_length - sizeof(KernHeader) };

					for (size_t i = 0; i < m_obj->Header.SubtableCount; ++i) {
						if (data.size_bytes() < sizeof(SubtableHeader))
							return;  // invalid kern table

						const auto& kernSubtableHeader = *reinterpret_cast<const SubtableHeader*>(data.data());
						if (data.size_bytes() < kernSubtableHeader.Length)
							return;  // invalid kern table

						if (kernSubtableHeader.Length < sizeof(kernSubtableHeader))
							return;  // invalid kern table

						const auto coverage = *kernSubtableHeader.Coverage;
						if (!coverage.Vertical && !coverage.CrossStream && !coverage.Variation) {
							const auto formatData = data.subspan(sizeof(kernSubtableHeader), kernSubtableHeader.Length - sizeof(kernSubtableHeader));
							switch (coverage.Format) {
								case 0:
									if (Format0::View view(formatData); view)
										view.Parse(result, glyphToCharMap, true);
									break;

								default:
									// Formats 1 (state table), 2 (class table) and 3 (index array) are not supported.
									break;
							}
						}

						data = data.subspan(kernSubtableHeader.Length);
					}
				}
			};
		};

		union {
			Version0 V0;
			Version1 V1;
		};

		class View {
			union {
				const Kern* m_obj;
				const char* m_bytes;
			};
			size_t m_length;

		public:
			View() : m_obj(nullptr), m_length(0) {}
			View(std::nullptr_t) : View() {}
			View(decltype(m_obj) pObject, size_t length)
				: m_obj(pObject), m_length(length) {}
			View(View&&) = default;
			View(const View&) = default;
			View& operator=(View&&) = default;
			View& operator=(const View&) = default;
			View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; return *this; }
			View(const void* pData, size_t length) : View(std::span(static_cast<const char*>(pData), length)) {}
			template<typename T>
			View(std::span<T> data) : View() {
				if (data.size_bytes() < sizeof(Version0::KernHeader))
					return;

				const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

				m_obj = obj;
				m_length = data.size_bytes();
			}

			operator bool() const {
				return !!m_obj;
			}

			decltype(m_obj) operator*() const {
				return m_obj;
			}

			decltype(m_obj) operator->() const {
				return m_obj;
			}

			[[nodiscard]] std::map<std::pair<char32_t, char32_t>, int> Parse(const std::vector<std::set<char32_t>>& glyphToCharMap) const {
				std::map<std::pair<char32_t, char32_t>, int> result;

				switch (*m_obj->V0.Header.Version) {
					case 0:
						if (Version0::View v(m_bytes, m_length); v)
							v.Parse(result, glyphToCharMap);
						break;

					case 1:
						if (Version1::View v(m_bytes, m_length); v)
							v.Parse(result, glyphToCharMap);
						break;
				}

				return result;
			}
		};
	};

	namespace detail {
		// Reads a big endian value, or returns std::nullopt if it lies outside the data.
		template<typename T>
		std::optional<T> ReadBigEndian(std::span<const char> data, size_t offset) {
			if (offset > data.size() || data.size() - offset < sizeof(T))
				return std::nullopt;
			return **reinterpret_cast<const BE<T>*>(data.data() + offset);
		}
	}

	struct Fvar {
		// https://learn.microsoft.com/en-us/typography/opentype/spec/fvar

		static constexpr TagStruct DirectoryTableTag{ { 'f', 'v', 'a', 'r' } };
		static constexpr TagStruct OpticalSizeAxisTag{ { 'o', 'p', 's', 'z' } };

		struct Axis {
			uint32_t Tag;  // TagStruct::NativeValue
			float Minimum;
			float Default;
			float Maximum;
		};

		// Returns the variation axes of the font, or an empty vector if the font is not a variable font.
		[[nodiscard]] static std::vector<Axis> ReadAxes(std::span<const char> fvar) {
			using detail::ReadBigEndian;

			std::vector<Axis> result;
			const auto majorVersion = ReadBigEndian<uint16_t>(fvar, 0);
			const auto axesArrayOffset = ReadBigEndian<uint16_t>(fvar, 4);
			const auto axisCount = ReadBigEndian<uint16_t>(fvar, 8);
			const auto axisSize = ReadBigEndian<uint16_t>(fvar, 10);
			if (majorVersion != 1 || !axesArrayOffset || !axisCount || !axisSize || *axisSize < 20)
				return result;

			for (size_t i = 0; i < *axisCount; i++) {
				const auto offset = *axesArrayOffset + static_cast<size_t>(*axisSize) * i;
				const auto minimum = ReadBigEndian<int32_t>(fvar, offset + 4);
				const auto defaultValue = ReadBigEndian<int32_t>(fvar, offset + 8);
				const auto maximum = ReadBigEndian<int32_t>(fvar, offset + 12);
				if (!minimum || !defaultValue || !maximum)
					return {};

				uint32_t tag;
				std::memcpy(&tag, fvar.data() + offset, sizeof tag);
				result.emplace_back(Axis{
					.Tag = tag,
					.Minimum = static_cast<float>(*minimum) / 65536.f,
					.Default = static_cast<float>(*defaultValue) / 65536.f,
					.Maximum = static_cast<float>(*maximum) / 65536.f,
				});
			}
			return result;
		}

		// Converts design coordinates to normalized coordinates, applying avar if present.
		// Axes without a given coordinate are at their default.
		[[nodiscard]] static std::vector<float> NormalizeCoordinates(const std::vector<Axis>& axes, const std::map<uint32_t, float>& designCoordinates, std::span<const char> avar) {
			using detail::ReadBigEndian;

			// Normalized coordinates are stored as F2DOT14; quantize as shapers and rasterizers do.
			const auto quantize = [](double v) { return static_cast<float>(std::round(v * 16384.) / 16384.); };

			std::vector<float> result;
			result.reserve(axes.size());
			for (const auto& axis : axes) {
				const auto it = designCoordinates.find(axis.Tag);
				const auto value = std::clamp(it == designCoordinates.end() ? axis.Default : it->second, axis.Minimum, axis.Maximum);
				double normalized = 0;
				if (value < axis.Default && axis.Default > axis.Minimum)
					normalized = (static_cast<double>(value) - axis.Default) / (static_cast<double>(axis.Default) - axis.Minimum);
				else if (value > axis.Default && axis.Maximum > axis.Default)
					normalized = (static_cast<double>(value) - axis.Default) / (static_cast<double>(axis.Maximum) - axis.Default);
				result.push_back(quantize(normalized));
			}

			// avar version 1: SegmentMaps[axisCount] { uint16 positionMapCount; { F2DOT14 fromCoordinate, toCoordinate; }[] }
			if (ReadBigEndian<uint16_t>(avar, 0) == 1 && ReadBigEndian<uint16_t>(avar, 6) == axes.size()) {
				size_t offset = 8;
				for (auto& coordinate : result) {
					const auto count = ReadBigEndian<uint16_t>(avar, offset);
					if (!count)
						break;
					offset += 2;

					std::vector<std::pair<double, double>> map;
					for (size_t i = 0; i < *count; i++, offset += 4) {
						const auto from = ReadBigEndian<int16_t>(avar, offset);
						const auto to = ReadBigEndian<int16_t>(avar, offset + 2);
						if (!from || !to)
							return result;
						map.emplace_back(*from / 16384., *to / 16384.);
					}

					// Piecewise linear interpolation between the mapping points.
					for (size_t i = 1; i < map.size(); i++) {
						if (coordinate <= map[i].first) {
							const auto& [x0, y0] = map[i - 1];
							const auto& [x1, y1] = map[i];
							if (x1 > x0)
								coordinate = quantize(y0 + (y1 - y0) * (coordinate - x0) / (x1 - x0));
							else
								coordinate = static_cast<float>(y1);
							break;
						}
					}
				}
			}

			return result;
		}
	};

	struct ItemVariationStore {
		// https://learn.microsoft.com/en-us/typography/opentype/spec/otvarcommonformats#item-variation-store

		class View {
			std::span<const char> m_data;

		public:
			View() = default;

			explicit View(std::span<const char> data) {
				if (detail::ReadBigEndian<uint16_t>(data, 0) == 1)
					m_data = data;
			}

			explicit operator bool() const {
				return !m_data.empty();
			}

			// Returns the delta of the item for the given normalized coordinates, in font units.
			[[nodiscard]] double GetDelta(uint16_t outerIndex, uint16_t innerIndex, std::span<const float> coordinates) const {
				using detail::ReadBigEndian;

				const auto regionListOffset = ReadBigEndian<uint32_t>(m_data, 2);
				const auto dataCount = ReadBigEndian<uint16_t>(m_data, 6);
				if (!regionListOffset || !dataCount || outerIndex >= *dataCount)
					return 0;

				const auto dataOffset = ReadBigEndian<uint32_t>(m_data, 8 + 4 * static_cast<size_t>(outerIndex));
				if (!dataOffset)
					return 0;

				// ItemVariationData { uint16 itemCount; uint16 wordDeltaCount; uint16 regionIndexCount; uint16 regionIndexes[]; deltaSets[] }
				const auto itemCount = ReadBigEndian<uint16_t>(m_data, *dataOffset);
				const auto wordDeltaCount = ReadBigEndian<uint16_t>(m_data, *dataOffset + 2);
				const auto regionIndexCount = ReadBigEndian<uint16_t>(m_data, *dataOffset + 4);
				if (!itemCount || !wordDeltaCount || !regionIndexCount || innerIndex >= *itemCount)
					return 0;

				const auto longWords = (*wordDeltaCount & 0x8000) != 0;
				const auto wordCount = static_cast<size_t>(*wordDeltaCount & 0x7FFF);
				const size_t wordSize = longWords ? 4 : 2;
				const size_t shortSize = longWords ? 2 : 1;
				const auto rowSize = wordCount * wordSize + (*regionIndexCount - wordCount) * shortSize;
				if (wordCount > *regionIndexCount)
					return 0;

				const auto rowOffset = *dataOffset + 6 + 2 * static_cast<size_t>(*regionIndexCount) + rowSize * innerIndex;
				double delta = 0;
				size_t cellOffset = rowOffset;
				for (size_t i = 0; i < *regionIndexCount; i++) {
					const auto cellSize = i < wordCount ? wordSize : shortSize;
					const auto cell = ReadSignedCell(cellOffset, cellSize);
					cellOffset += cellSize;
					const auto regionIndex = ReadBigEndian<uint16_t>(m_data, *dataOffset + 6 + 2 * i);
					if (!cell || !regionIndex)
						return 0;
					if (*cell)
						delta += *cell * GetRegionScalar(*regionListOffset, *regionIndex, coordinates);
				}
				return delta;
			}

		private:
			[[nodiscard]] std::optional<int32_t> ReadSignedCell(size_t offset, size_t size) const {
				using detail::ReadBigEndian;
				switch (size) {
					case 4:
						return ReadBigEndian<int32_t>(m_data, offset);
					case 2:
						if (const auto v = ReadBigEndian<int16_t>(m_data, offset))
							return *v;
						return std::nullopt;
					case 1:
						if (const auto v = ReadBigEndian<int8_t>(m_data, offset))
							return *v;
						return std::nullopt;
					default:
						return std::nullopt;
				}
			}

			[[nodiscard]] double GetRegionScalar(size_t regionListOffset, size_t regionIndex, std::span<const float> coordinates) const {
				using detail::ReadBigEndian;

				// VariationRegionList { uint16 axisCount; uint16 regionCount; VariationRegion { F2DOT14 start, peak, end; }[axisCount][regionCount] }
				const auto axisCount = ReadBigEndian<uint16_t>(m_data, regionListOffset);
				const auto regionCount = ReadBigEndian<uint16_t>(m_data, regionListOffset + 2);
				if (!axisCount || !regionCount || regionIndex >= *regionCount)
					return 0;

				double scalar = 1;
				for (size_t axis = 0; axis < *axisCount; axis++) {
					const auto offset = regionListOffset + 4 + (regionIndex * *axisCount + axis) * 6;
					const auto startValue = ReadBigEndian<int16_t>(m_data, offset);
					const auto peakValue = ReadBigEndian<int16_t>(m_data, offset + 2);
					const auto endValue = ReadBigEndian<int16_t>(m_data, offset + 4);
					if (!startValue || !peakValue || !endValue)
						return 0;

					const auto start = *startValue / 16384., peak = *peakValue / 16384., end = *endValue / 16384.;
					const auto coordinate = axis < coordinates.size() ? static_cast<double>(coordinates[axis]) : 0.;
					if (start > peak || peak > end)
						continue;
					if (start < 0 && end > 0 && peak != 0)
						continue;
					if (peak == 0 || coordinate == peak)
						continue;
					if (coordinate <= start || coordinate >= end)
						return 0;
					scalar *= coordinate < peak ? (coordinate - start) / (peak - start) : (end - coordinate) / (end - peak);
				}
				return scalar;
			}
		};
	};

	struct Gdef {
		// https://learn.microsoft.com/en-us/typography/opentype/spec/gdef

		static constexpr TagStruct DirectoryTableTag{ { 'G', 'D', 'E', 'F' } };

		// Returns the item variation store of GDEF version 1.3 and later.
		[[nodiscard]] static ItemVariationStore::View GetItemVariationStore(std::span<const char> gdef) {
			using detail::ReadBigEndian;
			if (ReadBigEndian<uint16_t>(gdef, 0) != 1 || ReadBigEndian<uint16_t>(gdef, 2).value_or(0) < 3)
				return {};
			const auto offset = ReadBigEndian<uint32_t>(gdef, 14).value_or(0);
			if (!offset || offset >= gdef.size())
				return {};
			return ItemVariationStore::View(gdef.subspan(offset));
		}
	};

	struct Avar {
		static constexpr TagStruct DirectoryTableTag{ { 'a', 'v', 'a', 'r' } };
	};

	struct Base {
		// https://learn.microsoft.com/en-us/typography/opentype/spec/base

		static constexpr TagStruct DirectoryTableTag{ { 'B', 'A', 'S', 'E' } };

		static constexpr TagStruct RomanBaselineTag{ { 'r', 'o', 'm', 'n' } };
		static constexpr TagStruct IdeographicEmBoxBottomTag{ { 'i', 'd', 'e', 'o' } };
		static constexpr TagStruct IdeographicEmBoxTopTag{ { 'i', 'd', 't', 'p' } };
		static constexpr TagStruct IdeographicFaceBottomTag{ { 'i', 'c', 'f', 'b' } };
		static constexpr TagStruct IdeographicFaceTopTag{ { 'i', 'c', 'f', 't' } };

		// Returns the horizontal baselines of the first of scriptTags that the table lists, or of its first script if none,
		// keyed by baseline tags. Values are in font units, above the origin of the glyphs.
		[[nodiscard]] static std::map<uint32_t, int> ReadHorizontalBaselines(std::span<const char> base, std::span<const uint32_t> scriptTags) {
			using detail::ReadBigEndian;

			std::map<uint32_t, int> result;
			if (ReadBigEndian<uint16_t>(base, 0) != 1)
				return result;

			// Axis { Offset16 baseTagListOffset; Offset16 baseScriptListOffset; }
			const auto axisOffset = static_cast<size_t>(ReadBigEndian<uint16_t>(base, 4).value_or(0));
			if (!axisOffset)
				return result;
			const auto tagListOffset = ReadBigEndian<uint16_t>(base, axisOffset).value_or(0);
			const auto scriptListOffset = ReadBigEndian<uint16_t>(base, axisOffset + 2).value_or(0);
			if (!tagListOffset || !scriptListOffset)
				return result;

			// BaseTagList { uint16 baseTagCount; Tag baselineTags[]; }
			const auto tagList = axisOffset + tagListOffset;
			const auto tagCount = ReadBigEndian<uint16_t>(base, tagList).value_or(0);
			std::vector<uint32_t> baselineTags;
			for (size_t i = 0; i < tagCount; i++) {
				if (tagList + 2 + 4 * i + 4 > base.size())
					return result;
				uint32_t tag;
				std::memcpy(&tag, base.data() + tagList + 2 + 4 * i, sizeof tag);
				baselineTags.push_back(tag);
			}

			// BaseScriptList { uint16 baseScriptCount; { Tag baseScriptTag; Offset16 baseScriptOffset; }[] }
			const auto scriptList = axisOffset + scriptListOffset;
			const auto scriptCount = ReadBigEndian<uint16_t>(base, scriptList).value_or(0);
			std::optional<size_t> scriptOffset;
			for (const auto wanted : scriptTags) {
				for (size_t i = 0; i < scriptCount && !scriptOffset; i++) {
					uint32_t tag;
					if (scriptList + 2 + 6 * i + 6 > base.size())
						return result;
					std::memcpy(&tag, base.data() + scriptList + 2 + 6 * i, sizeof tag);
					if (tag == wanted)
						scriptOffset = scriptList + ReadBigEndian<uint16_t>(base, scriptList + 2 + 6 * i + 4).value_or(0);
				}
				if (scriptOffset)
					break;
			}
			if (!scriptOffset && scriptCount)
				scriptOffset = scriptList + ReadBigEndian<uint16_t>(base, scriptList + 6).value_or(0);
			if (!scriptOffset)
				return result;

			// BaseScript { Offset16 baseValuesOffset; ... }
			// BaseValues { uint16 defaultBaselineIndex; uint16 baseCoordCount; Offset16 baseCoordOffsets[]; }
			// BaseCoord { uint16 format; int16 coordinate; ... }
			const auto valuesOffset = ReadBigEndian<uint16_t>(base, *scriptOffset).value_or(0);
			if (!valuesOffset)
				return result;
			const auto values = *scriptOffset + valuesOffset;
			const auto coordCount = ReadBigEndian<uint16_t>(base, values + 2).value_or(0);
			for (size_t i = 0; i < coordCount && i < baselineTags.size(); i++) {
				const auto coordOffset = ReadBigEndian<uint16_t>(base, values + 4 + 2 * i).value_or(0);
				if (!coordOffset)
					continue;
				if (const auto coordinate = ReadBigEndian<int16_t>(base, values + coordOffset + 2))
					result.emplace(baselineTags[i], *coordinate);
			}
			return result;
		}
	};

	struct Gpos {
		// https://docs.microsoft.com/en-us/typography/opentype/spec/gpos

		static constexpr TagStruct DirectoryTableTag{ { 'G', 'P', 'O', 'S' } };
		static constexpr TagStruct KerningFeatureTag{ { 'k', 'e', 'r', 'n' } };

		struct GposHeaderV1_0 {
			Fixed Version;
			BE<uint16_t> ScriptListOffset;
			BE<uint16_t> FeatureListOffset;
			BE<uint16_t> LookupListOffset;
		};

		struct GposHeaderV1_1 : GposHeaderV1_0 {
			BE<uint32_t> FeatureVariationsOffset;
		};

		union ValueFormatFlags {
			uint16_t Value;
			struct {
				uint16_t PlacementX : 1;
				uint16_t PlacementY : 1;
				uint16_t AdvanceX : 1;
				uint16_t AdvanceY : 1;
				uint16_t PlaDeviceOffsetX : 1;
				uint16_t PlaDeviceOffsetY : 1;
				uint16_t AdvDeviceOffsetX : 1;
				uint16_t AdvDeviceOffsetY : 1;
				uint16_t Reserved : 8;
			};
		};
		static_assert(sizeof(ValueFormatFlags) == 2);

		union PairAdjustmentPositioningSubtable {
			struct Format1 {
				struct FormatHeader {
					BE<uint16_t> FormatId;
					BE<uint16_t> CoverageOffset;
					BE<ValueFormatFlags> ValueFormat1;
					BE<ValueFormatFlags> ValueFormat2;
					BE<uint16_t> PairSetCount;
				};

				struct PairSet {
					BE<uint16_t> Count;
					BE<uint16_t> Records[1];

					class View {
						union {
							const PairSet* m_obj;
							const char* m_bytes;
						};
						size_t m_length;
						ValueFormatFlags m_format1;
						ValueFormatFlags m_format2;
						uint32_t m_bit;
						size_t m_valueCountPerPairValueRecord;

					public:
						View() : m_obj(nullptr), m_length(0), m_format1{ 0 }, m_format2{ 0 }, m_bit(0), m_valueCountPerPairValueRecord(0) {}
						View(std::nullptr_t) : View() {}
						View(decltype(m_obj) pObject, size_t length, ValueFormatFlags format1, ValueFormatFlags format2, uint32_t bit, size_t valueCountPerPairValueRecord)
							: m_obj(pObject), m_length(length), m_format1(format1), m_format2(format2), m_bit(bit), m_valueCountPerPairValueRecord(valueCountPerPairValueRecord) {}
						View(View&&) = default;
						View(const View&) = default;
						View& operator=(View&&) = default;
						View& operator=(const View&) = default;
						View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; return *this; }
						View(const void* pData, size_t length, ValueFormatFlags format1, ValueFormatFlags format2) : View(std::span(static_cast<const char*>(pData), length), format1, format2) {}
						template<typename T>
						View(std::span<T> data, ValueFormatFlags format1, ValueFormatFlags format2) : View() {
							if (data.size_bytes() < 2)
								return;

							const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

							const auto bit = (format2.Value << 16) | format1.Value;
							const auto valueCountPerPairValueRecord = static_cast<size_t>(1) + std::popcount<uint32_t>(bit);

							if (data.size_bytes() < static_cast<size_t>(2) + 2 * valueCountPerPairValueRecord * (*obj->Count))
								return;

							m_obj = obj;
							m_length = data.size_bytes();
							m_format1 = format1;
							m_format2 = format2;
							m_bit = bit;
							m_valueCountPerPairValueRecord = valueCountPerPairValueRecord;
						}

						operator bool() const {
							return !!m_obj;
						}

						decltype(m_obj) operator*() const {
							return m_obj;
						}

						decltype(m_obj) operator->() const {
							return m_obj;
						}

						[[nodiscard]] const BE<uint16_t>* GetPairValueRecord(size_t index) const {
							return &m_obj->Records[m_valueCountPerPairValueRecord * index];
						}

						[[nodiscard]] uint16_t GetSecondGlyph(size_t index) const {
							return **GetPairValueRecord(index);
						}

						[[nodiscard]] uint16_t GetValueRecord1(size_t index, ValueFormatFlags desiredRecord) const {
							if (!(m_format1.Value & desiredRecord.Value))
								return 0;
							auto bit = m_bit;
							auto pRecord = GetPairValueRecord(index);
							for (auto i = static_cast<uint32_t>(desiredRecord.Value); i && bit; i >>= 1, bit >>= 1) {
								if (bit & 1)
									pRecord++;
							}
							return *pRecord;
						}

						[[nodiscard]] uint16_t GetValueRecord2(size_t index, ValueFormatFlags desiredRecord) const {
							if (!(m_format2.Value & desiredRecord.Value))
								return 0;
							auto bit = m_bit;
							auto pRecord = GetPairValueRecord(index);
							for (auto i = static_cast<uint32_t>(desiredRecord.Value) << 16; i && bit; i >>= 1, bit >>= 1) {
								if (bit & 1)
									pRecord++;
							}
							return *pRecord;
						}
					};
				};

				FormatHeader Header;
				BE<uint16_t> PairSetOffsets[1];

				class View {
					union {
						const Format1* m_obj;
						const char* m_bytes;
					};
					size_t m_length;

				public:
					View() : m_obj(nullptr), m_length(0) {}
					View(std::nullptr_t) : View() {}
					View(decltype(m_obj) pObject, size_t length)
						: m_obj(pObject), m_length(length) {}
					View(View&&) = default;
					View(const View&) = default;
					View& operator=(View&&) = default;
					View& operator=(const View&) = default;
					View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; return *this; }
					View(const void* pData, size_t length) : View(std::span(static_cast<const char*>(pData), length)) {}
					template<typename T>
					View(std::span<T> data) : View() {
						if (data.size_bytes() < sizeof(FormatHeader))
							return;

						const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

						if (obj->Header.FormatId != 1)
							return;

						if (data.size_bytes() < sizeof(FormatHeader) + static_cast<size_t>(2) * (*obj->Header.PairSetCount))
							return;

						if (*obj->Header.CoverageOffset >= data.size_bytes())
							return;

						if (CoverageTable::View coverageTable(reinterpret_cast<const char*>(obj) + *obj->Header.CoverageOffset, data.size_bytes() - *obj->Header.CoverageOffset); !coverageTable)
							return;

						for (size_t i = 0, i_ = *obj->Header.PairSetCount; i < i_; i++) {
							const auto off = static_cast<size_t>(*obj->PairSetOffsets[i]);
							if (data.size_bytes() < off + 2)
								return;

							const auto pPairSet = reinterpret_cast<const PairSet*>(reinterpret_cast<const char*>(obj) + off);
							if (data.size_bytes() < off + 2 + static_cast<size_t>(2) * (*pPairSet->Count))
								return;
						}

						m_obj = obj;
						m_length = data.size_bytes();
					}

					operator bool() const {
						return !!m_obj;
					}

					decltype(m_obj) operator*() const {
						return m_obj;
					}

					decltype(m_obj) operator->() const {
						return m_obj;
					}

					[[nodiscard]] std::span<const BE<uint16_t>> PairSetOffsetSpan() const {
						return { m_obj->PairSetOffsets, m_obj->Header.PairSetCount };
					}

					[[nodiscard]] PairSet::View PairSetView(size_t index) const {
						const auto offset = static_cast<size_t>(*m_obj->PairSetOffsets[index]);
						return { reinterpret_cast<const char*>(m_obj) + offset, m_length - offset, m_obj->Header.ValueFormat1, m_obj->Header.ValueFormat2 };
					}

					[[nodiscard]] CoverageTable::View CoverageTableView() const {
						const auto offset = static_cast<size_t>(*m_obj->Header.CoverageOffset);
						return { reinterpret_cast<const char*>(m_obj) + offset, m_length - offset };
					}
				};
			};

			struct Format2 {
				struct FormatHeader {
					BE<uint16_t> FormatId;
					BE<uint16_t> CoverageOffset;
					BE<ValueFormatFlags> ValueFormat1;
					BE<ValueFormatFlags> ValueFormat2;
					BE<uint16_t> ClassDef1Offset;
					BE<uint16_t> ClassDef2Offset;
					BE<uint16_t> Class1Count;
					BE<uint16_t> Class2Count;
				};

				// Note:
				// ClassRecord1 { Class2Record[Class2Count]; }
				// ClassRecord2 { ValueFormat1; ValueFormat2; }

				FormatHeader Header;
				BE<uint16_t> Records[1];

				class View {
					union {
						const Format2* m_obj;
						const char* m_bytes;
					};
					size_t m_length;
					uint32_t m_bit;
					size_t m_valueCountPerPairValueRecord;

				public:
					View() : m_obj(nullptr), m_length(0), m_bit(0), m_valueCountPerPairValueRecord(0) {}
					View(std::nullptr_t) : View() {}
					View(decltype(m_obj) pObject, size_t length, uint32_t bit, size_t valueCountPerPairValueRecord)
						: m_obj(pObject), m_length(length), m_bit(bit), m_valueCountPerPairValueRecord(valueCountPerPairValueRecord) {}
					View(View&&) = default;
					View(const View&) = default;
					View& operator=(View&&) = default;
					View& operator=(const View&) = default;
					View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; return *this; }
					View(const void* pData, size_t length) : View(std::span(static_cast<const char*>(pData), length)) {}
					template<typename T>
					View(std::span<T> data) : View() {
						if (data.size_bytes() < sizeof(FormatHeader))
							return;

						const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

						if (obj->Header.FormatId != 2)
							return;

						const auto bit = (static_cast<uint32_t>((*obj->Header.ValueFormat2).Value) << 16) | (*obj->Header.ValueFormat1).Value;
						const auto valueCountPerPairValueRecord = static_cast<size_t>(std::popcount<uint32_t>(bit));

						if (data.size_bytes() < sizeof(FormatHeader) + sizeof(BE<uint16_t>) * valueCountPerPairValueRecord * (*obj->Header.Class1Count) * (*obj->Header.Class2Count))
							return;

						for (const size_t offset : {*obj->Header.CoverageOffset, *obj->Header.ClassDef1Offset, *obj->Header.ClassDef2Offset}) {
							if (offset >= data.size_bytes())
								return;
						}

						if (CoverageTable::View v(reinterpret_cast<const char*>(obj) + *obj->Header.CoverageOffset, data.size_bytes() - *obj->Header.CoverageOffset); !v)
							return;
						if (ClassDefTable::View v(reinterpret_cast<const char*>(obj) + *obj->Header.ClassDef1Offset, data.size_bytes() - *obj->Header.ClassDef1Offset); !v)
							return;
						if (ClassDefTable::View v(reinterpret_cast<const char*>(obj) + *obj->Header.ClassDef2Offset, data.size_bytes() - *obj->Header.ClassDef2Offset); !v)
							return;

						m_obj = obj;
						m_length = data.size_bytes();
						m_bit = bit;
						m_valueCountPerPairValueRecord = valueCountPerPairValueRecord;
					}

					operator bool() const {
						return !!m_obj;
					}

					decltype(m_obj) operator*() const {
						return m_obj;
					}

					decltype(m_obj) operator->() const {
						return m_obj;
					}

					[[nodiscard]] const BE<uint16_t>* GetPairValueRecord(size_t class1, size_t class2) const {
						return &m_obj->Records[m_valueCountPerPairValueRecord * (class1 * *m_obj->Header.Class2Count + class2)];
					}

					// Unlike PairValueRecord of Format1, Class2Record does not begin with a glyph ID;
					// the requested value is preceded only by the values of lower bits that are set.
					// desiredRecord must have exactly one bit set.
					[[nodiscard]] uint16_t GetValueRecord1(size_t class1, size_t class2, ValueFormatFlags desiredRecord) const {
						const auto desired = static_cast<uint32_t>(desiredRecord.Value);
						if (!(m_bit & desired))
							return 0;
						return *GetPairValueRecord(class1, class2)[std::popcount(m_bit & (desired - 1))];
					}

					[[nodiscard]] uint16_t GetValueRecord2(size_t class1, size_t class2, ValueFormatFlags desiredRecord) const {
						const auto desired = static_cast<uint32_t>(desiredRecord.Value) << 16;
						if (!(m_bit & desired))
							return 0;
						return *GetPairValueRecord(class1, class2)[std::popcount(m_bit & (desired - 1))];
					}

					[[nodiscard]] CoverageTable::View CoverageTableView() const {
						const auto offset = static_cast<size_t>(*m_obj->Header.CoverageOffset);
						return { m_bytes + offset, m_length - offset };
					}

					[[nodiscard]] ClassDefTable::View GetClassTableDefinition1() const {
						return { m_bytes + *m_obj->Header.ClassDef1Offset, m_length - *m_obj->Header.ClassDef1Offset };
					}

					[[nodiscard]] ClassDefTable::View GetClassTableDefinition2() const {
						return { m_bytes + *m_obj->Header.ClassDef2Offset, m_length - *m_obj->Header.ClassDef2Offset };
					}
				};
			};
		};

		union ExtensionPositioningSubtable {
			struct Format1 {
				BE<uint16_t> PosFormat;
				BE<LookupType> ExtensionLookupType;
				BE<uint32_t> ExtensionOffset;
			};
		};

		union {
			Fixed Version;
			GposHeaderV1_0 HeaderV1_1;
			GposHeaderV1_1 HeaderV1_0;
		};

		class View {
			union {
				const Gpos* m_obj;
				const char* m_bytes;
			};
			size_t m_length;

		public:
			View() : m_obj(nullptr), m_length(0) {}
			View(std::nullptr_t) : View() {}
			View(decltype(m_obj) pObject, size_t length)
				: m_obj(pObject), m_length(length) {}
			View(View&&) = default;
			View(const View&) = default;
			View& operator=(View&&) = default;
			View& operator=(const View&) = default;
			View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; return *this; }
			View(const void* pData, size_t length) : View(std::span(static_cast<const char*>(pData), length)) {}
			template<typename T>
			View(std::span<T> data) : View() {
				if (data.size_bytes() < sizeof(GposHeaderV1_0))
					return;

				const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

				if (obj->Version.Major < 1)
					return;

				if (obj->Version.Major > 1 || (obj->Version.Major == 1 && obj->Version.Minor >= 1)) {
					if (data.size_bytes() < sizeof(GposHeaderV1_1))
						return;
				}

				m_obj = obj;
				m_length = data.size_bytes();
			}

			operator bool() const {
				return !!m_obj;
			}

			decltype(m_obj) operator*() const {
				return m_obj;
			}

			decltype(m_obj) operator->() const {
				return m_obj;
			}

			// What Device and VariationIndex tables of ValueRecords are evaluated against.
			struct ValueContext {
				// Item variation store of GDEF, and the normalized coordinates of the instance, for variable fonts.
				ItemVariationStore::View VariationStore;
				std::span<const float> NormalizedCoordinates;

				// Horizontal and vertical pixels per em, for Device tables; 0 to ignore them.
				unsigned PpemX = 0;
				unsigned PpemY = 0;

				// Font units per em, to convert the pixel adjustments of Device tables into font units.
				unsigned UnitsPerEm = 0;
			};
			// Returns the type of the lookup, resolving extension lookups, or 0 if the lookup is invalid.
			[[nodiscard]] LookupType GetLookupType(uint16_t lookupIndex) const {
				const auto lookupListOffset = static_cast<size_t>(*m_obj->HeaderV1_0.LookupListOffset);
				if (!lookupListOffset || lookupListOffset >= m_length)
					return static_cast<LookupType>(0);

				LookupList::View lookupList(m_bytes + lookupListOffset, m_length - lookupListOffset);
				if (!lookupList || lookupIndex >= lookupList.Offsets().size())
					return static_cast<LookupType>(0);

				const auto offset = lookupListOffset + *lookupList.Offsets()[lookupIndex];
				if (offset >= m_length)
					return static_cast<LookupType>(0);

				LookupTable::View lookupTable(m_bytes + offset, m_length - offset);
				if (!lookupTable)
					return static_cast<LookupType>(0);

				const auto lookupType = *lookupTable->Header.LookupType;
				if (lookupType != LookupType::ExtensionPositioning || !*lookupTable->Header.SubtableCount)
					return lookupType;

				const auto subtableSpan = lookupTable.SubtableSpan(0);
				if (subtableSpan.size() < sizeof(ExtensionPositioningSubtable::Format1))
					return static_cast<LookupType>(0);
				return *reinterpret_cast<const ExtensionPositioningSubtable::Format1*>(subtableSpan.data())->ExtensionLookupType;
			}

			// Returns whether the font has a 'kern' feature with lookups.
			[[nodiscard]] bool HasKerningFeature() const {
				const auto featureList = GetFeatureListView();
				if (!featureList)
					return false;

				const auto records = featureList.Records();
				for (size_t i = 0; i < records.size(); i++) {
					if (records[i].FeatureTag.NativeValue == KerningFeatureTag.NativeValue && !featureList.LookupIndices(i).empty())
						return true;
				}
				return false;
			}

			// Returns the indices into LookupList of the lookups of the given features. Tags are in TagStruct::NativeValue form.
			// The script is the first of scriptTags that the font has, falling back to DFLT, dflt, and latn as shapers do.
			// The language system is the first of languageTags that the script has, falling back to its default language system.
			// If the font has no script list, every matching feature is used.
			// For variable fonts, feature tables are substituted as FeatureVariations specifies for the normalized coordinates.
			[[nodiscard]] std::set<uint16_t> GetFeatureLookupIndices(const std::set<uint32_t>& featureTags, std::span<const uint32_t> scriptTags, std::span<const uint32_t> languageTags, std::span<const float> normalizedCoordinates = {}) const {
				static constexpr TagStruct FallbackScriptTags[]{ { { 'D', 'F', 'L', 'T' } }, { { 'd', 'f', 'l', 't' } }, { { 'l', 'a', 't', 'n' } } };

				std::set<uint16_t> result;

				const auto featureList = GetFeatureListView();
				if (!featureList)
					return result;

				const auto features = featureList.Records();
				const auto substitutions = GetFeatureSubstitutions(normalizedCoordinates);
				const auto addFeature = [&](size_t featureIndex) {
					if (featureIndex >= features.size() || !featureTags.contains(features[featureIndex].FeatureTag.NativeValue))
						return;

					if (const auto it = substitutions.find(static_cast<uint16_t>(featureIndex)); it != substitutions.end()) {
						for (const auto lookupIndex : it->second)
							result.insert(lookupIndex);
						return;
					}

					for (const auto& lookupIndex : featureList.LookupIndices(featureIndex))
						result.insert(*lookupIndex);
				};

				const auto readUInt16 = [this](size_t offset) -> std::optional<uint16_t> {
					if (offset + sizeof(uint16_t) > m_length)
						return std::nullopt;
					return *reinterpret_cast<const BE<uint16_t>*>(m_bytes + offset);
				};
				const auto readTag = [this](size_t offset) -> std::optional<uint32_t> {
					if (offset + sizeof(uint32_t) > m_length)
						return std::nullopt;
					uint32_t tag;
					std::memcpy(&tag, m_bytes + offset, sizeof tag);
					return tag;
				};

				// ScriptList { uint16 scriptCount; ScriptRecord { Tag scriptTag; Offset16 scriptOffset; } scriptRecords[]; }
				// Script { Offset16 defaultLangSysOffset; uint16 langSysCount; LangSysRecord { Tag langSysTag; Offset16 langSysOffset; } langSysRecords[]; }
				// LangSys { Offset16 lookupOrderOffset; uint16 requiredFeatureIndex; uint16 featureIndexCount; uint16 featureIndices[]; }
				const auto scriptListOffset = static_cast<size_t>(*m_obj->HeaderV1_0.ScriptListOffset);
				const auto scriptCount = scriptListOffset ? readUInt16(scriptListOffset).value_or(0) : 0;
				if (!scriptCount) {
					for (size_t i = 0; i < features.size(); i++)
						addFeature(i);
					return result;
				}

				// Finds the offset to the table of the record with the given tag, among (count) records of 6 bytes each.
				const auto findRecord = [&](size_t tableOffset, size_t recordsOffset, size_t count, uint32_t tag) -> size_t {
					for (size_t i = 0; i < count; i++) {
						const auto recordTag = readTag(recordsOffset + 6 * i);
						const auto recordOffset = readUInt16(recordsOffset + 6 * i + 4);
						if (!recordTag || !recordOffset)
							return 0;
						if (*recordTag == tag)
							return tableOffset + *recordOffset;
					}
					return 0;
				};

				size_t scriptTableOffset = 0;
				for (const auto tag : scriptTags) {
					if ((scriptTableOffset = findRecord(scriptListOffset, scriptListOffset + 2, scriptCount, tag)))
						break;
				}
				for (const auto& tag : FallbackScriptTags) {
					if (scriptTableOffset)
						break;
					scriptTableOffset = findRecord(scriptListOffset, scriptListOffset + 2, scriptCount, tag.NativeValue);
				}
				if (!scriptTableOffset)
					return result;

				size_t langSysOffset = 0;
				const auto langSysCount = readUInt16(scriptTableOffset + 2).value_or(0);
				for (const auto tag : languageTags) {
					if ((langSysOffset = findRecord(scriptTableOffset, scriptTableOffset + 4, langSysCount, tag)))
						break;
				}
				if (!langSysOffset) {
					const auto defaultLangSysOffset = readUInt16(scriptTableOffset).value_or(0);
					if (!defaultLangSysOffset)
						return result;
					langSysOffset = scriptTableOffset + defaultLangSysOffset;
				}

				const auto requiredFeatureIndex = readUInt16(langSysOffset + 2);
				const auto featureIndexCount = readUInt16(langSysOffset + 4);
				if (!requiredFeatureIndex || !featureIndexCount)
					return result;

				if (*requiredFeatureIndex != 0xFFFF)
					addFeature(*requiredFeatureIndex);
				for (size_t j = 0; j < *featureIndexCount; j++) {
					if (const auto featureIndex = readUInt16(langSysOffset + 6 + 2 * j))
						addFeature(*featureIndex);
				}

				return result;
			}

			// Extracts horizontal pair kerning from the given lookups, in font units:
			// XAdvance of the first glyph plus XPlacement of the second glyph.
			// Lookups are applied in LookupList order and their values add up.
			// Within a lookup, the first subtable that applies to a pair wins, even if its value is zero.
			[[nodiscard]] std::map<std::pair<char32_t, char32_t>, double> ExtractAdvanceX(const std::vector<std::set<char32_t>>& glyphToCharMap, const std::set<uint16_t>& lookupIndices, const ValueContext& context = {}) const {
				const auto lookupListOffset = static_cast<size_t>(*m_obj->HeaderV1_0.LookupListOffset);
				if (!lookupListOffset || lookupListOffset >= m_length)
					return {};

				LookupList::View lookupList(m_bytes + lookupListOffset, m_length - lookupListOffset);
				if (!lookupList)
					return {};

				// Only glyphs that are mapped from a codepoint can take part in the result.
				std::vector<uint16_t> mappedGlyphs;
				for (size_t i = 0, i_ = (std::min<size_t>)(glyphToCharMap.size(), 65536); i < i_; i++) {
					if (!glyphToCharMap[i].empty())
						mappedGlyphs.push_back(static_cast<uint16_t>(i));
				}

				// Key is (left glyph << 16) | right glyph.
				std::unordered_map<uint32_t, double> glyphPairs;

				const auto lookupOffsets = lookupList.Offsets();
				for (const auto lookupIndex : lookupIndices) {
					if (lookupIndex >= lookupOffsets.size())
						continue;

					const auto offset = lookupListOffset + *lookupOffsets[lookupIndex];
					if (offset >= m_length)
						continue;

					if (LookupTable::View lookupTable(m_bytes + offset, m_length - offset); lookupTable)
						ApplyPairAdjustmentLookup(lookupTable, glyphToCharMap, mappedGlyphs, glyphPairs, context);
				}

				std::map<std::pair<char32_t, char32_t>, double> result;
				for (const auto& [key, value] : glyphPairs) {
					if (value == 0)
						continue;

					for (const auto c1 : glyphToCharMap[key >> 16])
						for (const auto c2 : glyphToCharMap[key & 0xFFFF])
							result[std::make_pair(c1, c2)] = value;
				}

				return result;
			}


			// Reads a field of a ValueRecord, adding the adjustment of its Device or VariationIndex table if any, in font units.
			// subtable is the table that device offsets are relative to: the PairSet table for PairPosFormat1, and the subtable otherwise.
			// desiredRecord must be one of PlacementX, PlacementY, AdvanceX, or AdvanceY.
			[[nodiscard]] static double ReadValueRecordField(std::span<const char> subtable, const char* pRecord, uint16_t valueFormat, ValueFormatFlags desiredRecord, const ValueContext& context) {
				const auto bit = static_cast<uint32_t>(desiredRecord.Value);

				double value = 0;
				if (valueFormat & bit) {
					const auto fieldOffset = sizeof(uint16_t) * std::popcount(valueFormat & (bit - 1));
					value = **reinterpret_cast<const BE<int16_t>*>(pRecord + fieldOffset);
				}

				// Device offsets follow the four value fields, in the same order. A device offset may be present without its value field.
				if (const auto deviceBit = bit << 4; valueFormat & deviceBit) {
					const auto deviceFieldOffset = sizeof(uint16_t) * std::popcount(valueFormat & (deviceBit - 1));
					if (const auto deviceOffset = static_cast<size_t>(**reinterpret_cast<const BE<uint16_t>*>(pRecord + deviceFieldOffset))) {
						const auto isVertical = desiredRecord.PlacementY || desiredRecord.AdvanceY;
						value += EvaluateDeviceTable(subtable, deviceOffset, context, isVertical ? context.PpemY : context.PpemX);
					}
				}
				return value;
			}

			// Evaluates a Device or VariationIndex table, in font units.
			[[nodiscard]] static double EvaluateDeviceTable(std::span<const char> subtable, size_t offset, const ValueContext& context, unsigned ppem) {
				using detail::ReadBigEndian;

				// Device { uint16 startSize; uint16 endSize; uint16 deltaFormat; ... }
				// VariationIndex { uint16 deltaSetOuterIndex; uint16 deltaSetInnerIndex; uint16 deltaFormat = 0x8000; }
				const auto first = ReadBigEndian<uint16_t>(subtable, offset);
				const auto second = ReadBigEndian<uint16_t>(subtable, offset + 2);
				const auto deltaFormat = ReadBigEndian<uint16_t>(subtable, offset + 4);
				if (!first || !second || !deltaFormat)
					return 0;

				if (*deltaFormat == 0x8000) {
					if (!context.VariationStore)
						return 0;
					return context.VariationStore.GetDelta(*first, *second, context.NormalizedCoordinates);
				}

				// Device formats 1, 2, and 3 pack signed pixel adjustments of 2, 4, and 8 bits, for sizes startSize to endSize.
				const auto startSize = *first, endSize = *second;
				if (*deltaFormat < 1 || *deltaFormat > 3 || !ppem || !context.UnitsPerEm || ppem < startSize || ppem > endSize)
					return 0;

				const auto bits = 1u << *deltaFormat;
				const auto valuesPerWord = 16 / bits;
				const auto index = ppem - startSize;
				const auto word = ReadBigEndian<uint16_t>(subtable, offset + 6 + 2 * static_cast<size_t>(index / valuesPerWord));
				if (!word)
					return 0;

				const auto shift = 16 - bits * (index % valuesPerWord + 1);
				auto pixels = static_cast<int>((*word >> shift) & ((1u << bits) - 1));
				if (pixels >= static_cast<int>(1u << (bits - 1)))
					pixels -= static_cast<int>(1u << bits);
				return static_cast<double>(pixels) * context.UnitsPerEm / ppem;
			}

			struct SingleAdjustment {
				double PlacementX = 0;
				double PlacementY = 0;
				double AdvanceX = 0;
			};

			// Extracts single glyph adjustments (lookup type 1) from the given lookups for the given glyphs, in font units.
			// Lookups are applied in LookupList order and their values add up.
			// Within a lookup, the first subtable that covers a glyph applies to it.
			[[nodiscard]] std::map<uint16_t, SingleAdjustment> ExtractSingleAdjustments(const std::set<uint16_t>& lookupIndices, const std::vector<uint16_t>& glyphs, const ValueContext& context = {}) const {
				std::map<uint16_t, SingleAdjustment> result;

				const auto lookupListOffset = static_cast<size_t>(*m_obj->HeaderV1_0.LookupListOffset);
				if (!lookupListOffset || lookupListOffset >= m_length)
					return result;

				LookupList::View lookupList(m_bytes + lookupListOffset, m_length - lookupListOffset);
				if (!lookupList)
					return result;

				const auto lookupOffsets = lookupList.Offsets();
				for (const auto lookupIndex : lookupIndices) {
					if (lookupIndex >= lookupOffsets.size())
						continue;

					const auto offset = lookupListOffset + *lookupOffsets[lookupIndex];
					if (offset >= m_length)
						continue;

					LookupTable::View lookupTable(m_bytes + offset, m_length - offset);
					if (!lookupTable)
						continue;

					std::vector<SinglePositioningSubtable> subtables;
					for (size_t subtableIndex = 0, i_ = *lookupTable->Header.SubtableCount; subtableIndex < i_; subtableIndex++) {
						auto subtableSpan = lookupTable.SubtableSpan(subtableIndex);
						if (!ResolveExtension(*lookupTable->Header.LookupType, LookupType::SingleAdjustment, subtableSpan))
							continue;
						if (SinglePositioningSubtable subtable(subtableSpan); subtable)
							subtables.emplace_back(subtable);
					}

					for (const auto glyph : glyphs) {
						for (const auto& subtable : subtables) {
							const auto pRecord = subtable.GetValueRecord(glyph);
							if (!pRecord)
								continue;

							const auto valueX = subtable.ReadValue(pRecord, ValueFormatFlags{ .PlacementX = 1 }, context);
							const auto valueY = subtable.ReadValue(pRecord, ValueFormatFlags{ .PlacementY = 1 }, context);
							const auto advanceX = subtable.ReadValue(pRecord, ValueFormatFlags{ .AdvanceX = 1 }, context);
							if (valueX != 0 || valueY != 0 || advanceX != 0) {
								auto& target = result[glyph];
								target.PlacementX += valueX;
								target.PlacementY += valueY;
								target.AdvanceX += advanceX;
							}
							break;
						}
					}
				}

				return result;
			}

		private:
			// Returns the lookup indices of the feature tables that FeatureVariations substitutes for the normalized coordinates, keyed by feature index.
			[[nodiscard]] std::map<uint16_t, std::vector<uint16_t>> GetFeatureSubstitutions(std::span<const float> normalizedCoordinates) const {
				using detail::ReadBigEndian;

				std::map<uint16_t, std::vector<uint16_t>> result;
				if (normalizedCoordinates.empty())
					return result;

				// GPOS 1.1 header: ... Offset32 featureVariationsOffset at offset 10.
				const std::span<const char> gpos(m_bytes, m_length);
				if (ReadBigEndian<uint16_t>(gpos, 0) != 1 || ReadBigEndian<uint16_t>(gpos, 2).value_or(0) < 1)
					return result;
				const auto variationsOffset = static_cast<size_t>(ReadBigEndian<uint32_t>(gpos, 10).value_or(0));
				if (!variationsOffset || variationsOffset >= m_length)
					return result;

				// FeatureVariations { uint16 major, minor; uint32 recordCount; { Offset32 conditionSetOffset; Offset32 featureTableSubstitutionOffset; }[] }
				const auto variations = gpos.subspan(variationsOffset);
				const auto recordCount = ReadBigEndian<uint32_t>(variations, 4).value_or(0);
				for (size_t i = 0; i < recordCount; i++) {
					const auto conditionSetOffset = ReadBigEndian<uint32_t>(variations, 8 + 8 * i);
					const auto substitutionOffset = ReadBigEndian<uint32_t>(variations, 12 + 8 * i);
					if (!conditionSetOffset || !substitutionOffset)
						return result;

					// ConditionSet { uint16 conditionCount; Offset32 conditionOffsets[]; }; an empty or absent set always matches.
					auto matches = true;
					if (*conditionSetOffset) {
						const auto conditionCount = ReadBigEndian<uint16_t>(variations, *conditionSetOffset).value_or(0);
						for (size_t j = 0; j < conditionCount && matches; j++) {
							const auto conditionOffset = ReadBigEndian<uint32_t>(variations, *conditionSetOffset + 2 + 4 * j);
							if (!conditionOffset) {
								matches = false;
								break;
							}

							// ConditionFormat1 { uint16 format = 1; uint16 axisIndex; F2DOT14 filterRangeMinValue, filterRangeMaxValue; }
							const auto conditionTableOffset = static_cast<size_t>(*conditionSetOffset) + *conditionOffset;
							const auto format = ReadBigEndian<uint16_t>(variations, conditionTableOffset);
							const auto axisIndex = ReadBigEndian<uint16_t>(variations, conditionTableOffset + 2);
							const auto minValue = ReadBigEndian<int16_t>(variations, conditionTableOffset + 4);
							const auto maxValue = ReadBigEndian<int16_t>(variations, conditionTableOffset + 6);
							if (format != 1 || !axisIndex || !minValue || !maxValue) {
								matches = false;
								break;
							}
							const auto coordinate = *axisIndex < normalizedCoordinates.size() ? normalizedCoordinates[*axisIndex] : 0.f;
							matches = *minValue / 16384.f <= coordinate && coordinate <= *maxValue / 16384.f;
						}
					}
					if (!matches)
						continue;

					// FeatureTableSubstitution { uint16 major, minor; uint16 count; { uint16 featureIndex; Offset32 alternateFeatureOffset; }[] }
					const auto substitutionCount = ReadBigEndian<uint16_t>(variations, *substitutionOffset + 4).value_or(0);
					for (size_t j = 0; j < substitutionCount; j++) {
						const auto recordOffset = static_cast<size_t>(*substitutionOffset) + 6 + 6 * j;
						const auto featureIndex = ReadBigEndian<uint16_t>(variations, recordOffset);
						const auto featureOffset = ReadBigEndian<uint32_t>(variations, recordOffset + 2);
						if (!featureIndex || !featureOffset)
							break;

						// FeatureTable { Offset16 featureParamsOffset; uint16 lookupIndexCount; uint16 lookupListIndices[]; }
						const auto featureTableOffset = static_cast<size_t>(*substitutionOffset) + *featureOffset;
						auto& lookups = result[*featureIndex];
						const auto lookupCount = ReadBigEndian<uint16_t>(variations, featureTableOffset + 2).value_or(0);
						for (size_t k = 0; k < lookupCount; k++) {
							if (const auto lookupIndex = ReadBigEndian<uint16_t>(variations, featureTableOffset + 4 + 2 * k))
								lookups.push_back(*lookupIndex);
						}
					}
					break;
				}

				return result;
			}

			[[nodiscard]] FeatureList::View GetFeatureListView() const {
				const auto featureListOffset = static_cast<size_t>(*m_obj->HeaderV1_0.FeatureListOffset);
				if (!featureListOffset || featureListOffset >= m_length)
					return {};
				return { m_bytes + featureListOffset, m_length - featureListOffset };
			}

			// SinglePos subtable, format 1 (one ValueRecord for all covered glyphs) or format 2 (one ValueRecord per covered glyph).
			class SinglePositioningSubtable {
				std::span<const char> m_data;
				CoverageTable::View m_coverage;
				uint16_t m_format = 0;
				uint16_t m_valueFormat = 0;
				size_t m_valueCount = 0;
				size_t m_recordSize = 0;

			public:
				explicit SinglePositioningSubtable(std::span<const char> data) {
					if (data.size() < 6)
						return;

					const auto read16 = [&data](size_t offset) -> uint16_t { return **reinterpret_cast<const BE<uint16_t>*>(data.data() + offset); };
					const auto format = read16(0);
					const auto coverageOffset = static_cast<size_t>(read16(2));
					const auto valueFormat = read16(4);
					const auto recordSize = sizeof(uint16_t) * std::popcount(static_cast<uint32_t>(valueFormat));
					size_t valueCount;
					switch (format) {
						case 1:
							valueCount = 1;
							if (data.size() < 6 + recordSize)
								return;
							break;
						case 2:
							if (data.size() < 8)
								return;
							valueCount = read16(6);
							if (data.size() < 8 + recordSize * valueCount)
								return;
							break;
						default:
							return;
					}

					if (coverageOffset >= data.size())
						return;
					CoverageTable::View coverage(data.subspan(coverageOffset));
					if (!coverage)
						return;

					m_data = data;
					m_coverage = coverage;
					m_format = format;
					m_valueFormat = valueFormat;
					m_valueCount = valueCount;
					m_recordSize = recordSize;
				}

				explicit operator bool() const {
					return m_format != 0;
				}

				// Returns the ValueRecord applied to the glyph, or nullptr if this subtable does not cover the glyph.
				[[nodiscard]] const char* GetValueRecord(uint16_t glyph) const {
					const auto coverageIndex = m_coverage.GetCoverageIndex(glyph);
					if (coverageIndex == (std::numeric_limits<size_t>::max)())
						return nullptr;
					if (m_format == 1)
						return m_data.data() + 6;
					if (coverageIndex >= m_valueCount)
						return nullptr;
					return m_data.data() + 8 + m_recordSize * coverageIndex;
				}

				// desiredRecord must have exactly one bit set.
				[[nodiscard]] double ReadValue(const char* pRecord, ValueFormatFlags desiredRecord, const ValueContext& context) const {
					return ReadValueRecordField(m_data, pRecord, m_valueFormat, desiredRecord, context);
				}
			};

			// Replaces an extension subtable with the subtable it points to.
			// Returns whether the resulting subtable is of the desired lookup type.
			static bool ResolveExtension(LookupType lookupType, LookupType desiredType, std::span<const char>& subtableSpan) {
				if (lookupType == desiredType)
					return true;
				if (lookupType != LookupType::ExtensionPositioning || subtableSpan.size() < sizeof(ExtensionPositioningSubtable::Format1))
					return false;

				const auto& table = *reinterpret_cast<const ExtensionPositioningSubtable::Format1*>(subtableSpan.data());
				if (*table.PosFormat != 1 || *table.ExtensionLookupType != desiredType || *table.ExtensionOffset >= subtableSpan.size())
					return false;

				subtableSpan = subtableSpan.subspan(*table.ExtensionOffset);
				return true;
			}

			struct PairAdjustmentSubtableRef {
				std::span<const char> Data;
				PairAdjustmentPositioningSubtable::Format1::View Format1;
				PairAdjustmentPositioningSubtable::Format2::View Format2;
				CoverageTable::View Coverage;

				// Format2 only: glyphs that are mapped from a codepoint, grouped by their class in ClassDef2.
				// Class 0 contains the mapped glyphs that are not listed in ClassDef2.
				std::map<uint16_t, std::vector<uint16_t>> Class2Glyphs;
			};

			static void ApplyPairAdjustmentLookup(
				const LookupTable::View& lookupTable,
				const std::vector<std::set<char32_t>>& glyphToCharMap,
				const std::vector<uint16_t>& mappedGlyphs,
				std::unordered_map<uint32_t, double>& glyphPairs,
				const ValueContext& context
			) {
				std::vector<PairAdjustmentSubtableRef> subtables;
				for (size_t subtableIndex = 0, i_ = *lookupTable->Header.SubtableCount; subtableIndex < i_; subtableIndex++) {
					auto subtableSpan = lookupTable.SubtableSpan(subtableIndex);
					if (!ResolveExtension(*lookupTable->Header.LookupType, LookupType::PairAdjustment, subtableSpan))
						continue;

					PairAdjustmentSubtableRef ref;
					ref.Data = subtableSpan;
					if (PairAdjustmentPositioningSubtable::Format1::View v(subtableSpan); v) {
						ref.Format1 = v;
						ref.Coverage = v.CoverageTableView();
					} else if (PairAdjustmentPositioningSubtable::Format2::View v2(subtableSpan); v2) {
						ref.Format2 = v2;
						ref.Coverage = v2.CoverageTableView();

						const auto classDef2 = v2.GetClassTableDefinition2();
						for (const auto glyph : mappedGlyphs)
							ref.Class2Glyphs[classDef2.GetClass(glyph)].push_back(glyph);
					} else {
						continue;
					}

					if (ref.Coverage)
						subtables.emplace_back(std::move(ref));
				}

				if (subtables.empty())
					return;

				std::unordered_set<uint16_t> claimedRightGlyphs;
				for (const auto glyph1 : mappedGlyphs) {
					claimedRightGlyphs.clear();

					for (const auto& subtable : subtables) {
						const auto coverageIndex = subtable.Coverage.GetCoverageIndex(glyph1);
						if (coverageIndex == (std::numeric_limits<size_t>::max)())
							continue;

						if (const auto& v = subtable.Format1) {
							if (coverageIndex >= *v->Header.PairSetCount)
								continue;

							const auto pairSetView = v.PairSetView(coverageIndex);
							if (!pairSetView)
								continue;

							// Device offsets in ValueRecords of PairPosFormat1 are relative to the PairSet table.
							const auto pairSetData = subtable.Data.subspan(*v->PairSetOffsets[coverageIndex]);

							for (size_t pairIndex = 0, j_ = *pairSetView->Count; pairIndex < j_; pairIndex++) {
								const auto glyph2 = pairSetView.GetSecondGlyph(pairIndex);
								if (!claimedRightGlyphs.insert(glyph2).second)
									continue;

								if (glyph2 >= glyphToCharMap.size() || glyphToCharMap[glyph2].empty())
									continue;

								// PairValueRecord { uint16 secondGlyph; ValueRecord valueRecord1; ValueRecord valueRecord2; }
								const auto valueFormat1 = (*v->Header.ValueFormat1).Value;
								const auto valueFormat2 = (*v->Header.ValueFormat2).Value;
								const auto pRecord1 = reinterpret_cast<const char*>(pairSetView.GetPairValueRecord(pairIndex) + 1);
								const auto pRecord2 = pRecord1 + sizeof(uint16_t) * std::popcount(static_cast<uint32_t>(valueFormat1));
								const auto val = ReadValueRecordField(pairSetData, pRecord1, valueFormat1, { .AdvanceX = 1 }, context)
									+ ReadValueRecordField(pairSetData, pRecord2, valueFormat2, { .PlacementX = 1 }, context);
								if (val != 0)
									glyphPairs[(static_cast<uint32_t>(glyph1) << 16) | glyph2] += val;
							}

						} else {
							const auto& v2 = subtable.Format2;
							const auto class1 = v2.GetClassTableDefinition1().GetClass(glyph1);
							if (class1 >= *v2->Header.Class1Count)
								continue;

							// Once the left glyph is covered, a Format2 subtable applies to every right glyph.
							for (const auto& [class2, glyphs2] : subtable.Class2Glyphs) {
								if (class2 >= *v2->Header.Class2Count)
									continue;

								// Class2Record { ValueRecord valueRecord1; ValueRecord valueRecord2; }
								const auto valueFormat1 = (*v2->Header.ValueFormat1).Value;
								const auto valueFormat2 = (*v2->Header.ValueFormat2).Value;
								const auto pRecord1 = reinterpret_cast<const char*>(v2.GetPairValueRecord(class1, class2));
								const auto pRecord2 = pRecord1 + sizeof(uint16_t) * std::popcount(static_cast<uint32_t>(valueFormat1));
								const auto val = ReadValueRecordField(subtable.Data, pRecord1, valueFormat1, { .AdvanceX = 1 }, context)
									+ ReadValueRecordField(subtable.Data, pRecord2, valueFormat2, { .PlacementX = 1 }, context);
								if (val == 0)
									continue;

								for (const auto glyph2 : glyphs2) {
									if (!claimedRightGlyphs.contains(glyph2))
										glyphPairs[(static_cast<uint32_t>(glyph1) << 16) | glyph2] += val;
								}
							}
							break;
						}
					}
				}
			}

		public:
		};
	};

	struct SfntFile {
		// http://formats.kaitai.io/ttf/ttf.svg

		OffsetTableStruct OffsetTable;
		DirectoryTableEntry DirectoryTable[1];

		class View {
			union {
				const SfntFile* m_obj;
				const char* m_bytes;
			};
			size_t m_length;
			size_t m_offsetInCollection;

		public:
			View() : m_obj(nullptr), m_length(0), m_offsetInCollection(0) {}
			View(std::nullptr_t) : View() {}
			View(decltype(m_obj) pObject, size_t length, size_t offsetInCollection = 0)
				: m_obj(pObject), m_length(length), m_offsetInCollection(offsetInCollection) {}
			View(View&&) = default;
			View(const View&) = default;
			View& operator=(View&&) = default;
			View& operator=(const View&) = default;
			View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; return *this; }
			View(const void* pData, size_t length, size_t offsetInCollection = 0) : View(std::span(static_cast<const char*>(pData), length), offsetInCollection) {}
			template<typename T>
			View(std::span<T> data, size_t offsetInCollection = 0) : View() {
				if (data.size_bytes() < sizeof(OffsetTable))
					return;

				const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

				if (data.size_bytes() < sizeof(OffsetTable) + sizeof(DirectoryTableEntry) * obj->OffsetTable.TableCount)
					return;

				size_t requiredLength = sizeof(OffsetTableStruct) + sizeof(DirectoryTableEntry) * *obj->OffsetTable.TableCount;
				for (size_t i = 0, i_ = *obj->OffsetTable.TableCount; i < i_; i++)
					requiredLength = (std::max)(requiredLength, static_cast<size_t>(*obj->DirectoryTable[i].Offset) + *obj->DirectoryTable[i].Length);
				if (requiredLength > data.size_bytes())
					return;

				m_obj = obj;
				m_length = data.size_bytes();
				m_offsetInCollection = offsetInCollection;
			}

			operator bool() const {
				return !!m_obj;
			}

			decltype(m_obj) operator*() const {
				return m_obj;
			}

			decltype(m_obj) operator->() const {
				return m_obj;
			}

			[[nodiscard]] std::span<const DirectoryTableEntry> DirectoryTableSpan() const {
				return { m_obj->DirectoryTable, *m_obj->OffsetTable.TableCount };
			}

			[[nodiscard]] std::span<const char> GetDirectoryTable(const TagStruct& tag) const {
				for (const auto& table : DirectoryTableSpan())
					if (table.Tag == tag)
						return { m_bytes + *table.Offset - m_offsetInCollection, *table.Length };
				return {};
			}

			template<typename Table>
			typename Table::View TryGetTable() const {
				const auto s = GetDirectoryTable(Table::DirectoryTableTag);
				if (s.empty())
					return {};

				return Table::View(s);
			}
		};
	};

	struct TtcFile {
		struct Header {
			static constexpr TagStruct HeaderTag{ { 't', 't', 'c', 'f' } };

			TagStruct Tag;
			BE<uint16_t> MajorVersion;
			BE<uint16_t> MinorVersion;
			BE<uint32_t> FontCount;
		};

		struct DigitalSignatureHeader {
			static constexpr TagStruct HeaderTag{ { 'D', 'S', 'I', 'G' } };

			TagStruct Tag;
			BE<uint32_t> Length;
			BE<uint32_t> Offset;
		};

		Header FileHeader;
		BE<uint32_t> FontOffsets[1];

		class View {
			union {
				const TtcFile* m_obj;
				const char* m_bytes;
			};
			size_t m_length;

		public:
			View() : m_obj(nullptr), m_length(0) {}
			View(std::nullptr_t) : View() {}
			View(decltype(m_obj) pObject, size_t length)
				: m_obj(pObject), m_length(length) {}
			View(View&&) = default;
			View(const View&) = default;
			View& operator=(View&&) = default;
			View& operator=(const View&) = default;
			View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; return *this; }
			View(const void* pData, size_t length) : View(std::span(static_cast<const char*>(pData), length)) {}
			template<typename T>
			View(std::span<T> data) : View() {
				if (data.size_bytes() < sizeof(Header))
					return;

				const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

				if (obj->FileHeader.Tag != Header::HeaderTag)
					return;
				if (obj->FileHeader.MajorVersion == 0)
					return;
				if (data.size_bytes() < sizeof(Header) + sizeof(uint32_t) * obj->FileHeader.FontCount)
					return;
				if (obj->FileHeader.MajorVersion >= 2) {
					if (data.size_bytes() < sizeof(Header) + sizeof(uint32_t) * obj->FileHeader.FontCount + sizeof(DigitalSignatureHeader))
						return;

					const auto pDsig = reinterpret_cast<const DigitalSignatureHeader*>(&obj->FontOffsets[*obj->FileHeader.FontCount]);
					if (pDsig->Tag.NativeValue == 0)
						void();
					else if (pDsig->Tag == DigitalSignatureHeader::HeaderTag) {
						if (data.size_bytes() < static_cast<size_t>(*pDsig->Offset) + *pDsig->Length)
							return;
					} else
						return;
				}
				for (size_t i = 0, i_ = *obj->FileHeader.FontCount; i < i_; i++) {
					const auto offset = static_cast<size_t>(*obj->FontOffsets[i]);
					if (SfntFile::View v(reinterpret_cast<const char*>(obj) + offset, data.size_bytes() - offset, offset); !v)
						return;
				}

				m_obj = obj;
				m_length = data.size_bytes();
			}

			operator bool() const {
				return !!m_obj;
			}

			decltype(m_obj) operator*() const {
				return m_obj;
			}

			decltype(m_obj) operator->() const {
				return m_obj;
			}

			[[nodiscard]] size_t GetFontCount() const {
				return *m_obj->FileHeader.FontCount;
			}

			[[nodiscard]] SfntFile::View GetFont(size_t index) const {
				if (index >= m_obj->FileHeader.FontCount)
					return {};

				const auto offset = static_cast<size_t>(*m_obj->FontOffsets[index]);
				return SfntFile::View(m_bytes + offset, m_length - offset, offset);
			}
		};
	};
}
#pragma pack(pop)

#endif
