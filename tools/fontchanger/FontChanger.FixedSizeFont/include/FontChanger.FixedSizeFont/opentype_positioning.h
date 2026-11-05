#ifndef XIVRES_FONTGENERATOR_OPENTYPEPOSITIONING_H_
#define XIVRES_FONTGENERATOR_OPENTYPEPOSITIONING_H_

#include <map>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

typedef struct hb_face_t hb_face_t;

namespace FontChanger::FixedSizeFont {
	// Adjustment of a single glyph, in pixels. PlacementY grows downwards.
	struct glyph_adjustment {
		int PlacementX = 0;
		int PlacementY = 0;
		int AdvanceX = 0;
	};

	struct opentype_positioning_params {
		std::span<const char> Gpos;
		std::span<const char> Kern;

		// Tables of variable fonts; empty if not a variable font.
		std::span<const char> Gdef;
		std::span<const char> Fvar;
		std::span<const char> Avar;

		// Design coordinates of the instance, keyed by axis tags in DWRITE_FONT_AXIS_TAG byte order; axes not listed are at their default.
		std::map<uint32_t, float> DesignCoordinates;

		// Features enabled in addition to 'kern', in DWRITE_FONT_FEATURE_TAG byte order.
		std::set<uint32_t> FeatureTags;

		// Features turned off, in DWRITE_FONT_FEATURE_TAG byte order. Turning off 'kern' leaves out pair kerning entirely.
		std::set<uint32_t> DisabledFeatureTags;

		// BCP 47 language tag of the text, such as "ja" or "zh-Hant"; empty if unspecified.
		std::string LanguageCode;

		float Size = 0.f;
		unsigned UnitsPerEm = 0;

		// Horizontal and vertical scale of the transformation matrix.
		float ScaleX = 1.f;
		float ScaleY = 1.f;

		// HarfBuzz face of the font, to find kerning applied by contextual lookups; may be null.
		hb_face_t* HarfBuzzFace = nullptr;
	};

	struct opentype_positioning {
		// Pair kerning in pixels, keyed by codepoints.
		std::map<std::pair<char32_t, char32_t>, int> KerningPairs;

		// Single glyph adjustments, keyed by glyph IDs.
		std::unordered_map<uint16_t, glyph_adjustment> GlyphAdjustments;
	};

	// Extracts what the FDT format can represent of the font's glyph positioning:
	// pair kerning from GPOS 'kern' (or the legacy kern table if GPOS has no 'kern' feature),
	// and single glyph adjustments from GPOS 'kern' and the enabled features; 'kern' contributes nothing if turned off.
	// glyphToCharMap maps each glyph ID that is drawn to the codepoints drawn with it.
	[[nodiscard]] opentype_positioning extract_opentype_positioning(const opentype_positioning_params& params, const std::vector<std::set<char32_t>>& glyphToCharMap);

	// Reads the baselines of the BASE table used to align fonts, in font units above the origin of the glyphs, keyed by
	// baseline tags in DWRITE_FONT_FEATURE_TAG byte order: 'romn' as listed for Latin, and 'ideo', 'idtp', 'icfb', and 'icft'
	// as listed for Han, kana, or Hangul.
	[[nodiscard]] std::map<uint32_t, int> read_baselines(std::span<const char> base);

	// Returns the ISO 15924 script code (such as "Latn" or "Hira") of text in the given BCP 47 language:
	// the script subtag if the tag has one, otherwise the usual script of the language, or "Latn" if unknown.
	// Characters that belong to no particular script, such as punctuation, are assumed to be used in text of this script.
	[[nodiscard]] std::string get_default_script_for_language(const std::string& languageCode);
}

#endif
