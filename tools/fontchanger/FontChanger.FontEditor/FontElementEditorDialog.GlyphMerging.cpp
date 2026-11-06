#include "pch.h"
#include "FontElementEditorDialog.Internal.h"
#include "FontChanger.Presets/CodepointRanges.h"

using namespace App::FontElementEditorDialogInternal;
using FontChanger::FixedSizeFont::glyph_merge_fit_mode;
using FontChanger::FixedSizeFont::glyph_merge_line_alignment;
using FontChanger::FixedSizeFont::glyph_merge_mapping;
using FontChanger::FixedSizeFont::glyph_merge_shape;
using FontChanger::FixedSizeFont::glyph_merge_text_mode;

namespace {
	constexpr GUID Guid_IFileDialog_GlyphMergingSvg{0x5c2fc703, 0x7406, 0x4704, {0x92, 0x12, 0xae, 0x41, 0x1d, 0x4b, 0x74, 0x70}};

	// Codepoints that are drawn this many times or more by a mapping are not worth typing in one by one.
	constexpr size_t MaxMappingCodepoints = 65536;

	// The text is drawn over these shapes instead of being cut out of them.
	bool IsDrawingShape(glyph_merge_shape shape) {
		return shape == glyph_merge_shape::None || shape == glyph_merge_shape::HollowBox;
	}

	// The shapes, in the order of the combobox, and their names.
	constexpr std::pair<glyph_merge_shape, UINT> ShapeNames[]{
		{glyph_merge_shape::None, IDS_GLYPHMERGING_SHAPE_NONE},
		{glyph_merge_shape::AmPm, IDS_GLYPHMERGING_SHAPE_AMPM},
		{glyph_merge_shape::Ime, IDS_GLYPHMERGING_SHAPE_IME},
		{glyph_merge_shape::Box, IDS_GLYPHMERGING_SHAPE_BOX},
		{glyph_merge_shape::NumberBox, IDS_GLYPHMERGING_SHAPE_NUMBERBOX},
		{glyph_merge_shape::HollowBox, IDS_GLYPHMERGING_SHAPE_HOLLOWBOX},
		{glyph_merge_shape::Hexagon, IDS_GLYPHMERGING_SHAPE_HEXAGON},
		{glyph_merge_shape::Bozja, IDS_GLYPHMERGING_SHAPE_BOZJA},
		{glyph_merge_shape::Time, IDS_GLYPHMERGING_SHAPE_TIME},
		{glyph_merge_shape::Star, IDS_GLYPHMERGING_SHAPE_STAR},
		{glyph_merge_shape::Custom, IDS_GLYPHMERGING_SHAPE_CUSTOM},
		{glyph_merge_shape::Glyph, IDS_GLYPHMERGING_SHAPE_GLYPH},
	};

	UINT GetShapeNameId(glyph_merge_shape shape) {
		const auto it = std::ranges::find(ShapeNames, shape, &std::pair<glyph_merge_shape, UINT>::first);
		return it == std::end(ShapeNames) ? IDS_GLYPHMERGING_SHAPE_CUSTOM : it->second;
	}

	// Texts may span multiple lines; line breaks in them are written as \n, so that each line of the edit is a text.
	std::wstring EscapeText(const std::u32string& text) {
		std::wstring res;
		for (const auto c : xivres::util::unicode::convert<std::wstring>(text)) {
			if (c == L'\\')
				res += L"\\\\";
			else if (c == L'\n')
				res += L"\\n";
			else
				res += c;
		}
		return res;
	}

	std::u32string UnescapeText(std::wstring_view text) {
		std::wstring res;
		for (size_t i = 0; i < text.size(); i++) {
			if (text[i] == L'\\' && i + 1 < text.size()) {
				if (text[i + 1] == L'n') {
					res += L'\n';
					i++;
					continue;
				}
				if (text[i + 1] == L'\\') {
					res += L'\\';
					i++;
					continue;
				}
			}
			res += text[i];
		}
		return xivres::util::unicode::convert<std::u32string>(res);
	}

	std::wstring FormatTexts(const std::vector<std::u32string>& texts, std::wstring_view separator) {
		std::wstring res;
		for (size_t i = 0; i < texts.size(); i++) {
			if (i)
				res += separator;
			res += EscapeText(texts[i]);
		}
		return res;
	}

