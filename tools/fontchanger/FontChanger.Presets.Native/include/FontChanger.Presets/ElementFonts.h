#pragma once

#include <exception>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include "Structs.h"

// What the fonts are made from, the game installations and the project directory, is configured process-wide.
namespace FontChanger::ElementFonts {
	struct SynthesizedFace {
		// DirectWrite's oblique simulation shears by x += 0.33985 * height (about 18.77 degrees), as measured from
		// GetGlyphRunOutline of Arial 'l', and FreeType slants by the same.
		static constexpr float ObliqueSlope = 0.33985f;

		// Unset to keep those of the matched font, as earlier versions did.
		std::optional<DWRITE_FONT_SIMULATIONS> Simulations;

		// For FreeType, which emboldens outlines; negative values make glyphs thinner.
		int WeightDelta = 0;

		// For FreeType; DirectWrite slants with Simulations instead.
		bool Oblique = false;

		float ScaleX = 1.f;

		// Keyed in DWRITE_FONT_AXIS_TAG byte order; the lookup's Variations override them.
		std::map<uint32_t, float> AxisValues;

		// Applied on screen before the element's transformation: the slant, then the scale.
		[[nodiscard]] FontChanger::FixedSizeFont::font_render_transformation_matrix GetScreenMatrix() const;

		// In ems, for FreeType.
		[[nodiscard]] float GetEmbolden() const;
	};

	// As usWidthClass percentages of OS/2; 0 if undefined.
	[[nodiscard]] float GetStretchPercent(DWRITE_FONT_STRETCH stretch);

	std::pair<IDWriteFactoryPtr, IDWriteFontPtr> ResolveFont(const Structs::LookupStruct& lookup);

	[[nodiscard]] SynthesizedFace ResolveSynthesis(const Structs::LookupStruct& lookup, Structs::ElementSourceEnum renderer, IDWriteFont* font);

	// Changes the requested properties so that the font is drawn as before Synthesis was set.
	void ConvertToExplicitSynthesis(Structs::LookupStruct& lookup, Structs::ElementSourceEnum renderer);

	[[nodiscard]] std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font> MergeElements(const Structs::Font& font, const std::set<const Structs::FontElement*>& skip = {});

	void FlushCachedFonts();

	// The installations are tried in order; without a provider, elements of game fonts are drawn empty.
	void SetGameInstallationPathsProvider(std::function<std::vector<std::filesystem::path>(xivres::font_type fontType)> provider);

	// Called once until the cached fonts are flushed, for an installation whose fonts could not be read and are drawn empty.
	void SetGameFontErrorHandler(std::function<void(const std::exception& e)> handler);

	// Relative paths of the configuration resolve against the folder of its file; empty if it has not been saved.
	void SetProjectDirectory(std::filesystem::path path);
	[[nodiscard]] std::filesystem::path GetProjectDirectory();

	void OnProjectDirectoryChange(const Structs::MultiFontSet& multiFontSet);
}
