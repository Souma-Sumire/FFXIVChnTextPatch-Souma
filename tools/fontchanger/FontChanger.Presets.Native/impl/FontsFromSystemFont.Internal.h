#pragma once

#include "FontChanger.Presets/CodepointRanges.h"
#include "FontChanger.Presets/FontsFromSystemFont.h"

namespace FontChanger::FontsFromSystemFont::Internal {
	using FontChanger::CodepointRanges::Ranges;
	using FontChanger::CodepointRanges::ScriptGroup;
	using FontChanger::Structs::FontElement;
	using FontChanger::Structs::LookupStruct;
	using FontChanger::FixedSizeFont::codepoint_merge_mode;

	inline const Ranges AllCodepoints{{0x20, 0x10FFFF}};
	inline const Ranges PrivateUseCodepoints{{0xE000, 0xF8FF}};

	// JupiterN's critical-hit exclamation mark, as tuned in the game with Source Han Sans K.
	struct MarkTraits {
		float SizePerDigitSize = 146.f / 96;
		float OffsetPerSize = -8.f / 96;
		float VerticalOffsetPerSize = 15.f / 96;

		// The gap between the digits' ink and the mark's over the size, and the pitch of "!!" over the width of its ink.
		float GapPerSize = -0.256f;
		float PitchPerInk = 0.424f;
	};

	struct Traits {
		// 8 for the digit-only families, as JupiterN's old style 8 is as tall as a lining one.
		std::u32string_view ReferenceCharacters;

		// Over the size, as the game draws it; AXIS's is of AXIS Basic ProN, which draws AXIS as the game does.
		float ReferenceHeight;

		// Unset where the lowercase letters are capitals (MiedingerMid) or small capitals (Jupiter, with smcp).
		std::optional<float> XHeightPerCapHeight;

		bool HasText;

		// Of any client; AXIS fonts have the game's symbols and IME indicators.
		bool IsAxis;

		std::vector<std::string_view> Features;
		std::optional<MarkTraits> Mark;

		static Traits Of(std::string_view family) {
			constexpr std::u32string_view capitals = U"HEFILTZ";
			if (family == "Jupiter")
				return {capitals, 0.773f, std::nullopt, true, false, {"onum", "smcp"}};
			if (family == "JupiterN")
				return {U"8", 0.76f, std::nullopt, false, false, {"onum"}, MarkTraits{}};
			if (family == "Meidinger")
				return {U"8", 0.71f, std::nullopt, false, false, {}};
			if (family == "MiedingerMid")
				return {capitals, 0.705f, std::nullopt, true, false, {}};
			if (family == "TrumpGothic")
				return {capitals, 0.761f, 0.74f, true, false, {}};
			return {capitals, 0.768f, 0.75f, true, true, {}};
		}
	};

	struct CjkGroup {
		ScriptGroup Group;
		FontScripts Scripts;
		const Ranges& Codepoints;
		std::u32string_view Samples;

		// The samples' ink in the game's AXIS over its cap height, averaged from AXIS_18 and AXIS_36 (KrnAXIS for Hangul)
		// where row coverage crosses one half.
		float TopPerCapHeight;
		float BottomPerCapHeight;

		// Null for NonCjk.
		static const CjkGroup* Of(ScriptGroup group);
	};

	inline const std::array<CjkGroup, 3> CjkGroups{{
		{ScriptGroup::Han, FontScripts::Han, CodepointRanges::GetScriptGroupCodepoints(ScriptGroup::Han), U"口日田中国東", 1.036f, -0.075f},
		{ScriptGroup::Kana, FontScripts::Kana, CodepointRanges::GetScriptGroupCodepoints(ScriptGroup::Kana), U"あアのカ", 1.012f, -0.049f},
		{ScriptGroup::Hangul, FontScripts::Hangul, CodepointRanges::GetScriptGroupCodepoints(ScriptGroup::Hangul), U"가한글국를", 1.070f, -0.109f},
	}};

	inline const CjkGroup* CjkGroup::Of(ScriptGroup group) {
		const auto it = std::ranges::find(CjkGroups, group, &CjkGroup::Group);
		return it == CjkGroups.end() ? nullptr : &*it;
	}

	// In ems above the baseline.
	struct InkRange {
		float Top;
		float Bottom;
	};

