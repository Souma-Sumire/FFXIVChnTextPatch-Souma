#pragma once

#include "FontChanger.Presets/Structs.h"

// What the editor does with glyph files (FontChanger.FixedSizeFont/image_glyph_files.h): watches their folders, embeds them in
// elements, and exports fonts as them.
namespace App::GlyphFileTools {
	// Starts watching the folders that fonts are made from: when files in one change, it is invalidated, and the message
	// is posted to the window. Without this, as on the command line, folders are not watched.
	void SetChangeNotificationWindow(HWND hWnd, UINT message);

	// Returns the folders that have changed since the last call.
	[[nodiscard]] std::vector<std::filesystem::path> TakeChangedFolders();

	// Stores the files of the folder in the settings, so that the configuration does not need the folder any more.
	void Embed(Structs::GlyphImagesStruct& settings);

	struct ExportOptions {
		bool Svg = true;
		bool Png = true;

		// Whether the font has the adjustments of an element applied; recorded in font.json.
		bool WithAdjustments = false;

		// Describes where the glyphs came from; recorded in font.json.
		nlohmann::json Source;
	};

	// Returns the font of the glyphs that an element adds, with or without its letter spacing, offset, and vertical offset.
	[[nodiscard]] std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font> GetElementFontForExport(const Structs::FontElement& element, bool withAdjustments);

	// Describes where the glyphs of an element or a font come from, for the source of ExportOptions.
	[[nodiscard]] nlohmann::json DescribeSource(const Structs::FontElement& element);
	[[nodiscard]] nlohmann::json DescribeSource(const Structs::Font& font);

	// Writes the glyphs of the font into the folder. progress is called with the number of glyphs written and the
	// total, and may throw to stop.
	void ExportGlyphs(
		const FontChanger::FixedSizeFont::fixed_size_font& font,
		const std::filesystem::path& folder,
		const ExportOptions& options,
		const std::function<void(size_t, size_t)>& progress = {});
}
