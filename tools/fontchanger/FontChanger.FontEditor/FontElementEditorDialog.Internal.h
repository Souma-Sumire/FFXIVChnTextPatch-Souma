#ifndef FONTELEMENTEDITORDIALOG_INTERNAL_H
#define FONTELEMENTEDITORDIALOG_INTERNAL_H

#include <exprtk.hpp>

#include "FontElementEditorDialog.h"
#include "FontChanger.Presets/GlyphMergingPresets.h"
#include "resource.h"

typedef struct hb_face_t hb_face_t;

namespace App::FontElementEditorDialogInternal {
	inline constexpr std::array PageDialogIds{
		IDD_FONTELEMENTEDITOR_FONT,
		IDD_FONTELEMENTEDITOR_CHARACTERS,
		IDD_FONTELEMENTEDITOR_TYPOGRAPHY,
		IDD_FONTELEMENTEDITOR_GLYPHMERGING,
	};

	inline constexpr std::array PageNameIds{
		IDS_FONTELEMENTEDITOR_TAB_FONT,
		IDS_FONTELEMENTEDITOR_TAB_CHARACTERS,
		IDS_FONTELEMENTEDITOR_TAB_TYPOGRAPHY,
		IDS_FONTELEMENTEDITOR_TAB_GLYPHMERGING,
	};

	// Number of the preset checkboxes of the Glyph merging page, whose IDs follow IDC_CHECK_GLYPHMERGING_PRESET_0.
	inline constexpr size_t GlyphMergingPresetCount = IDC_CHECK_GLYPHMERGING_PRESET_12 - IDC_CHECK_GLYPHMERGING_PRESET_0 + 1;
	static_assert(GlyphMergingPresetCount == FontChanger::GlyphMergingPresets::Count, "a checkbox per preset");

	// Whether the source is the font files of a system font, which have features and variations, and can be drawn at
	// other sizes.
	inline bool DrawsFontFiles(Structs::ElementSourceEnum source) {
		return source == Structs::ElementSourceEnum::DirectWrite || source == Structs::ElementSourceEnum::FreeType;
	}

	// Whether the element takes a system font by its family, weight, style, and stretch: those that draw font files, and
	// glyph images, whose texts of glyph merging are drawn with the font.
	inline bool UsesSystemFont(Structs::ElementSourceEnum source) {
		return DrawsFontFiles(source) || source == Structs::ElementSourceEnum::GlyphImages;
	}

	// Creates a HarfBuzz face of the font file of the lookup.
	std::shared_ptr<hb_face_t> CreateHarfBuzzFace(const Structs::LookupStruct& lookup);

	// Returns, for each GSUB feature of the font, the largest number of alternates that it offers for a glyph.
	std::map<DWRITE_FONT_FEATURE_TAG, uint32_t> GetFeatureAlternateCounts(hb_face_t* face);

	// Returns the features that shaping applies to text of the language when they are left at the default.
	std::set<DWRITE_FONT_FEATURE_TAG> GetDefaultOnFeatures(hb_face_t* face, const std::string& languageCode);
}

struct App::FontElementEditorDialog::ControlStruct {
	HWND Window;
	std::array<HWND, FontElementEditorDialogInternal::PageDialogIds.size()> Pages;

	// Finds a control either in the dialog or in one of the pages.
	[[nodiscard]] HWND Item(int id) const {
		for (const auto page : Pages) {
			if (const auto hwnd = GetDlgItem(page, id))
				return hwnd;
		}
		return GetDlgItem(Window, id);
	}

