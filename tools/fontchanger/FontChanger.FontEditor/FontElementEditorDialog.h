#pragma once

#include "FontChanger.Presets/ElementFonts.h"

namespace App {
	class FontElementEditorDialog {
		struct ControlStruct;

		Structs::FontElement& m_element;
		Structs::FontElement m_elementOriginal;
		HWND m_hParentWnd;
		bool m_bOpened = false;
		std::function<void()> m_onFontChanged;

		// Called with whether the element is to be left out of the preview of the font.
		std::function<void(bool)> m_onDeactivatedChange;
		bool m_bDeactivated = false;

		bool m_bBaseFontChanged = false;
		bool m_bWrappedFontChanged = false;

		std::vector<IDWriteFontFamilyPtr> m_fontFamilies;

		struct FeatureRow {
			DWRITE_FONT_FEATURE_TAG Tag;

			// Number of alternates that the feature can choose from; the value of the feature picks one if above 1.
			uint32_t Alternates;

			// Whether shaping applies the feature when it is left at the default.
			bool DefaultOn;
		};

		// Features that the selected font supports.
		std::vector<FeatureRow> m_features;
		bool m_bSettingFeatureValueCombo = false;

		// Language tags of the items of the language combobox; empty for unspecified.
		std::vector<std::string> m_languageTags;

		struct VariationAxis {
			uint32_t Tag;
			std::wstring Name;
			float Minimum;
			float Maximum;
			float InstanceValue;
		};

		// Axes of the selected font, if it is a variable font.
		std::vector<VariationAxis> m_variationAxes;
		bool m_bSettingVariationValueEdit = false;

		// Names of a game font of each family in the Preset Range group, whose characters those of the family are, in the
		// order of its combobox.
		std::vector<std::string> m_gameFontNames;

		// Set while the Glyph merging page is filled in from the element, so that the changes are not taken as edits.
		bool m_bRefreshingGlyphMerging = false;

		enum class MonospacingMode : uint8_t {
			Off,
			Fixed,
			AtLeast,
			AtMost,
			Between,
		};

		// Kept apart from the element, which cannot tell Between with equal limits from Fixed.
		MonospacingMode m_monospacingMode = MonospacingMode::Off;
		bool m_bRefreshingMonospacing = false;

		// Limits of the value of an edit of a number, and the steps that Up and Down (and Ctrl with them) change it by.
		struct SpinRange {
			float Min;
			float Max;
			float Step;
			float FineStep;
		};
		std::map<HWND, SpinRange> m_spinRanges;

		ControlStruct* m_controls = nullptr;

	public:
		FontElementEditorDialog(HWND hParentWnd, Structs::FontElement& element, std::function<void()> onFontChanged, std::function<void(bool)> onDeactivatedChange);
		FontElementEditorDialog(FontElementEditorDialog&&) = delete;
		FontElementEditorDialog(const FontElementEditorDialog&) = delete;
		FontElementEditorDialog operator =(FontElementEditorDialog&&) = delete;
		FontElementEditorDialog operator =(const FontElementEditorDialog&) = delete;

		~FontElementEditorDialog();

		bool IsOpened() const;

		void Activate() const;

		bool ConsumeDialogMessage(MSG& msg);

		// Shows the fields of the element that are Auto as they are fit now: their edits empty, with the values as cue
		// banners, and the Auto item of the render mode, after the font they're fit in changed.
		void RefreshAutoFields();

		HWND m_hWnd{};

		template<typename T>
		friend INT_PTR __stdcall ::DlgProcStaticImpl(HWND, UINT, WPARAM, LPARAM);

	private:
		template<typename T>
		bool TryEvaluate(const std::wstring& wstr, T& res, bool silent = false);

		template<typename T>
		bool TryGetOrEvaluateValueInto(HWND hwnd, T& res, const T& originalValue);

		// Reads a number from an edit that may be left empty, which reads as nullopt; a text starting with = is evaluated.
		// Returns false if the text is not a finite number, as while it is being typed; the last valid value is then kept.
		bool TryReadOptionalNumber(HWND hwnd, std::optional<float>& value);

		// Whether the element's fields may be Auto: those of an element that FontsFromSystemFont::CanAutoFit fits.
		[[nodiscard]] bool CanBeAuto() const;

		// The Auto flag and the value of the field that an edit sets, if the field may be Auto; nullptr otherwise.
		[[nodiscard]] std::pair<bool*, float*> GetAutoField(HWND hwnd);

		// The value that Up and Down step from: that of the field, for an Auto field's edit that is left empty.
		[[nodiscard]] float GetSteppedValue(HWND hwnd);

