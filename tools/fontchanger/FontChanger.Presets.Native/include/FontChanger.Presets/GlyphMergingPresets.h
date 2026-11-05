#pragma once

#include <array>
#include <string>
#include <vector>

#include "FontChanger.FixedSizeFont/glyph_merging_fixed_size_font.h"

namespace FontChanger::GlyphMergingPresets {
	struct Preset {
		std::vector<FontChanger::FixedSizeFont::glyph_merge_mapping> Mappings;
		std::u32string Codepoints;
	};

	inline constexpr size_t Count = 13;

	// The groups of FontChanger.FixedSizeFont's data/glyph_merge_shapes.json, as the font editor offers them; texts
	// beginning with "_" are of the underlined IME indicator boxes.
	const std::array<Preset, Count>& Get();
}