	HWND Tab = GetDlgItem(Window, IDC_TAB_FONTELEMENTEDITOR);
	HWND OkButton = Item(IDOK);
	HWND CancelButton = Item(IDCANCEL);
	HWND ApplyButton = Item(IDC_BUTTON_FONTELEMENTEDITOR_APPLY);
	HWND DeactivateCheck = Item(IDC_CHECK_FONTELEMENTEDITOR_DEACTIVATE);
	HWND FontSourceCombo = Item(IDC_COMBO_FONT_SOURCE);
	HWND FontCombo = Item(IDC_COMBO_FONT);
	HWND FontSizeEdit = Item(IDC_EDIT_FONT_SIZE);
	HWND FontWeightCombo = Item(IDC_COMBO_FONT_WEIGHT);
	HWND FontStyleCombo = Item(IDC_COMBO_FONT_STYLE);
	HWND FontStretchCombo = Item(IDC_COMBO_FONT_STRETCH);
	HWND FontFeaturesList = Item(IDC_LIST_FONT_FEATURES);
	HWND FontFeatureValueCombo = Item(IDC_COMBO_FONT_FEATURE_VALUE);
	HWND FontLanguageCombo = Item(IDC_COMBO_FONT_LANGUAGE);
	HWND FontVariationsList = Item(IDC_LIST_FONT_VARIATIONS);
	HWND FontVariationValueEdit = Item(IDC_EDIT_FONT_VARIATION_VALUE);
	HWND EmptyAscentEdit = Item(IDC_EDIT_EMPTY_ASCENT);
	HWND EmptyLineHeightEdit = Item(IDC_EDIT_EMPTY_LINEHEIGHT);
	HWND FreeTypeHintingCombo = Item(IDC_COMBO_FREETYPE_HINTING);
	HWND FreeTypeEmbeddedBitmapsCheck = Item(IDC_CHECK_FREETYPE_EMBEDDEDBITMAPS);
	HWND FreeTypeRenderModeCombo = Item(IDC_COMBO_FREETYPE_RENDERMODE);
	HWND DirectWriteRenderModeCombo = Item(IDC_COMBO_DIRECTWRITE_RENDERMODE);
	HWND DirectWriteMeasureModeCombo = Item(IDC_COMBO_DIRECTWRITE_MEASUREMODE);
	HWND DirectWriteGridFitModeCombo = Item(IDC_COMBO_DIRECTWRITE_GRIDFITMODE);
	HWND AdjustmentVerticalOffsetEdit = Item(IDC_EDIT_ADJUSTMENT_VERTICALOFFSET);
	HWND AdjustmentLetterSpacingEdit = Item(IDC_EDIT_ADJUSTMENT_LETTERSPACING);
	HWND AdjustmentHorizontalOffsetEdit = Item(IDC_EDIT_ADJUSTMENT_HORIZONTALOFFSET);
	HWND AdjustmentGammaEdit = Item(IDC_EDIT_ADJUSTMENT_GAMMA);
	HWND CodepointsList = Item(IDC_LIST_CODEPOINTS);
	HWND CodepointsClearButton = Item(IDC_BUTTON_CODEPOINTS_CLEAR);
	HWND CodepointsDeleteButton = Item(IDC_BUTTON_CODEPOINTS_DELETE);
	HWND CodepointsMergeModeCombo = Item(IDC_COMBO_CODEPOINTS_MERGEMODE);
	HWND UnicodeBlockSearchNameEdit = Item(IDC_EDIT_UNICODEBLOCKS_SEARCH);
	HWND UnicodeBlockSearchShowBlocksWithAnyOfCharactersInput = Item(IDC_CHECK_UNICODEBLOCKS_SHOWBLOCKSWITHANYOFCHARACTERSINPUT);
	HWND UnicodeBlockSearchResultList = Item(IDC_LIST_UNICODEBLOCKS_SEARCHRESULTS);
	HWND UnicodeBlockSearchSelectedPreviewEdit = Item(IDC_EDIT_UNICODEBLOCKS_RANGEPREVIEW);
	HWND UnicodeBlockSearchAddAll = Item(IDC_BUTTON_UNICODEBLOCKS_ADDALL);
	HWND UnicodeBlockSearchAdd = Item(IDC_BUTTON_UNICODEBLOCKS_ADD);
	HWND UnicodeBlockSearchSubtract = Item(IDC_BUTTON_UNICODEBLOCKS_SUBTRACT);
	HWND CustomRangeEdit = Item(IDC_EDIT_ADDCUSTOMRANGE_INPUT);
	HWND CustomRangePreview = Item(IDC_EDIT_ADDCUSTOMRANGE_PREVIEW);
	HWND CustomRangeAdd = Item(IDC_BUTTON_ADDCUSTOMRANGE_ADD);
	HWND CustomRangeSubtract = Item(IDC_BUTTON_ADDCUSTOMRANGE_SUBTRACT);
	HWND PresetRangeScriptCombo = Item(IDC_COMBO_PRESETRANGE_SCRIPT);
	HWND PresetRangeScriptAdd = Item(IDC_BUTTON_PRESETRANGE_SCRIPT_ADD);
	HWND PresetRangeScriptSubtract = Item(IDC_BUTTON_PRESETRANGE_SCRIPT_SUBTRACT);
	HWND PresetRangeGameFontCombo = Item(IDC_COMBO_PRESETRANGE_GAMEFONT);
	HWND PresetRangeGameFontAdd = Item(IDC_BUTTON_PRESETRANGE_GAMEFONT_ADD);
	HWND PresetRangeGameFontSubtract = Item(IDC_BUTTON_PRESETRANGE_GAMEFONT_SUBTRACT);
	// In the order of TransformEdit_OnCommand's index.
	HWND TransformScaleXEdit = Item(IDC_EDIT_TRANSFORM_SCALEX);
	HWND TransformScaleYEdit = Item(IDC_EDIT_TRANSFORM_SCALEY);
	HWND TransformSkewEdit = Item(IDC_EDIT_TRANSFORM_SKEW);
	HWND TransformRotationEdit = Item(IDC_EDIT_TRANSFORM_ROTATION);
	HWND FontAllowSynthesisCheck = Item(IDC_CHECK_FONT_ALLOWSYNTHESIS);
	HWND GlyphImagesPathEdit = Item(IDC_EDIT_GLYPHIMAGES_PATH);
	HWND GlyphImagesBrowseButton = Item(IDC_BUTTON_GLYPHIMAGES_BROWSE);
	HWND GlyphImagesEmbedButton = Item(IDC_BUTTON_GLYPHIMAGES_EMBED);
	HWND GlyphImagesUnitsPerEmEdit = Item(IDC_EDIT_GLYPHIMAGES_UNITSPEREM);
	HWND GlyphImagesBaselineEdit = Item(IDC_EDIT_GLYPHIMAGES_BASELINE);
	HWND GlyphImagesAscentEdit = Item(IDC_EDIT_GLYPHIMAGES_ASCENT);
	HWND GlyphImagesLineHeightEdit = Item(IDC_EDIT_GLYPHIMAGES_LINEHEIGHT);
	HWND GlyphImagesInfoStatic = Item(IDC_STATIC_GLYPHIMAGES_INFO);
	HWND MonospacingModeCombo = Item(IDC_COMBO_MONOSPACING_MODE);
	HWND MonospacingAlignmentCombo = Item(IDC_COMBO_MONOSPACING_ALIGNMENT);
	HWND MonospacingMinEdit = Item(IDC_EDIT_MONOSPACING_MIN);
	HWND MonospacingMaxEdit = Item(IDC_EDIT_MONOSPACING_MAX);
	HWND MonospacingUnitCombo = Item(IDC_COMBO_MONOSPACING_UNIT);
	HWND MonospacingReferenceEdit = Item(IDC_EDIT_MONOSPACING_REFERENCE);
	HWND MonospacingDropKerningCheck = Item(IDC_CHECK_MONOSPACING_DROPKERNING);
	HWND GlyphMergingTextSizeEdit = Item(IDC_EDIT_GLYPHMERGING_TEXTSIZE);
	HWND GlyphMergingFitCombo = Item(IDC_COMBO_GLYPHMERGING_FIT);
	HWND GlyphMergingOffsetXEdit = Item(IDC_EDIT_GLYPHMERGING_OFFSETX);
	HWND GlyphMergingOffsetYEdit = Item(IDC_EDIT_GLYPHMERGING_OFFSETY);
	HWND GlyphMergingScaleXEdit = Item(IDC_EDIT_GLYPHMERGING_SCALEX);
	HWND GlyphMergingScaleYEdit = Item(IDC_EDIT_GLYPHMERGING_SCALEY);
	HWND GlyphMergingSkewEdit = Item(IDC_EDIT_GLYPHMERGING_SKEW);
	HWND GlyphMergingRotationEdit = Item(IDC_EDIT_GLYPHMERGING_ROTATION);
	HWND GlyphMergingLetterSpacingEdit = Item(IDC_EDIT_GLYPHMERGING_LETTERSPACING);
	HWND GlyphMergingLineSpacingEdit = Item(IDC_EDIT_GLYPHMERGING_LINESPACING);
	HWND GlyphMergingLineAlignmentCombo = Item(IDC_COMBO_GLYPHMERGING_LINEALIGNMENT);
	HWND GlyphMergingMappingsList = Item(IDC_LIST_GLYPHMERGING_MAPPINGS);
	HWND GlyphMergingAddButton = Item(IDC_BUTTON_GLYPHMERGING_ADD);
	HWND GlyphMergingDeleteButton = Item(IDC_BUTTON_GLYPHMERGING_DELETE);
	HWND GlyphMergingCharactersEdit = Item(IDC_EDIT_GLYPHMERGING_CHARACTERS);
	HWND GlyphMergingTextsEdit = Item(IDC_EDIT_GLYPHMERGING_TEXTS);
	HWND GlyphMergingShapeCombo = Item(IDC_COMBO_GLYPHMERGING_SHAPE);
	HWND GlyphMergingImportSvgButton = Item(IDC_BUTTON_GLYPHMERGING_IMPORTSVG);
	HWND GlyphMergingTextModeCombo = Item(IDC_COMBO_GLYPHMERGING_TEXTMODE);
	std::array<HWND, FontElementEditorDialogInternal::GlyphMergingPresetCount> GlyphMergingPresetChecks = [this] {
		std::array<HWND, FontElementEditorDialogInternal::GlyphMergingPresetCount> res{};
		for (size_t i = 0; i < res.size(); i++)
			res[i] = Item(IDC_CHECK_GLYPHMERGING_PRESET_0 + static_cast<int>(i));
		return res;
	}();
};

