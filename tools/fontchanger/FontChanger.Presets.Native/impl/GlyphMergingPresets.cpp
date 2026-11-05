#include "pch.h"
#include "FontChanger.Presets/GlyphMergingPresets.h"

const std::array<FontChanger::GlyphMergingPresets::Preset, FontChanger::GlyphMergingPresets::Count>& FontChanger::GlyphMergingPresets::Get() {
	static const auto presets = [] {
		const auto& groups = FontChanger::FixedSizeFont::get_glyph_merge_presets();
		if (groups.size() != Count)
			throw std::runtime_error(std::format("Expected {} glyph merging presets, got {}", Count, groups.size()));

		std::array<Preset, Count> res;
		for (size_t i = 0; i < Count; i++) {
			res[i].Mappings = groups[i];
			for (const auto& mapping : res[i].Mappings)
				res[i].Codepoints += mapping.Codepoints;
		}
		return res;
	}();
	return presets;
}