	std::vector<std::u32string> ParseTexts(std::wstring_view str) {
		std::vector<std::u32string> res;
		for (size_t i = 0; i <= str.size();) {
			auto next = str.find(L'\n', i);
			if (next == std::wstring_view::npos)
				next = str.size();
			auto line = str.substr(i, next - i);
			if (line.ends_with(L'\r'))
				line = line.substr(0, line.size() - 1);
			res.emplace_back(UnescapeText(line));
			i = next + 1;
		}

		// Codepoints without texts are not drawn, as are ones with empty texts.
		while (!res.empty() && res.back().empty())
			res.pop_back();
		return res;
	}

	bool IsMapped(const std::vector<glyph_merge_mapping>& mappings, char32_t codepoint) {
		for (const auto& mapping : mappings) {
			for (size_t i = 0; i < mapping.Codepoints.size() && i < mapping.Texts.size(); i++) {
				if (mapping.Codepoints[i] == codepoint && !mapping.Texts[i].empty())
					return true;
			}
		}
		return false;
	}

	// Removes the codepoints from the mappings, along with their texts. Mappings left without any codepoint are removed.
	bool RemoveCodepointsFromMappings(std::vector<glyph_merge_mapping>& mappings, const std::u32string& codepoints) {
		auto changed = false;
		for (auto it = mappings.begin(); it != mappings.end();) {
			auto& mapping = *it;
			const auto wasEmpty = mapping.Codepoints.empty();
			for (auto i = mapping.Codepoints.size(); i-- > 0;) {
				if (codepoints.find(mapping.Codepoints[i]) == std::u32string::npos)
					continue;

				mapping.Codepoints.erase(i, 1);
				if (i < mapping.Texts.size())
					mapping.Texts.erase(mapping.Texts.begin() + static_cast<ptrdiff_t>(i));
				changed = true;
			}

			if (!wasEmpty && mapping.Codepoints.empty())
				it = mappings.erase(it);
			else
				++it;
		}
		return changed;
	}

	// Finds the value of an attribute in the tag starting at offset, or an empty view if there is none.
	std::string_view FindAttribute(std::string_view svg, size_t offset, std::string_view name) {
		const auto tagEnd = svg.find('>', offset);
		const auto tag = svg.substr(offset, tagEnd == std::string_view::npos ? std::string_view::npos : tagEnd - offset);
		for (size_t i = 0; (i = tag.find(name, i)) != std::string_view::npos; i += name.size()) {
			// The name must be a whole word: " d=" and not "id=".
			if (i == 0 || !std::isspace(static_cast<uint8_t>(tag[i - 1])))
				continue;

			auto j = i + name.size();
			while (j < tag.size() && std::isspace(static_cast<uint8_t>(tag[j])))
				j++;
			if (j >= tag.size() || tag[j] != '=')
				continue;
			j++;
			while (j < tag.size() && std::isspace(static_cast<uint8_t>(tag[j])))
				j++;
			if (j >= tag.size() || (tag[j] != '"' && tag[j] != '\''))
				continue;

			const auto quote = tag[j];
			const auto end = tag.find(quote, j + 1);
			if (end == std::string_view::npos)
				return {};
			return tag.substr(j + 1, end - j - 1);
		}
		return {};
	}
}