template<typename T>
bool App::FontElementEditorDialog::TryEvaluate(const std::wstring& wstr, T& res, bool silent) {
	exprtk::expression<double> expr;
	exprtk::parser<double> parser;
	if (parser.compile(xivres::util::unicode::convert<std::string>(std::wstring_view(wstr).substr(1)), expr)) {
		if constexpr (std::is_integral_v<T>)
			res = static_cast<T>(std::round(expr.value()));
		else
			res = static_cast<T>(expr.value());
		return true;
	} else {
		std::wstring errors(GetStringResource(IDS_ERROR_MATHEXPREVAL_BODY));
		for (size_t i = 0; i < parser.error_count(); i++) {
			const auto error = parser.get_error(i);
			errors += xivres::util::unicode::convert<std::wstring>(
				std::format(
					"\n* {:02} [{}] {}",
					error.token.position,
					to_str(error.mode),
					error.diagnostic));
		}

		if (!silent) {
			MessageBoxW(
				m_controls->Window,
				errors.c_str(),
				std::wstring(GetStringResource(IDS_ERROR_MATHEXPREVAL_TITLE)).c_str(),
				MB_OK | MB_ICONWARNING);
		}
		return false;
	}
}

template<typename T>
bool App::FontElementEditorDialog::TryGetOrEvaluateValueInto(HWND hwnd, T& res, const T& originalValue) {
	auto wstr = GetWindowString(hwnd, true);

	// Edits show values with fewer digits than they may have, such as those of scaled elements; showing a value must not
	// change it to the shown one.
	if constexpr (std::is_floating_point_v<T>) {
		if (wstr == std::format(L"{:g}", res))
			return false;
	}

	T newValue = originalValue;
	if (!wstr.empty()) {
		if (!wstr.starts_with(L'='))
			newValue = static_cast<T>(std::wcstod(wstr.c_str(), nullptr));
		else
			void(TryEvaluate(wstr, newValue, true));
	}

	if (newValue == res)  // NOLINT(clang-diagnostic-float-equal)
		return false;

	res = newValue;
	return true;
}

#endif