	// Pixels by which the game's AXIS draws glyphs right of (Latin, PUA) and below (CJK, PUA) where AXIS Basic ProN would,
	// assumed to scale with the size for other families.
	struct Placement {
		float LatinOffset;
		float PuaOffset;
		float CjkShift;

		// nominalSize is that of the game's font by its name, which larger fonts are scaled from.
		static Placement Of(const Traits& traits, float size, float nominalSize) {
			Placement res;
			if (!traits.IsAxis)
				res = {(std::max)(1.f, std::round(nominalSize / 12)), 0, 0};
			else if (nominalSize >= 36)
				res = {3, 3, -1};
			else
				res = {1, 1, 0};

			const auto scale = nominalSize > 0 ? size / nominalSize : 1.f;
			return {std::round(res.LatinOffset * scale), std::round(res.PuaOffset * scale), std::round(res.CjkShift * scale)};
		}
	};

	// Offsets are in pixels, VerticalOffset downwards.
	struct GroupPlacement {
		float Size;
		DWRITE_RENDERING_MODE1 RenderMode;
		float HorizontalOffset;
		float VerticalOffset;
	};

	// In pixels, the descent below the baseline.
	struct LineMetrics {
		float Ascent;
		float Descent;

		static LineMetrics Of(const FontChanger::FixedSizeFont::fixed_size_font& font, float scale = 1.f) {
			const auto ascent = std::round(static_cast<float>(font.ascent()) * scale);
			return {ascent, std::round(static_cast<float>(font.line_height()) * scale) - ascent};
		}
	};

	struct LatinPlacement {
		float Size;
		DWRITE_RENDERING_MODE1 RenderMode;
		float VerticalOffset;
	};

	class FontCache {
		std::map<std::tuple<std::string, DWRITE_FONT_WEIGHT, DWRITE_FONT_STRETCH, DWRITE_FONT_STYLE>, std::optional<FontInfo>> m_fonts;

	public:
		const FontInfo* operator()(const LookupStruct& lookup) {
			const auto key = std::make_tuple(lookup.FamilyName, lookup.Weight, lookup.Stretch, lookup.Style);
			auto it = m_fonts.find(key);
			if (it == m_fonts.end())
				it = m_fonts.emplace(key, FontInfo::Open(lookup.FamilyName, lookup.Weight, lookup.Stretch, lookup.Style)).first;
			return it->second ? &*it->second : nullptr;
		}
	};

	struct ExclamationMarkPlacement {
		float DigitOffset;
		float Size;
		DWRITE_RENDERING_MODE1 RenderMode;
		float Offset;
		float VerticalOffset;
		float LetterSpacing;
	};

	std::optional<InkRange> MeasureInk(const FontProbe& font, std::u32string_view samples);

	Ranges GetEmWideSymbols(const FontProbe& font, const FontChanger::FixedSizeFont::fixed_size_font* gameFont);

	bool HasText(const FontProbe& font, std::u32string_view text);

	bool HasTabularDigits(const FontProbe& font);

	std::pair<float, DWRITE_RENDERING_MODE1> GetSizeAndMode(const FontProbe& font, float exactSize);

	// Targets are in pixels above the baseline.
	GroupPlacement FitInk(const FontProbe& font, const InkRange& ink, float targetTop, float targetBottom, float latinSize, float horizontalOffset);

	GroupPlacement KeepWithinLine(const FontProbe& font, const InkRange& ink, const GroupPlacement& placement, const std::optional<LineMetrics>& line, float latinSize);

	bool IsLeftToGame(const FontProbe& font, const FontProbe* baseFont, const FontChanger::FixedSizeFont::fixed_size_font* gameFont, const CjkGroup& group);

	std::vector<std::pair<ScriptGroup, Ranges>> SplitByScriptGroup(const FontProbe& face, const Ranges& codepoints, const FontChanger::FixedSizeFont::fixed_size_font* gameFont, bool hasText);

	LatinPlacement FitLatin(const FontProbe& font, const Traits& traits, float size);

	LatinPlacement ScaleLatin(const FontProbe& font, const LatinPlacement& latin, float by);

	GroupPlacement ScaleAroundInk(const FontProbe& font, const InkRange& ink, const GroupPlacement& placement, float by);

	ExclamationMarkPlacement PlaceExclamationMark(const MarkTraits& traits, const FontProbe& digits, const FontProbe& mark, float fontSize, float size, float latinOffset);
}
