#ifndef XIVRES_FONTGENERATOR_IMAGEGLYPHFILES_H_
#define XIVRES_FONTGENERATOR_IMAGEGLYPHFILES_H_

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "image_fixed_size_font.h"

// Glyphs as files: one SVG or PNG file per glyph, named by the codepoint, and a font.json beside them for the metrics
// of the whole font. SVG files are in units of 1/1000 em with y growing downwards and the baseline at y = 880, unless
// their data-xivfont-units-per-em and data-xivfont-baseline attributes say otherwise; the width of the viewBox is the
// advance. PNG files cover the line box at the size in font.json, with the top of the line at the top of the image.
namespace FontChanger::FixedSizeFont {
	inline constexpr float glyph_files_units_per_em = 1000.f;
	inline constexpr float glyph_files_baseline_y = 880.f;
	inline constexpr auto glyph_files_metadata_file_name = L"font.json";

	// Returns "uniXXXX" for codepoints in the BMP, and "uXXXXX" for the others.
	[[nodiscard]] std::wstring get_glyph_file_stem(char32_t codepoint);

	// Reads the codepoint from a file name such as uni0041.svg, u1F600.png, or U+0041_bold.svg, ignoring the case.
	[[nodiscard]] std::optional<char32_t> parse_glyph_file_name(const std::filesystem::path& path);

	// Glyph files read from a folder, or from files kept elsewhere.
	struct glyph_file_set {
		std::map<char32_t, image_glyph_source> Glyphs;

		// The PNG files as they are, for keeping them elsewhere.
		std::map<char32_t, std::shared_ptr<const std::string>> PngFiles;

		// Contents of font.json, or null.
		nlohmann::json Metadata;

		// Files that are not taken: of names that are not of codepoints, of codepoints already taken, or unreadable.
		size_t SkippedCount = 0;

		// Why nothing could be read, if so.
		std::wstring Error;
	};

	// Reads the glyph files of a folder.
	[[nodiscard]] std::shared_ptr<glyph_file_set> read_glyph_folder(const std::filesystem::path& folder);

	// Decodes a PNG file into the source of a bitmap glyph.
	[[nodiscard]] image_glyph_source decode_png_glyph(std::string_view png);

	// What the font made from glyph files takes from elsewhere than the files, overriding font.json when set.
	struct glyph_files_font_options {
		// Units of SVG files and PNG files, which override their attributes and font.json when set.
		std::optional<float> UnitsPerEm;
		std::optional<float> BaselineY;

		// Line metrics in units of UnitsPerEm.
		std::optional<float> Ascent;
		std::optional<float> LineHeight;

		image_coverage_mode BitmapCoverage = image_coverage_mode::Auto;
		std::string FamilyName;
	};

	// Returns how to make a font of the glyph files at a size, with their metrics from font.json unless overridden.
	[[nodiscard]] image_fixed_size_font::create_struct make_glyph_files_font_params(const glyph_file_set& set, const glyph_files_font_options& options, float size);
}

#endif