		// Fills the render modes of DirectWrite in, after an Auto item for elements whose fields may be Auto.
		void RepopulateDirectWriteRenderModeCombo();

		INT_PTR OkButton_OnCommand(uint16_t notiCode);

		INT_PTR CancelButton_OnCommand(uint16_t notiCode);

		// Keeps the changes so far, which Cancel then no longer reverts.
		INT_PTR ApplyButton_OnCommand(uint16_t notiCode);

		INT_PTR DeactivateCheck_OnCommand(uint16_t notiCode);

		// Activates the element again if the dialog deactivated it, as the dialog closes.
		void Reactivate();

		INT_PTR FontSourceCombo_OnCommand(uint16_t notiCode);

		INT_PTR FontCombo_OnCommand(uint16_t notiCode);

		// Edits of numbers of the element: the size, the metrics of empty fonts, and the adjustments.
		INT_PTR NumberEdit_OnCommand(uint16_t id, uint16_t notiCode);

		// Comboboxes of settings of the element: the weight, style, and stretch, and the settings of the renderers.
		INT_PTR SettingCombo_OnCommand(uint16_t id, uint16_t notiCode);

		INT_PTR FontAllowSynthesisCheck_OnCommand(uint16_t notiCode);

		INT_PTR FontFeaturesList_OnItemChanged(const NMLISTVIEW& nmlv);

		INT_PTR FontFeaturesList_OnKeyDown(const NMLVKEYDOWN& nmkd);

		INT_PTR FontFeaturesList_OnDblClick(const NMITEMACTIVATE& nmia);

		INT_PTR FontFeaturesList_OnCustomDraw(const NMLVCUSTOMDRAW& nmcd);

		// screenPos is (-1, -1) if the menu is opened from the keyboard.
		INT_PTR FontFeaturesList_OnContextMenu(POINT screenPos);

		INT_PTR FontFeatureValueCombo_OnCommand(uint16_t notiCode);

		INT_PTR FontLanguageCombo_OnCommand(uint16_t notiCode);

		INT_PTR FontVariationsList_OnItemChanged(const NMLISTVIEW& nmlv);

		INT_PTR FontVariationValueEdit_OnCommand(uint16_t notiCode);

		INT_PTR FreeTypeHinting_OnCommand(uint16_t notiCode);

		// index: 0 for scale x, 1 for scale y, 2 for skew, and 3 for rotation.
		INT_PTR TransformEdit_OnCommand(int index, uint16_t notiCode);

		// Returns the transformation of the element as components, decomposing a matrix of an earlier version.
		[[nodiscard]] Structs::TransformStruct GetEditableTransform() const;

		void RefreshTransformEdits();

		// Controls of the SVG/PNG glyphs group of the Font page.
		INT_PTR GlyphImages_OnCommand(uint16_t id, uint16_t notiCode);

		INT_PTR GlyphImagesBrowseButton_OnCommand(uint16_t notiCode);

		INT_PTR GlyphImagesEmbedButton_OnCommand(uint16_t notiCode);

		void InitializeGlyphImagesGroup();
		void RefreshGlyphImagesGroup();
		void SetGlyphImagesControlsEnabled(bool enabled);

		// Set while the SVG/PNG glyphs group is filled in from the element.
		bool m_bRefreshingGlyphImages = false;

		// Controls of the Monospacing group of the Font page.
		INT_PTR Monospacing_OnCommand(uint16_t id, uint16_t notiCode);

		void InitializeMonospacingControls();

		// Sets the limits of the element from the mode and the width edits.
		void ApplyMonospacingWidths();

		void SetMonospacingControlsEnabled();

		INT_PTR CustomRangeEdit_OnCommand(uint16_t notiCode);

		INT_PTR CustomRangeAdd_OnCommand(uint16_t notiCode);

		INT_PTR CustomRangeSubtract_OnCommand(uint16_t notiCode);

		INT_PTR CodepointsList_OnCommand(uint16_t notiCode);

		INT_PTR CodepointsClearButton_OnCommand(uint16_t notiCode);

		INT_PTR CodepointsDeleteButton_OnCommand(uint16_t notiCode);

		INT_PTR CodepointsMergeModeCombo_OnCommand(uint16_t notiCode);

		INT_PTR UnicodeBlockSearchNameEdit_OnCommand(uint16_t notiCode);

		INT_PTR UnicodeBlockSearchShowBlocksWithAnyOfCharactersInput_OnCommand(uint16_t notiCode);

		INT_PTR UnicodeBlockSearchResultList_OnCommand(uint16_t notiCode);

		INT_PTR UnicodeBlockSearchAddAll_OnCommand(uint16_t notiCode);