void App::FontElementEditorDialog::InitializeGlyphMergingPage() {
	ListView_SetExtendedListViewStyle(m_controls->GlyphMergingMappingsList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
	AddListViewColumn(m_controls->GlyphMergingMappingsList, 0, 64, IDS_GLYPHMERGING_COLUMN_CHARACTERS);
	AddListViewColumn(m_controls->GlyphMergingMappingsList, 1, 64, IDS_GLYPHMERGING_COLUMN_TEXTS);
	AddListViewColumn(m_controls->GlyphMergingMappingsList, 2, 48, IDS_GLYPHMERGING_COLUMN_SHAPE);
	AddListViewColumn(m_controls->GlyphMergingMappingsList, 3, 40, IDS_GLYPHMERGING_COLUMN_TEXT);

	const auto& params = m_element.GlyphMerging.Params;
	m_bRefreshingGlyphMerging = true;
	SetComboboxContent<glyph_merge_fit_mode>(
		m_controls->GlyphMergingFitCombo,
		params.FitMode,
		{
			std::make_pair(glyph_merge_fit_mode::CondenseThenShrink, IDS_GLYPHMERGING_FIT_CONDENSE),
			std::make_pair(glyph_merge_fit_mode::Shrink, IDS_GLYPHMERGING_FIT_SHRINK),
			std::make_pair(glyph_merge_fit_mode::Overflow, IDS_GLYPHMERGING_FIT_OVERFLOW),
		});
	SetComboboxContent<glyph_merge_line_alignment>(
		m_controls->GlyphMergingLineAlignmentCombo,
		params.LineAlignment,
		{
			std::make_pair(glyph_merge_line_alignment::Left, IDS_GLYPHMERGING_ALIGN_LEFT),
			std::make_pair(glyph_merge_line_alignment::Center, IDS_GLYPHMERGING_ALIGN_CENTER),
			std::make_pair(glyph_merge_line_alignment::Right, IDS_GLYPHMERGING_ALIGN_RIGHT),
		});
	SetComboboxContent<glyph_merge_shape>(m_controls->GlyphMergingShapeCombo, glyph_merge_shape::Box, ShapeNames);
	m_bRefreshingGlyphMerging = false;

	// An empty size fits the text in the shape.
	Edit_SetCueBannerTextFocused(m_controls->GlyphMergingTextSizeEdit, std::wstring(GetStringResource(IDS_FONTVARIATIONS_AUTO)).c_str(), TRUE);

	RefreshGlyphMergingPlacement();
	RefreshGlyphMergingMappingsList(-1);
	RefreshGlyphMergingPresets();
	SetGlyphMergingControlsEnabled(UsesSystemFont(m_element.Source));
}

void App::FontElementEditorDialog::RefreshGlyphMergingPlacement() {
	const auto& params = m_element.GlyphMerging.Params;
	const auto& transform = m_element.GlyphMerging.TextTransform;

	m_bRefreshingGlyphMerging = true;
	if (params.TextSize)
		SetWindowNumber(m_controls->GlyphMergingTextSizeEdit, *params.TextSize);
	else
		SetWindowTextW(m_controls->GlyphMergingTextSizeEdit, L"");
	SetWindowNumber(m_controls->GlyphMergingOffsetXEdit, params.TextOffsetX);
	SetWindowNumber(m_controls->GlyphMergingOffsetYEdit, params.TextOffsetY);
	SetWindowNumber(m_controls->GlyphMergingScaleXEdit, transform.ScaleX * 100.f);
	SetWindowNumber(m_controls->GlyphMergingScaleYEdit, transform.ScaleY * 100.f);
	SetWindowNumber(m_controls->GlyphMergingSkewEdit, transform.SkewDegrees);
	SetWindowNumber(m_controls->GlyphMergingRotationEdit, transform.RotationDegrees);
	SetWindowNumber(m_controls->GlyphMergingLetterSpacingEdit, params.LetterSpacing);
	SetWindowNumber(m_controls->GlyphMergingLineSpacingEdit, params.LineSpacing);
	m_bRefreshingGlyphMerging = false;
}

INT_PTR App::FontElementEditorDialog::GlyphMergingPlacement_OnCommand(uint16_t id, uint16_t notiCode) {
	if (m_bRefreshingGlyphMerging)
		return 0;

	auto& params = m_element.GlyphMerging.Params;
	auto& transform = m_element.GlyphMerging.TextTransform;
	switch (id) {
		case IDC_COMBO_GLYPHMERGING_FIT:
		case IDC_COMBO_GLYPHMERGING_LINEALIGNMENT: {
			if (notiCode != CBN_SELCHANGE)
				return 0;

			if (id == IDC_COMBO_GLYPHMERGING_FIT) {
				const auto value = GetComboboxSelData<glyph_merge_fit_mode>(m_controls->GlyphMergingFitCombo);
				if (value == params.FitMode)
					return 0;
				params.FitMode = value;
			} else {
				const auto value = GetComboboxSelData<glyph_merge_line_alignment>(m_controls->GlyphMergingLineAlignmentCombo);
				if (value == params.LineAlignment)
					return 0;
				params.LineAlignment = value;
			}
			break;
		}

		case IDC_EDIT_GLYPHMERGING_TEXTSIZE: {
			if (notiCode != EN_CHANGE)
				return 0;

			std::optional<float> size;
			if (!TryReadOptionalNumber(m_controls->GlyphMergingTextSizeEdit, size) || (size && !(*size > 0.f)))
				return 0;
			if (size == params.TextSize)
				return 0;
			params.TextSize = size;
			break;
		}

		default: {
			if (notiCode != EN_CHANGE)
				return 0;

			// Scales are shown in percent.
			const std::array<std::tuple<int, HWND, float*, float>, 8> fields{{
				{IDC_EDIT_GLYPHMERGING_OFFSETX, m_controls->GlyphMergingOffsetXEdit, &params.TextOffsetX, 1.f},
				{IDC_EDIT_GLYPHMERGING_OFFSETY, m_controls->GlyphMergingOffsetYEdit, &params.TextOffsetY, 1.f},
				{IDC_EDIT_GLYPHMERGING_SCALEX, m_controls->GlyphMergingScaleXEdit, &transform.ScaleX, 100.f},
				{IDC_EDIT_GLYPHMERGING_SCALEY, m_controls->GlyphMergingScaleYEdit, &transform.ScaleY, 100.f},
				{IDC_EDIT_GLYPHMERGING_SKEW, m_controls->GlyphMergingSkewEdit, &transform.SkewDegrees, 1.f},
				{IDC_EDIT_GLYPHMERGING_ROTATION, m_controls->GlyphMergingRotationEdit, &transform.RotationDegrees, 1.f},
				{IDC_EDIT_GLYPHMERGING_LETTERSPACING, m_controls->GlyphMergingLetterSpacingEdit, &params.LetterSpacing, 1.f},
				{IDC_EDIT_GLYPHMERGING_LINESPACING, m_controls->GlyphMergingLineSpacingEdit, &params.LineSpacing, 1.f},
			}};
			const auto it = std::ranges::find(fields, static_cast<int>(id), [](const auto& f) { return std::get<0>(f); });
			if (it == fields.end())
				return 0;

			const auto& [_, hwnd, field, factor] = *it;
			auto shown = *field * factor;
			if (!TryGetOrEvaluateValueInto(hwnd, shown, shown))
				return 0;

			// A zero scale would leave nothing to draw, and cannot be inverted by the renderers.
			if (factor != 1.f && shown == 0.f)  // NOLINT(clang-diagnostic-float-equal)
				return 0;
			*field = shown / factor;
			break;
		}
	}

	if (m_element.GlyphMerging.IsEnabled())
		OnBaseFontChanged();
	return 0;
}

void App::FontElementEditorDialog::RefreshGlyphMergingMappingsList(int selectIndex) {
	const auto& mappings = m_element.GlyphMerging.Params.Mappings;

	m_bRefreshingGlyphMerging = true;
	ListView_DeleteAllItems(m_controls->GlyphMergingMappingsList);
	for (int i = 0; i < static_cast<int>(mappings.size()); i++) {
		LVITEMW lvi{
			.mask = LVIF_TEXT,
			.iItem = i,
			.pszText = const_cast<wchar_t*>(L""),
		};
		ListView_InsertItem(m_controls->GlyphMergingMappingsList, &lvi);
		UpdateGlyphMergingMappingsListItem(i);
	}

	if (selectIndex >= 0 && selectIndex < static_cast<int>(mappings.size())) {
		ListView_SetItemState(m_controls->GlyphMergingMappingsList, selectIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		ListView_EnsureVisible(m_controls->GlyphMergingMappingsList, selectIndex, FALSE);
	}
	m_bRefreshingGlyphMerging = false;

	RefreshGlyphMergingMappingEditor();
}

void App::FontElementEditorDialog::UpdateGlyphMergingMappingsListItem(int index) {
	const auto& mapping = m_element.GlyphMerging.Params.Mappings[index];

	auto characters = FontChanger::CodepointRanges::Format(mapping.Codepoints);
	auto texts = FormatTexts(mapping.Texts, L" ");
	std::wstring shape(GetStringResource(GetShapeNameId(mapping.Shape)));
	std::wstring mode(GetStringResource(
		IsDrawingShape(mapping.Shape)
		? IDS_GLYPHMERGING_TEXTMODE_DRAW
		: mapping.TextMode == glyph_merge_text_mode::Difference
		? IDS_GLYPHMERGING_TEXTMODE_DIFFERENCE
		: IDS_GLYPHMERGING_TEXTMODE_SUBTRACT));

	ListView_SetItemText(m_controls->GlyphMergingMappingsList, index, 0, characters.data());
	ListView_SetItemText(m_controls->GlyphMergingMappingsList, index, 1, texts.data());
	ListView_SetItemText(m_controls->GlyphMergingMappingsList, index, 2, shape.data());
	ListView_SetItemText(m_controls->GlyphMergingMappingsList, index, 3, mode.data());
}

int App::FontElementEditorDialog::GetSelectedGlyphMergingMapping() const {
	const auto index = ListView_GetNextItem(m_controls->GlyphMergingMappingsList, -1, LVNI_SELECTED);
	return index >= 0 && index < static_cast<int>(m_element.GlyphMerging.Params.Mappings.size()) ? index : -1;
}

void App::FontElementEditorDialog::RefreshGlyphMergingMappingEditor() {
	const auto index = GetSelectedGlyphMergingMapping();
	const auto* mapping = index >= 0 ? &m_element.GlyphMerging.Params.Mappings[index] : nullptr;

	m_bRefreshingGlyphMerging = true;
	SetWindowTextW(m_controls->GlyphMergingCharactersEdit, mapping ? FontChanger::CodepointRanges::Format(mapping->Codepoints).c_str() : L"");
	SetWindowTextW(m_controls->GlyphMergingTextsEdit, mapping ? FormatTexts(mapping->Texts, L"\r\n").c_str() : L"");

	const auto shape = mapping ? mapping->Shape : glyph_merge_shape::Box;
	for (int i = 0, i_ = ComboBox_GetCount(m_controls->GlyphMergingShapeCombo); i < i_; i++) {
		if (static_cast<glyph_merge_shape>(ComboBox_GetItemData(m_controls->GlyphMergingShapeCombo, i)) == shape)
			ComboBox_SetCurSel(m_controls->GlyphMergingShapeCombo, i);
	}

	// Shapes that the text is drawn over have no choice of how the text is combined with them.
	if (IsDrawingShape(shape)) {
		ComboBox_ResetContent(m_controls->GlyphMergingTextModeCombo);
		ComboBox_AddString(m_controls->GlyphMergingTextModeCombo, std::wstring(GetStringResource(IDS_GLYPHMERGING_TEXTMODE_DRAW)).c_str());
		ComboBox_SetCurSel(m_controls->GlyphMergingTextModeCombo, 0);
	} else {
		SetComboboxContent<glyph_merge_text_mode>(
			m_controls->GlyphMergingTextModeCombo,
			mapping ? mapping->TextMode : glyph_merge_text_mode::Subtract,
			{
				std::make_pair(glyph_merge_text_mode::Subtract, IDS_GLYPHMERGING_TEXTMODE_SUBTRACT),
				std::make_pair(glyph_merge_text_mode::Difference, IDS_GLYPHMERGING_TEXTMODE_DIFFERENCE),
			});
	}
	m_bRefreshingGlyphMerging = false;

	const auto enabled = mapping && UsesSystemFont(m_element.Source);
	EnableWindow(m_controls->GlyphMergingDeleteButton, enabled);
	EnableWindow(m_controls->GlyphMergingCharactersEdit, enabled);
	EnableWindow(m_controls->GlyphMergingTextsEdit, enabled);
	EnableWindow(m_controls->GlyphMergingShapeCombo, enabled);
	EnableWindow(m_controls->GlyphMergingImportSvgButton, enabled && shape == glyph_merge_shape::Custom);
	EnableWindow(m_controls->GlyphMergingTextModeCombo, enabled && !IsDrawingShape(shape));
}

void App::FontElementEditorDialog::RefreshGlyphMergingPresets() {
	const auto& mappings = m_element.GlyphMerging.Params.Mappings;
	const auto& characters = m_element.WrapModifiers.Codepoints;
	const auto& presets = FontChanger::GlyphMergingPresets::Get();
	for (size_t i = 0; i < presets.size(); i++) {
		size_t mapped = 0, included = 0;
		for (const auto c : presets[i].Codepoints) {
			if (!IsMapped(mappings, c))
				continue;
			mapped++;
			if (FontChanger::CodepointRanges::Contains(characters, c))
				included++;
		}

		Button_SetCheck(
			m_controls->GlyphMergingPresetChecks[i],
			included == presets[i].Codepoints.size()
			? BST_CHECKED
			: mapped == 0
			? BST_UNCHECKED
			: BST_INDETERMINATE);
	}
}

void App::FontElementEditorDialog::SetGlyphMergingControlsEnabled(bool enabled) {
	for (const auto hwnd : {
		     m_controls->GlyphMergingTextSizeEdit,
		     m_controls->GlyphMergingFitCombo,
		     m_controls->GlyphMergingOffsetXEdit,
		     m_controls->GlyphMergingOffsetYEdit,
		     m_controls->GlyphMergingScaleXEdit,
		     m_controls->GlyphMergingScaleYEdit,
		     m_controls->GlyphMergingSkewEdit,
		     m_controls->GlyphMergingRotationEdit,
		     m_controls->GlyphMergingLetterSpacingEdit,
		     m_controls->GlyphMergingLineSpacingEdit,
		     m_controls->GlyphMergingLineAlignmentCombo,
		     m_controls->GlyphMergingMappingsList,
		     m_controls->GlyphMergingAddButton,
	     })
		EnableWindow(hwnd, enabled);
	for (const auto hwnd : m_controls->GlyphMergingPresetChecks)
		EnableWindow(hwnd, enabled);

	RefreshGlyphMergingMappingEditor();
}

INT_PTR App::FontElementEditorDialog::GlyphMergingMappingsList_OnItemChanged(const NMLISTVIEW& nmlv) {
	if (!m_bRefreshingGlyphMerging && (nmlv.uChanged & LVIF_STATE) && ((nmlv.uNewState ^ nmlv.uOldState) & LVIS_SELECTED))
		RefreshGlyphMergingMappingEditor();
	return 0;
}

INT_PTR App::FontElementEditorDialog::GlyphMergingAddButton_OnCommand(uint16_t notiCode) {
	auto& mappings = m_element.GlyphMerging.Params.Mappings;
	const auto selected = GetSelectedGlyphMergingMapping();
	const auto index = selected >= 0 ? selected + 1 : static_cast<int>(mappings.size());
	mappings.insert(mappings.begin() + index, glyph_merge_mapping{});

	RefreshGlyphMergingMappingsList(index);
	SetFocus(m_controls->GlyphMergingCharactersEdit);
	OnBaseFontChanged();
	return 0;
}

INT_PTR App::FontElementEditorDialog::GlyphMergingDeleteButton_OnCommand(uint16_t notiCode) {
	auto& mappings = m_element.GlyphMerging.Params.Mappings;
	const auto index = GetSelectedGlyphMergingMapping();
	if (index < 0)
		return 0;

	mappings.erase(mappings.begin() + index);
	RefreshGlyphMergingMappingsList((std::min)(index, static_cast<int>(mappings.size()) - 1));
	OnBaseFontChanged();
	return 0;
}

INT_PTR App::FontElementEditorDialog::GlyphMergingMappingEditor_OnCommand(uint16_t id, uint16_t notiCode) {
	if (m_bRefreshingGlyphMerging)
		return 0;

	const auto index = GetSelectedGlyphMergingMapping();
	if (index < 0)
		return 0;

	auto& mapping = m_element.GlyphMerging.Params.Mappings[index];
	switch (id) {
		case IDC_EDIT_GLYPHMERGING_CHARACTERS: {
			if (notiCode != EN_CHANGE)
				return 0;

			std::u32string codepoints;
			for (const auto& [c1, c2] : FontChanger::CodepointRanges::Parse(GetWindowString(m_controls->GlyphMergingCharactersEdit))) {
				for (auto c = c1; c <= c2 && codepoints.size() < MaxMappingCodepoints; c++)
					codepoints.push_back(c);
			}
			if (codepoints == mapping.Codepoints)
				return 0;
			mapping.Codepoints = std::move(codepoints);
			break;
		}

		case IDC_EDIT_GLYPHMERGING_TEXTS: {
			if (notiCode != EN_CHANGE)
				return 0;

			auto texts = ParseTexts(GetWindowString(m_controls->GlyphMergingTextsEdit));
			if (texts == mapping.Texts)
				return 0;
			mapping.Texts = std::move(texts);
			break;
		}

		case IDC_COMBO_GLYPHMERGING_SHAPE: {
			if (notiCode != CBN_SELCHANGE)
				return 0;

			const auto shape = static_cast<glyph_merge_shape>(ComboBox_GetItemData(m_controls->GlyphMergingShapeCombo, ComboBox_GetCurSel(m_controls->GlyphMergingShapeCombo)));
			if (shape == mapping.Shape)
				return 0;
			mapping.Shape = shape;

			// The choices of the text mode depend on the shape.
			RefreshGlyphMergingMappingEditor();
			break;
		}

		case IDC_COMBO_GLYPHMERGING_TEXTMODE: {
			if (notiCode != CBN_SELCHANGE || IsDrawingShape(mapping.Shape))
				return 0;

			const auto mode = static_cast<glyph_merge_text_mode>(ComboBox_GetItemData(m_controls->GlyphMergingTextModeCombo, ComboBox_GetCurSel(m_controls->GlyphMergingTextModeCombo)));
			if (mode == mapping.TextMode)
				return 0;
			mapping.TextMode = mode;
			break;
		}

		default:
			return 0;
	}

	UpdateGlyphMergingMappingsListItem(index);
	OnBaseFontChanged();
	return 0;
}

INT_PTR App::FontElementEditorDialog::GlyphMergingImportSvgButton_OnCommand(uint16_t notiCode) {
	const auto index = GetSelectedGlyphMergingMapping();
	if (index < 0)
		return 0;

	const auto filterName = std::wstring(GetStringResource(IDS_GLYPHMERGING_IMPORTSVG_FILTER));
	const COMDLG_FILTERSPEC fileTypes[] = {
		{filterName.c_str(), L"*.svg"},
	};

	return TryCatchShowError(m_controls->Window, IDS_ERROR_OPENFILEFAILURE_BODY, INT_PTR{0}, [&]() -> INT_PTR {
		const auto fileName = PickFile(m_controls->Window, false, Guid_IFileDialog_GlyphMergingSvg, IDS_WINDOWTITLE_OPEN, fileTypes);
		if (!fileName)
			return 0;

		std::string svg;
		{
			std::ifstream in(*fileName, std::ios::binary);
			if (!in)
				throw std::runtime_error("Failed to open the file.");
			svg.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		}

		// The paths are taken in the coordinates of the shapes: 1/1000 em, with the baseline at y = 880.
		std::string path;
		for (size_t offset = 0; (offset = svg.find("<path", offset)) != std::string::npos; offset += 5) {
			if (const auto d = FindAttribute(svg, offset + 5, "d"); !d.empty()) {
				if (!path.empty())
					path += ' ';
				path += d;
			}
		}

		FontChanger::FixedSizeFont::image_fixed_size_font::svg_metrics metrics;
		if (!FontChanger::FixedSizeFont::image_fixed_size_font::try_read_svg_metrics(svg, metrics)) {
			MessageBoxW(
				m_controls->Window,
				std::wstring(GetStringResource(IDS_GLYPHMERGING_IMPORTSVG_FAILED)).c_str(),
				GetWindowString(m_controls->Window).c_str(),
				MB_OK | MB_ICONWARNING);
			return 0;
		}

		// The width of the drawing is the advance of the glyph.
		auto& mapping = m_element.GlyphMerging.Params.Mappings[index];
		if (metrics.Advance && *metrics.Advance > 0)
			mapping.CustomAdvance = *metrics.Advance;

		// The document is drawn as a whole, so that each element is filled by itself as SVG does; the joined paths are
		// kept for earlier versions, which fill them together.
		mapping.CustomPath = std::move(path);
		mapping.CustomSvg = std::move(svg);
		mapping.Shape = glyph_merge_shape::Custom;
		UpdateGlyphMergingMappingsListItem(index);
		OnBaseFontChanged();
		return 0;
	});
}

INT_PTR App::FontElementEditorDialog::GlyphMergingPresetCheck_OnCommand(size_t presetIndex, uint16_t notiCode) {
	if (notiCode != BN_CLICKED || presetIndex >= GlyphMergingPresetCount)
		return 0;

	const auto& preset = FontChanger::GlyphMergingPresets::Get()[presetIndex];
	auto& mappings = m_element.GlyphMerging.Params.Mappings;

	if (Button_GetCheck(m_controls->GlyphMergingPresetChecks[presetIndex]) == BST_CHECKED) {
		// Unticking leaves the codepoints in the Characters page, where they are not drawn without a mapping.
		if (!RemoveCodepointsFromMappings(mappings, preset.Codepoints))
			return 0;
	} else {
		RemoveCodepointsFromMappings(mappings, preset.Codepoints);
		mappings.insert(mappings.end(), preset.Mappings.begin(), preset.Mappings.end());

		// The element draws only the codepoints in the Characters page.
		AddCodepointRanges(FontChanger::CodepointRanges::FromCodepoints(preset.Codepoints));
	}

	RefreshGlyphMergingMappingsList(-1);
	OnBaseFontChanged();
	return 0;
}
