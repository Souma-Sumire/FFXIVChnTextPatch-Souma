#ifndef MAINWINDOW_INTERNAL_H
#define MAINWINDOW_INTERNAL_H

#include "FontChanger.Presets/Structs.h"
#include "resource.h"

enum : uint8_t {
	ListViewColsFamilyName,
	ListViewColsSubfamilyName,
	ListViewColsSize,
	ListViewColsLineHeight,
	ListViewColsAscent,
	ListViewColsHorizontalOffset,
	ListViewColsLetterSpacing,
	ListViewColsGamma,
	ListViewColsCodepoints,
	ListViewColsGlyphCount,
	ListViewColsMergeMode,
	ListViewColsSource,
	ListViewColsLookup,
};

static constexpr auto FontListBoxWidth = 160;
static constexpr auto FontElementListViewHeight = 160;
static constexpr auto PreviewEditHeight = 60;
static constexpr auto SplitterThickness = 4;
static constexpr auto MinFontElementListViewHeight = 80;
static constexpr auto MinPreviewEditHeight = 60;
static constexpr auto MinPreviewHeight = 120;

static constexpr auto PreviewZoomOne = 100;
static constexpr auto PreviewZoomMin = 100;
static constexpr auto PreviewZoomScrollUnit = 100;
static constexpr auto PreviewZoomMax = 1600;

// Number of texture files the game loads for a built-in texture filename format (font{}.tex).
inline std::optional<int> GetGameTextureCount(std::string_view texFilenameFormat) {
	for (const auto fontType : FontChanger::FixedSizeFont::get_font_types()) {
		const std::string_view format(FontChanger::FixedSizeFont::get_font_tex_filename_format(fontType));
		if (format.substr(format.rfind('/') + 1) == texFilenameFormat)
			return FontChanger::FixedSizeFont::get_font_texture_count(fontType);
	}
	return std::nullopt;
}

// The game fonts that exports may also write the main game fonts (font{}.tex) as: the setting that enables it, the menu
// item that toggles it, and the font type written.
struct ExportMapping {
	bool FontChanger::Structs::MultiFontSet::* Enabled;
	UINT MenuId;
	xivres::font_type Target;
};

inline constexpr ExportMapping ExportMappings[]{
	{&FontChanger::Structs::MultiFontSet::ExportMapFontLobbyToFont, ID_EXPORT_MAPFONTLOBBY, xivres::font_type::font_lobby},
	{&FontChanger::Structs::MultiFontSet::ExportMapChnAxisToFont, ID_EXPORT_MAPFONTCHNAXIS, xivres::font_type::chn_axis},
	{&FontChanger::Structs::MultiFontSet::ExportMapKrnAxisToFont, ID_EXPORT_MAPFONTKRNAXIS, xivres::font_type::krn_axis},
	{&FontChanger::Structs::MultiFontSet::ExportMapTcAxisToFont, ID_EXPORT_MAPFONTTCAXIS, xivres::font_type::tc_axis},
};

// How the list of elements shows the codepoints and the source of an element.
std::wstring GetRangeRepresentation(const FontChanger::Structs::FontElement& element);
std::wstring GetSourceRepresentation(const FontChanger::Structs::FontElement& element);

static constexpr GUID Guid_IFileDialog_Json{0x5c2fc703, 0x7406, 0x4704, {0x92, 0x12, 0xae, 0x41, 0x1d, 0x4b, 0x74, 0x67}};
static constexpr GUID Guid_IFileDialog_Export{0x5c2fc703, 0x7406, 0x4704, {0x92, 0x12, 0xae, 0x41, 0x1d, 0x4b, 0x74, 0x68}};

#endif