		INT_PTR UnicodeBlockSearchAdd_OnCommand(uint16_t notiCode);

		INT_PTR UnicodeBlockSearchSubtract_OnCommand(uint16_t notiCode);

		// Adds or subtracts the codepoints of the script group chosen in the Preset Range group.
		INT_PTR PresetRangeScript_OnCommand(bool add, uint16_t notiCode);

		// Adds or subtracts the codepoints that the game font chosen in the Preset Range group has.
		INT_PTR PresetRangeGameFont_OnCommand(bool add, uint16_t notiCode);

		void InitializePresetRangeControls();

		INT_PTR ExpressionHelpButton_OnCommand(uint16_t notiCode);

		// Text placement controls of the Glyph merging page.
		INT_PTR GlyphMergingPlacement_OnCommand(uint16_t id, uint16_t notiCode);

		INT_PTR GlyphMergingMappingsList_OnItemChanged(const NMLISTVIEW& nmlv);

		INT_PTR GlyphMergingAddButton_OnCommand(uint16_t notiCode);

		INT_PTR GlyphMergingDeleteButton_OnCommand(uint16_t notiCode);

		// Controls that edit the selected mapping.
		INT_PTR GlyphMergingMappingEditor_OnCommand(uint16_t id, uint16_t notiCode);

		INT_PTR GlyphMergingImportSvgButton_OnCommand(uint16_t notiCode);

		INT_PTR GlyphMergingPresetCheck_OnCommand(size_t presetIndex, uint16_t notiCode);

		INT_PTR Dialog_OnInitDialog();

		void ShowPage(int index);

		std::vector<std::pair<char32_t, char32_t>> ParseCustomRangeString();

		void InitializeGlyphMergingPage();
		void RefreshGlyphMergingPlacement();
		void RefreshGlyphMergingMappingsList(int selectIndex);
		void UpdateGlyphMergingMappingsListItem(int index);
		void RefreshGlyphMergingMappingEditor();
		void RefreshGlyphMergingPresets();
		void SetGlyphMergingControlsEnabled(bool enabled);
		[[nodiscard]] int GetSelectedGlyphMergingMapping() const;
		// The codepoints of the element change as sets: the union with the ranges added, and the difference with those
		// subtracted, sorted with overlapping and adjacent ranges merged.
		void AddCodepointRanges(const std::vector<std::pair<char32_t, char32_t>>& ranges);
		void RemoveCodepointRanges(const std::vector<std::pair<char32_t, char32_t>>& ranges);
		void SetCodepointRanges(std::vector<std::pair<char32_t, char32_t>> ranges);
		void RefreshCodepointsList();
		void AddCodepointRangeToListBox(int index, char32_t c1, char32_t c2);
		void RefreshUnicodeBlockSearchResults();

		void SetControlsEnabledOrDisabled();
		void RepopulateFontCombobox();
		// Lists the weights, styles, and widths of the selected family, keeping the requested ones if they are listed.
		// snapToReal: whether to pick the closest real ones instead, as when the family changes.
		void RepopulateFontSubComboBox(bool snapToReal = false);
		void RefreshFontFeatureDefaults();
		void UpdateFontFeaturesListItem(int index);
		int GetSelectedFontFeature() const;

		// Values that a feature can be set to, in the order of the choices: std::nullopt for the default, then 1 (on),
		// 0 (off), and the alternates from 2.
		[[nodiscard]] std::vector<std::optional<uint32_t>> GetFontFeatureChoices(int index) const;
		[[nodiscard]] std::wstring GetFontFeatureChoiceText(int index, std::optional<uint32_t> value) const;
		[[nodiscard]] std::optional<uint32_t> GetFontFeatureValue(int index) const;
		void SetFontFeatureValue(int index, std::optional<uint32_t> value, bool refreshCombo = true);

		// Sets the feature to the choice after its current one, or back to the first.
		void CycleFontFeatureValue(int index);
		void RefreshFontFeatureValueCombo();
		void RepopulateFontLanguageCombobox();
		void RepopulateFontVariationsList();
		void UpdateFontVariationsListItem(int index);
		int GetSelectedFontVariationAxis() const;
		void RefreshFontVariationValueEdit();

		void OnBaseFontChanged();
		void OnWrappedFontChanged();

		INT_PTR DlgProc(UINT message, WPARAM wParam, LPARAM lParam);
		static INT_PTR __stdcall DlgProcStatic(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

		// Procedure of the pages of the tab control, which forwards notifications from their controls to the dialog.
		static INT_PTR __stdcall PageDlgProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
	};
}
