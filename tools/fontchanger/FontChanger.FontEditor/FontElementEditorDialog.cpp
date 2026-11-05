#include "pch.h"
#include "FontElementEditorDialog.Internal.h"

#include <Uxtheme.h>

#pragma comment(lib, "uxtheme.lib")

using namespace App::FontElementEditorDialogInternal;

namespace {
	// The page shown when the dialog opens: the one last shown.
	int s_lastPageIndex = 0;
}

App::FontElementEditorDialog::FontElementEditorDialog(HWND hParentWnd, Structs::FontElement& element, std::function<void()> onFontChanged, std::function<void(bool)> onDeactivatedChange)
	: m_element(element)
	, m_elementOriginal(element)
	, m_hParentWnd(hParentWnd)
	, m_onFontChanged(std::move(onFontChanged))
	, m_onDeactivatedChange(std::move(onDeactivatedChange)) {
	const auto hglob = LoadResourceWithLanguageFallback(RT_DIALOG, IDD_FONTELEMENTEDITOR);
	CreateDialogIndirectParamW(
		g_hInstance,
		static_cast<DLGTEMPLATE*>(LockResource(hglob.get())),
		m_hParentWnd,
		DlgProcStatic,
		reinterpret_cast<LPARAM>(this));
}

App::FontElementEditorDialog::~FontElementEditorDialog() {
	delete m_controls;
}

bool App::FontElementEditorDialog::IsOpened() const {
	return m_bOpened;
}

void App::FontElementEditorDialog::Activate() const {
	BringWindowToTop(m_controls->Window);
}

bool App::FontElementEditorDialog::ConsumeDialogMessage(MSG& msg) {
	if (!m_controls)
		return false;

	// Ctrl+(Shift+)Tab and Ctrl+PgUp/PgDn switch pages, as in property sheets.
	if (msg.message == WM_KEYDOWN
		&& (msg.wParam == VK_TAB || msg.wParam == VK_PRIOR || msg.wParam == VK_NEXT)
		&& GetKeyState(VK_CONTROL) < 0
		&& (msg.hwnd == m_controls->Window || IsChild(m_controls->Window, msg.hwnd))) {
		const auto count = TabCtrl_GetItemCount(m_controls->Tab);
		const auto backward = msg.wParam == VK_PRIOR || (msg.wParam == VK_TAB && GetKeyState(VK_SHIFT) < 0);
		const auto index = (TabCtrl_GetCurSel(m_controls->Tab) + (backward ? count - 1 : 1)) % count;
		TabCtrl_SetCurSel(m_controls->Tab, index);
		ShowPage(index);
		SetFocus(m_controls->Tab);
		return true;
	}

	return IsDialogMessage(m_controls->Window, &msg);
}

INT_PTR App::FontElementEditorDialog::OkButton_OnCommand(uint16_t notiCode) {
	Reactivate();
	// A modeless dialog: EndDialog would only hide it, leaving its windows behind.
	DestroyWindow(m_controls->Window);
	m_bOpened = false;
	return 0;
}

INT_PTR App::FontElementEditorDialog::CancelButton_OnCommand(uint16_t notiCode) {
	Reactivate();
	m_element = std::move(m_elementOriginal);

	// Before the controls they update are destroyed.
	if (m_bBaseFontChanged)
		OnBaseFontChanged();
	else if (m_bWrappedFontChanged)
		OnWrappedFontChanged();

	DestroyWindow(m_controls->Window);
	m_bOpened = false;
	return 0;
}

INT_PTR App::FontElementEditorDialog::ApplyButton_OnCommand(uint16_t notiCode) {
	m_elementOriginal = m_element;
	m_bBaseFontChanged = false;
	m_bWrappedFontChanged = false;

	// Disabling the focused button would leave the dialog without focus.
	if (GetFocus() == m_controls->ApplyButton)
		SendMessageW(m_controls->Window, WM_NEXTDLGCTL, reinterpret_cast<WPARAM>(m_controls->OkButton), TRUE);
	EnableWindow(m_controls->ApplyButton, FALSE);
	return 0;
}

INT_PTR App::FontElementEditorDialog::DeactivateCheck_OnCommand(uint16_t notiCode) {
	if (notiCode != BN_CLICKED)
		return 0;

	m_bDeactivated = Button_GetCheck(m_controls->DeactivateCheck) == BST_CHECKED;
	if (m_onDeactivatedChange)
		m_onDeactivatedChange(m_bDeactivated);
	return 0;
}

void App::FontElementEditorDialog::Reactivate() {
	if (!m_bDeactivated)
		return;

	m_bDeactivated = false;
	if (m_controls)
		Button_SetCheck(m_controls->DeactivateCheck, BST_UNCHECKED);
	if (m_onDeactivatedChange)
		m_onDeactivatedChange(false);
}

bool App::FontElementEditorDialog::TryReadOptionalNumber(HWND hwnd, std::optional<float>& value) {
	const auto str = GetWindowString(hwnd, true);
	if (str.empty()) {
		value.reset();
		return true;
	}

	float v;
	if (str.starts_with(L'=')) {
		if (!TryEvaluate(str, v, true))
			return false;
	} else {
		wchar_t* end;
		v = std::wcstof(str.c_str(), &end);
		if (end == str.c_str() || *end)
			return false;
	}
	if (!std::isfinite(v))
		return false;

	value = v;
	return true;
}

INT_PTR App::FontElementEditorDialog::ExpressionHelpButton_OnCommand(uint16_t notiCode) {
	ShellExecuteW(
		m_controls->Window,
		L"open",
		std::wstring(GetStringResource(IDS_URL_MATHEXPRHELP)).c_str(),
		nullptr,
		nullptr,
		SW_SHOW);
	return 0;
}

INT_PTR App::FontElementEditorDialog::Dialog_OnInitDialog() {
	std::array<HWND, PageDialogIds.size()> pages{};
	{
		const auto tab = GetDlgItem(m_hWnd, IDC_TAB_FONTELEMENTEDITOR);
		for (size_t i = 0; i < pages.size(); i++) {
			std::wstring name(GetStringResource(PageNameIds[i]));
			TCITEMW tci{ .mask = TCIF_TEXT, .pszText = name.data() };
			TabCtrl_InsertItem(tab, static_cast<int>(i), &tci);
		}

		// The area below the row of tabs, which exists only after the tabs are inserted.
		RECT rc;
		GetWindowRect(tab, &rc);
		MapWindowPoints(nullptr, m_hWnd, reinterpret_cast<POINT*>(&rc), 2);
		TabCtrl_AdjustRect(tab, FALSE, &rc);

		for (size_t i = 0; i < pages.size(); i++) {
			const auto hglob = LoadResourceWithLanguageFallback(RT_DIALOG, PageDialogIds[i]);
			pages[i] = CreateDialogIndirectParamW(g_hInstance, static_cast<DLGTEMPLATE*>(LockResource(hglob.get())), m_hWnd, PageDlgProc, 0);
			EnableThemeDialogTexture(pages[i], ETDT_ENABLETAB);

			// Right after the tab control in the Z order, so that the controls of the page come next in the tab order.
			SetWindowPos(pages[i], tab, rc.left, rc.top, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
		}
	}

	m_controls = new ControlStruct{ m_hWnd, pages };
	m_bOpened = true;
	TabCtrl_SetCurSel(m_controls->Tab, s_lastPageIndex);
	ShowPage(s_lastPageIndex);

	ListView_SetExtendedListViewStyle(m_controls->FontFeaturesList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
	{
		// The name takes the width that the value column and the scroll bar leave.
		RECT rc;
		GetClientRect(m_controls->FontFeaturesList, &rc);
		const auto zoom = GetZoomFromWindow(m_controls->FontFeaturesList);
		const auto valueWidth = static_cast<int>(64 * zoom);
		AddListViewColumn(m_controls->FontFeaturesList, 0, static_cast<int>((rc.right - valueWidth - GetSystemMetrics(SM_CXVSCROLL)) / zoom), IDS_FONTFEATURES_COLUMN_FEATURE);
		AddListViewColumn(m_controls->FontFeaturesList, 1, 64, IDS_FONTVARIATIONS_COLUMN_VALUE);
	}
	ListView_SetExtendedListViewStyle(m_controls->FontVariationsList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
	AddListViewColumn(m_controls->FontVariationsList, 0, 100, IDS_FONTVARIATIONS_COLUMN_AXIS);
	AddListViewColumn(m_controls->FontVariationsList, 1, 56, IDS_FONTVARIATIONS_COLUMN_RANGE);
	AddListViewColumn(m_controls->FontVariationsList, 2, 48, IDS_FONTVARIATIONS_COLUMN_VALUE);

	// Elements of earlier versions get the explicit model of synthesis, drawn as before; Cancel reverts this too.
	ElementFonts::ConvertToExplicitSynthesis(m_element.Lookup, m_element.Source);

	RepopulateFontLanguageCombobox();
	SetControlsEnabledOrDisabled();
	RepopulateFontCombobox();
	RefreshUnicodeBlockSearchResults();
	SetComboboxContent<Structs::ElementSourceEnum>(
		m_controls->FontSourceCombo,
		m_element.Source,
		{
			std::make_pair(Structs::ElementSourceEnum::Empty, IDS_ELEMENTSOURCE_EMPTY),
			std::make_pair(Structs::ElementSourceEnum::Game, IDS_ELEMENTSOURCE_GAME),
			std::make_pair(Structs::ElementSourceEnum::DirectWrite, IDS_ELEMENTSOURCE_DIRECTWRITE),
			std::make_pair(Structs::ElementSourceEnum::FreeType, IDS_ELEMENTSOURCE_FREETYPE),
			std::make_pair(Structs::ElementSourceEnum::GlyphImages, IDS_ELEMENTSOURCE_GLYPHIMAGES),
		});
	ComboBox_SetText(m_controls->FontCombo, xivres::util::unicode::convert<std::wstring>(m_element.Lookup.FamilyName).c_str());
	SetWindowNumber(m_controls->EmptyAscentEdit, m_element.RendererSpecific.Empty.Ascent);
	SetWindowNumber(m_controls->EmptyLineHeightEdit, m_element.RendererSpecific.Empty.LineHeight);
	{
		const auto loadFlags = m_element.RendererSpecific.FreeType.LoadFlags;
		const auto hinting = (loadFlags & FT_LOAD_NO_HINTING)
			? FT_LOAD_NO_HINTING
			: (loadFlags & FT_LOAD_NO_AUTOHINT)
			? FT_LOAD_NO_AUTOHINT
			: (loadFlags & FT_LOAD_FORCE_AUTOHINT)
			? FT_LOAD_FORCE_AUTOHINT
			: FT_LOAD_DEFAULT;
		SetComboboxContent<int>(
			m_controls->FreeTypeHintingCombo,
			hinting,
			{
				std::make_pair(FT_LOAD_DEFAULT, IDS_FREETYPE_HINTING_DEFAULT),
				std::make_pair(FT_LOAD_NO_AUTOHINT, IDS_FREETYPE_HINTING_NATIVE),
				std::make_pair(FT_LOAD_FORCE_AUTOHINT, IDS_FREETYPE_HINTING_AUTOHINTER),
				std::make_pair(FT_LOAD_NO_HINTING, IDS_FREETYPE_HINTING_NONE),
			});
		Button_SetCheck(m_controls->FreeTypeEmbeddedBitmapsCheck, (loadFlags & FT_LOAD_NO_BITMAP) ? FALSE : TRUE);
	}
	
	SetComboboxContent<FT_Render_Mode>(
		m_controls->FreeTypeRenderModeCombo,
		m_element.RendererSpecific.FreeType.RenderMode,
		{
			std::make_pair(FT_RENDER_MODE_NORMAL, IDS_FREETYPE_RENDERMODE_NORMAL),
			std::make_pair(FT_RENDER_MODE_LIGHT, IDS_FREETYPE_RENDERMODE_LIGHT),
			std::make_pair(FT_RENDER_MODE_MONO, IDS_FREETYPE_RENDERMODE_MONO),
		});
	
	RepopulateDirectWriteRenderModeCombo();

	SetComboboxContent<DWRITE_MEASURING_MODE>(
		m_controls->DirectWriteMeasureModeCombo,
		m_element.RendererSpecific.DirectWrite.MeasureMode,
		{
			std::make_pair(DWRITE_MEASURING_MODE_NATURAL, IDS_DIRECTWRITE_MEASUREMODE_NATURAL),
			std::make_pair(DWRITE_MEASURING_MODE_GDI_CLASSIC, IDS_DIRECTWRITE_MEASUREMODE_GDI_CLASSIC),
			std::make_pair(DWRITE_MEASURING_MODE_GDI_NATURAL, IDS_DIRECTWRITE_MEASUREMODE_GDI_NATURAL),
		});
	
	SetComboboxContent<DWRITE_GRID_FIT_MODE>(
		m_controls->DirectWriteGridFitModeCombo,
		m_element.RendererSpecific.DirectWrite.GridFitMode,
		{
			std::make_pair(DWRITE_GRID_FIT_MODE_DEFAULT, IDS_DIRECTWRITE_GRIDFITMODE_DEFAULT),
			std::make_pair(DWRITE_GRID_FIT_MODE_DISABLED, IDS_DIRECTWRITE_GRIDFITMODE_DISABLED),
			std::make_pair(DWRITE_GRID_FIT_MODE_ENABLED, IDS_DIRECTWRITE_GRIDFITMODE_ENABLED),
		});

	// The size and the offsets, which may be Auto, are shown by RefreshAutoFields.
	SetWindowNumber(m_controls->AdjustmentLetterSpacingEdit, m_element.WrapModifiers.LetterSpacing);
	SetWindowNumber(m_controls->AdjustmentGammaEdit, m_element.Gamma);
	RefreshAutoFields();

	RefreshCodepointsList();
	InitializePresetRangeControls();

	SetComboboxContent<FontChanger::FixedSizeFont::codepoint_merge_mode>(
		m_controls->CodepointsMergeModeCombo,
		m_element.MergeMode,
		{
			std::make_pair(FontChanger::FixedSizeFont::codepoint_merge_mode::AddNew, IDS_CODEPOINTMERGEMODE_ADDNEW),
			std::make_pair(FontChanger::FixedSizeFont::codepoint_merge_mode::AddAll, IDS_CODEPOINTMERGEMODE_ADDALL),
			std::make_pair(FontChanger::FixedSizeFont::codepoint_merge_mode::Replace, IDS_CODEPOINTMERGEMODE_REPLACE),
		});

	RefreshTransformEdits();
	InitializeGlyphImagesGroup();
	InitializeMonospacingControls();
	InitializeGlyphMergingPage();

	// Edits of numbers, which Up and Down change by a step; Ctrl changes them by a finer step.
	const auto setSpinRange = [this](SpinRange range, std::initializer_list<HWND> edits) {
		for (const auto edit : edits)
			m_spinRanges.emplace(edit, range);
	};
	// Values in pixels, which may be fractional after scaling.
	setSpinRange({-128.f, 127.f, 1.f, 0.1f}, {
		m_controls->EmptyAscentEdit,
		m_controls->EmptyLineHeightEdit,
		m_controls->AdjustmentVerticalOffsetEdit,
		m_controls->AdjustmentLetterSpacingEdit,
		m_controls->AdjustmentHorizontalOffsetEdit,
	});
	setSpinRange({1.f, 3.f, 0.1f, 0.1f}, {m_controls->AdjustmentGammaEdit});
	setSpinRange({8.f, 255.f, 1.f, 0.1f}, {m_controls->FontSizeEdit});
	setSpinRange({-1000.f, 1000.f, 1.f, 0.1f}, {
		m_controls->TransformScaleXEdit,
		m_controls->TransformScaleYEdit,
		m_controls->TransformSkewEdit,
		m_controls->TransformRotationEdit,
		m_controls->GlyphMergingOffsetXEdit,
		m_controls->GlyphMergingOffsetYEdit,
		m_controls->GlyphMergingScaleXEdit,
		m_controls->GlyphMergingScaleYEdit,
		m_controls->GlyphMergingSkewEdit,
		m_controls->GlyphMergingRotationEdit,
		m_controls->GlyphMergingLetterSpacingEdit,
		m_controls->GlyphMergingLineSpacingEdit,
		m_controls->MonospacingMinEdit,
		m_controls->MonospacingMaxEdit,
	});

	for (const auto& edit : m_spinRanges | std::views::keys) {
		SetWindowSubclass(edit, [](HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) -> LRESULT {
			auto& dlg = *reinterpret_cast<FontElementEditorDialog*>(dwRefData);
			const auto& range = dlg.m_spinRanges.at(hWnd);
			const auto step = GetKeyState(VK_CONTROL) & 0x8000 ? range.FineStep : range.Step;
			if (msg == WM_KEYDOWN && wParam == VK_DOWN && !GetWindowString(hWnd).starts_with(L"=")) {
				SetWindowNumber(hWnd, (std::max)(range.Min, dlg.GetSteppedValue(hWnd) + step));
				return 0;
			} else if (msg == WM_KEYDOWN && wParam == VK_UP && !GetWindowString(hWnd).starts_with(L"=")) {
				SetWindowNumber(hWnd, (std::min)(range.Max, dlg.GetSteppedValue(hWnd) - step));
				return 0;
			} else if (msg == WM_GETDLGCODE && wParam == VK_RETURN && GetWindowString(hWnd).starts_with(L"=")) {
				return DefSubclassProc(hWnd, msg, wParam, lParam) | DLGC_WANTALLKEYS;
			} else if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
				if (const auto wstr = GetWindowString(hWnd); wstr.starts_with(L"=")) {
					if (float r; dlg.TryEvaluate(wstr, r))
						SetWindowNumber(hWnd, (std::min)(range.Max, (std::max)(range.Min, r)));
					return 0;
				}
			}

			return DefSubclassProc(hWnd, msg, wParam, lParam);
		}, 1, reinterpret_cast<DWORD_PTR>(this));
	}

	// Edits that hold the x and y of a pair of values also take W, A, S, and D, which change the values as the arrow keys do:
	// A and D change x, and W and S change y as Up and Down do. Keys are taken by their positions on the keyboard.
	for (const auto& controlHwnd : {
		     m_controls->AdjustmentHorizontalOffsetEdit,
		     m_controls->AdjustmentVerticalOffsetEdit,
		     m_controls->GlyphMergingOffsetXEdit,
		     m_controls->GlyphMergingOffsetYEdit,
		     m_controls->GlyphMergingScaleXEdit,
		     m_controls->GlyphMergingScaleYEdit,
		     m_controls->GlyphMergingLetterSpacingEdit,
		     m_controls->GlyphMergingLineSpacingEdit,
	     }) {
		SetWindowSubclass(controlHwnd, [](HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) -> LRESULT {
			if ((msg != WM_KEYDOWN && msg != WM_CHAR) || GetWindowString(hWnd).starts_with(L"=") || GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_MENU) < 0)
				return DefSubclassProc(hWnd, msg, wParam, lParam);

			// Scan codes of W, A, S, and D, which are where they are regardless of the keyboard layout.
			const auto scanCode = (lParam >> 16) & 0xFF;
			int axis, direction;
			switch (scanCode) {
				case 0x11: axis = 1, direction = -1; break;
				case 0x1E: axis = 0, direction = -1; break;
				case 0x1F: axis = 1, direction = +1; break;
				case 0x20: axis = 0, direction = +1; break;
				default: return DefSubclassProc(hWnd, msg, wParam, lParam);
			}

			// The characters of the keys are not typed.
			if (msg == WM_CHAR)
				return 0;

			auto& dlg = *reinterpret_cast<FontElementEditorDialog*>(dwRefData);
			const auto& c = *dlg.m_controls;
			const std::array<std::pair<HWND, HWND>, 4> pairs{{
				{c.AdjustmentHorizontalOffsetEdit, c.AdjustmentVerticalOffsetEdit},
				{c.GlyphMergingOffsetXEdit, c.GlyphMergingOffsetYEdit},
				{c.GlyphMergingScaleXEdit, c.GlyphMergingScaleYEdit},
				{c.GlyphMergingLetterSpacingEdit, c.GlyphMergingLineSpacingEdit},
			}};
			const auto it = std::ranges::find_if(pairs, [hWnd](const auto& p) { return p.first == hWnd || p.second == hWnd; });
			if (it == pairs.end())
				return 0;

			const auto target = axis == 0 ? it->first : it->second;
			if (!IsWindowEnabled(target) || GetWindowString(target).starts_with(L"="))
				return 0;

			// Shift changes the value in finer steps.
			const auto& range = dlg.m_spinRanges.at(target);
			const auto step = GetKeyState(VK_SHIFT) < 0 ? range.FineStep : range.Step;
			SetWindowNumber(target, (std::min)(range.Max, (std::max)(range.Min, dlg.GetSteppedValue(target) + direction * step)));
			return 0;
		}, 2, reinterpret_cast<DWORD_PTR>(this));
	}

	CenterWindowOnParent(m_controls->Window, m_hParentWnd);
	ShowWindow(m_controls->Window, SW_SHOW);

	return 0;
}

void App::FontElementEditorDialog::SetControlsEnabledOrDisabled() {
	const auto source = m_element.Source;
	const auto isEmpty = source == Structs::ElementSourceEnum::Empty;
	const auto drawsFontFiles = DrawsFontFiles(source);
	const auto usesSystemFont = UsesSystemFont(source);
	const auto isFreeType = source == Structs::ElementSourceEnum::FreeType;
	const auto isDirectWrite = source == Structs::ElementSourceEnum::DirectWrite;

	for (const auto& [hwnd, enabled] : {
		     std::pair{m_controls->FontCombo, !isEmpty},
		     std::pair{m_controls->FontSizeEdit, true},
		     std::pair{m_controls->FontWeightCombo, usesSystemFont},
		     std::pair{m_controls->FontStyleCombo, usesSystemFont},
		     std::pair{m_controls->FontStretchCombo, usesSystemFont},
		     std::pair{m_controls->FontFeaturesList, drawsFontFiles},
		     std::pair{m_controls->FontFeatureValueCombo, drawsFontFiles && GetSelectedFontFeature() >= 0},
		     std::pair{m_controls->FontLanguageCombo, drawsFontFiles},
		     std::pair{m_controls->FontVariationsList, drawsFontFiles && !m_variationAxes.empty()},
		     std::pair{m_controls->FontVariationValueEdit, drawsFontFiles && GetSelectedFontVariationAxis() >= 0},
		     std::pair{m_controls->EmptyAscentEdit, isEmpty},
		     std::pair{m_controls->EmptyLineHeightEdit, isEmpty},
		     std::pair{m_controls->FreeTypeHintingCombo, isFreeType},
		     std::pair{m_controls->FreeTypeEmbeddedBitmapsCheck, isFreeType},
		     std::pair{m_controls->FreeTypeRenderModeCombo, isFreeType},
		     std::pair{m_controls->DirectWriteRenderModeCombo, isDirectWrite},
		     std::pair{m_controls->DirectWriteMeasureModeCombo, isDirectWrite},
		     std::pair{m_controls->DirectWriteGridFitModeCombo, isDirectWrite},
		     std::pair{m_controls->AdjustmentVerticalOffsetEdit, !isEmpty},
		     std::pair{m_controls->AdjustmentLetterSpacingEdit, !isEmpty},
		     std::pair{m_controls->AdjustmentHorizontalOffsetEdit, !isEmpty},
		     std::pair{m_controls->AdjustmentGammaEdit, usesSystemFont},
		     std::pair{m_controls->TransformScaleXEdit, usesSystemFont},
		     std::pair{m_controls->TransformScaleYEdit, usesSystemFont},
		     std::pair{m_controls->TransformSkewEdit, usesSystemFont},
		     std::pair{m_controls->TransformRotationEdit, usesSystemFont},
		     std::pair{m_controls->CustomRangeEdit, !isEmpty},
		     std::pair{m_controls->CustomRangeAdd, !isEmpty},
		     std::pair{m_controls->CustomRangeSubtract, !isEmpty},
		     std::pair{m_controls->PresetRangeScriptCombo, !isEmpty},
		     std::pair{m_controls->PresetRangeScriptAdd, !isEmpty},
		     std::pair{m_controls->PresetRangeScriptSubtract, !isEmpty},
		     std::pair{m_controls->PresetRangeGameFontCombo, !isEmpty},
		     std::pair{m_controls->PresetRangeGameFontAdd, !isEmpty},
		     std::pair{m_controls->PresetRangeGameFontSubtract, !isEmpty},
		     std::pair{m_controls->CodepointsList, !isEmpty},
		     std::pair{m_controls->CodepointsDeleteButton, !isEmpty},
		     std::pair{m_controls->CodepointsMergeModeCombo, !isEmpty},
		     std::pair{m_controls->UnicodeBlockSearchNameEdit, !isEmpty},
		     std::pair{m_controls->UnicodeBlockSearchResultList, !isEmpty},
		     std::pair{m_controls->UnicodeBlockSearchAddAll, !isEmpty},
		     std::pair{m_controls->UnicodeBlockSearchAdd, !isEmpty},
		     std::pair{m_controls->UnicodeBlockSearchSubtract, !isEmpty},
		     std::pair{m_controls->FontAllowSynthesisCheck, drawsFontFiles},
	     })
		EnableWindow(hwnd, enabled);

	RefreshGlyphImagesGroup();

	// Texts are drawn with the font of the element, which only these sources can make at other sizes.
	SetGlyphMergingControlsEnabled(usesSystemFont);

	Button_SetCheck(m_controls->FontAllowSynthesisCheck, drawsFontFiles && m_element.Lookup.Synthesis && m_element.Lookup.Synthesis->Allow ? BST_CHECKED : BST_UNCHECKED);
	SetMonospacingControlsEnabled();
}

void App::FontElementEditorDialog::OnBaseFontChanged() {
	m_bBaseFontChanged = true;
	EnableWindow(m_controls->ApplyButton, TRUE);
	m_element.OnFontCreateParametersChange();
	RefreshGlyphMergingPresets();
	if (m_onFontChanged)
		m_onFontChanged();

	// The fields that are Auto as fit again, and whether they may be, after the change.
	RefreshAutoFields();
}

void App::FontElementEditorDialog::OnWrappedFontChanged() {
	m_bWrappedFontChanged = true;
	EnableWindow(m_controls->ApplyButton, TRUE);
	m_element.OnFontWrappingParametersChange();
	RefreshGlyphMergingPresets();
	if (m_onFontChanged)
		m_onFontChanged();
	RefreshAutoFields();
}

INT_PTR App::FontElementEditorDialog::DlgProc(UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
		case WM_INITDIALOG:
			return Dialog_OnInitDialog();
		case WM_COMMAND: {
			if (!m_controls)
				return 0;

			switch (LOWORD(wParam)) {
				case IDOK: return OkButton_OnCommand(HIWORD(wParam));
				case IDCANCEL: return CancelButton_OnCommand(HIWORD(wParam));
				case IDC_BUTTON_FONTELEMENTEDITOR_APPLY: return ApplyButton_OnCommand(HIWORD(wParam));
				case IDC_CHECK_FONTELEMENTEDITOR_DEACTIVATE: return DeactivateCheck_OnCommand(HIWORD(wParam));
				case IDC_COMBO_FONT_SOURCE: return FontSourceCombo_OnCommand(HIWORD(wParam));
				case IDC_COMBO_FONT: return FontCombo_OnCommand(HIWORD(wParam));
				case IDC_EDIT_FONT_SIZE:
				case IDC_EDIT_EMPTY_ASCENT:
				case IDC_EDIT_EMPTY_LINEHEIGHT:
				case IDC_EDIT_ADJUSTMENT_VERTICALOFFSET:
				case IDC_EDIT_ADJUSTMENT_LETTERSPACING:
				case IDC_EDIT_ADJUSTMENT_HORIZONTALOFFSET:
				case IDC_EDIT_ADJUSTMENT_GAMMA: return NumberEdit_OnCommand(LOWORD(wParam), HIWORD(wParam));
				case IDC_COMBO_FONT_WEIGHT:
				case IDC_COMBO_FONT_STYLE:
				case IDC_COMBO_FONT_STRETCH:
				case IDC_COMBO_FREETYPE_RENDERMODE:
				case IDC_COMBO_DIRECTWRITE_RENDERMODE:
				case IDC_COMBO_DIRECTWRITE_MEASUREMODE:
				case IDC_COMBO_DIRECTWRITE_GRIDFITMODE: return SettingCombo_OnCommand(LOWORD(wParam), HIWORD(wParam));
				case IDC_CHECK_FONT_ALLOWSYNTHESIS: return FontAllowSynthesisCheck_OnCommand(HIWORD(wParam));
				case IDC_COMBO_FONT_FEATURE_VALUE: return FontFeatureValueCombo_OnCommand(HIWORD(wParam));
				case IDC_COMBO_FONT_LANGUAGE: return FontLanguageCombo_OnCommand(HIWORD(wParam));
				case IDC_EDIT_FONT_VARIATION_VALUE: return FontVariationValueEdit_OnCommand(HIWORD(wParam));
				case IDC_COMBO_FREETYPE_HINTING:
				case IDC_CHECK_FREETYPE_EMBEDDEDBITMAPS: return FreeTypeHinting_OnCommand(HIWORD(wParam));
				case IDC_EDIT_TRANSFORM_SCALEX: return TransformEdit_OnCommand(0, HIWORD(wParam));
				case IDC_EDIT_TRANSFORM_SCALEY: return TransformEdit_OnCommand(1, HIWORD(wParam));
				case IDC_EDIT_TRANSFORM_SKEW: return TransformEdit_OnCommand(2, HIWORD(wParam));
				case IDC_EDIT_TRANSFORM_ROTATION: return TransformEdit_OnCommand(3, HIWORD(wParam));
				case IDC_EDIT_GLYPHIMAGES_PATH:
				case IDC_EDIT_GLYPHIMAGES_UNITSPEREM:
				case IDC_EDIT_GLYPHIMAGES_BASELINE:
				case IDC_EDIT_GLYPHIMAGES_ASCENT:
				case IDC_EDIT_GLYPHIMAGES_LINEHEIGHT: return GlyphImages_OnCommand(LOWORD(wParam), HIWORD(wParam));
				case IDC_BUTTON_GLYPHIMAGES_BROWSE: return GlyphImagesBrowseButton_OnCommand(HIWORD(wParam));
				case IDC_BUTTON_GLYPHIMAGES_EMBED: return GlyphImagesEmbedButton_OnCommand(HIWORD(wParam));
				case IDC_COMBO_MONOSPACING_MODE:
				case IDC_COMBO_MONOSPACING_ALIGNMENT:
				case IDC_EDIT_MONOSPACING_MIN:
				case IDC_EDIT_MONOSPACING_MAX:
				case IDC_COMBO_MONOSPACING_UNIT:
				case IDC_EDIT_MONOSPACING_REFERENCE:
				case IDC_CHECK_MONOSPACING_DROPKERNING: return Monospacing_OnCommand(LOWORD(wParam), HIWORD(wParam));
				case IDC_EDIT_ADDCUSTOMRANGE_INPUT: return CustomRangeEdit_OnCommand(HIWORD(wParam));
				case IDC_BUTTON_ADDCUSTOMRANGE_ADD: return CustomRangeAdd_OnCommand(HIWORD(wParam));
				case IDC_BUTTON_ADDCUSTOMRANGE_SUBTRACT: return CustomRangeSubtract_OnCommand(HIWORD(wParam));
				case IDC_BUTTON_PRESETRANGE_SCRIPT_ADD: return PresetRangeScript_OnCommand(true, HIWORD(wParam));
				case IDC_BUTTON_PRESETRANGE_SCRIPT_SUBTRACT: return PresetRangeScript_OnCommand(false, HIWORD(wParam));
				case IDC_BUTTON_PRESETRANGE_GAMEFONT_ADD: return PresetRangeGameFont_OnCommand(true, HIWORD(wParam));
				case IDC_BUTTON_PRESETRANGE_GAMEFONT_SUBTRACT: return PresetRangeGameFont_OnCommand(false, HIWORD(wParam));
				case IDC_LIST_CODEPOINTS: return CodepointsList_OnCommand(HIWORD(wParam));
				case IDC_BUTTON_CODEPOINTS_CLEAR: return CodepointsClearButton_OnCommand(HIWORD(wParam));
				case IDC_BUTTON_CODEPOINTS_DELETE: return CodepointsDeleteButton_OnCommand(HIWORD(wParam));
				case IDC_COMBO_CODEPOINTS_MERGEMODE: return CodepointsMergeModeCombo_OnCommand(HIWORD(wParam));
				case IDC_EDIT_UNICODEBLOCKS_SEARCH: return UnicodeBlockSearchNameEdit_OnCommand(HIWORD(wParam));
				case IDC_CHECK_UNICODEBLOCKS_SHOWBLOCKSWITHANYOFCHARACTERSINPUT: return UnicodeBlockSearchShowBlocksWithAnyOfCharactersInput_OnCommand(HIWORD(wParam));
				case IDC_LIST_UNICODEBLOCKS_SEARCHRESULTS: return UnicodeBlockSearchResultList_OnCommand(HIWORD(wParam));
				case IDC_BUTTON_UNICODEBLOCKS_ADDALL: return UnicodeBlockSearchAddAll_OnCommand(HIWORD(wParam));
				case IDC_BUTTON_UNICODEBLOCKS_ADD: return UnicodeBlockSearchAdd_OnCommand(HIWORD(wParam));
				case IDC_BUTTON_UNICODEBLOCKS_SUBTRACT: return UnicodeBlockSearchSubtract_OnCommand(HIWORD(wParam));
				case IDC_BUTTON_EXPRESSION_HELP: return ExpressionHelpButton_OnCommand(HIWORD(wParam));
				case IDC_EDIT_GLYPHMERGING_TEXTSIZE:
				case IDC_COMBO_GLYPHMERGING_FIT:
				case IDC_EDIT_GLYPHMERGING_OFFSETX:
				case IDC_EDIT_GLYPHMERGING_OFFSETY:
				case IDC_EDIT_GLYPHMERGING_SCALEX:
				case IDC_EDIT_GLYPHMERGING_SCALEY:
				case IDC_EDIT_GLYPHMERGING_SKEW:
				case IDC_EDIT_GLYPHMERGING_ROTATION:
				case IDC_EDIT_GLYPHMERGING_LETTERSPACING:
				case IDC_EDIT_GLYPHMERGING_LINESPACING:
				case IDC_COMBO_GLYPHMERGING_LINEALIGNMENT: return GlyphMergingPlacement_OnCommand(LOWORD(wParam), HIWORD(wParam));
				case IDC_BUTTON_GLYPHMERGING_ADD: return GlyphMergingAddButton_OnCommand(HIWORD(wParam));
				case IDC_BUTTON_GLYPHMERGING_DELETE: return GlyphMergingDeleteButton_OnCommand(HIWORD(wParam));
				case IDC_EDIT_GLYPHMERGING_CHARACTERS:
				case IDC_EDIT_GLYPHMERGING_TEXTS:
				case IDC_COMBO_GLYPHMERGING_SHAPE:
				case IDC_COMBO_GLYPHMERGING_TEXTMODE: return GlyphMergingMappingEditor_OnCommand(LOWORD(wParam), HIWORD(wParam));
				case IDC_BUTTON_GLYPHMERGING_IMPORTSVG: return GlyphMergingImportSvgButton_OnCommand(HIWORD(wParam));
			}
			if (LOWORD(wParam) >= IDC_CHECK_GLYPHMERGING_PRESET_0 && LOWORD(wParam) < IDC_CHECK_GLYPHMERGING_PRESET_0 + GlyphMergingPresetCount)
				return GlyphMergingPresetCheck_OnCommand(LOWORD(wParam) - IDC_CHECK_GLYPHMERGING_PRESET_0, HIWORD(wParam));
			return 0;
		}
		case WM_NOTIFY: {
			if (!m_controls)
				return 0;

			const auto& nmhdr = *reinterpret_cast<LPNMHDR>(lParam);
			if (nmhdr.idFrom == IDC_TAB_FONTELEMENTEDITOR && nmhdr.code == TCN_SELCHANGE) {
				ShowPage(TabCtrl_GetCurSel(m_controls->Tab));
				return 0;
			}
			if (nmhdr.idFrom == IDC_LIST_FONT_FEATURES) {
				switch (nmhdr.code) {
					case LVN_ITEMCHANGED: return FontFeaturesList_OnItemChanged(*reinterpret_cast<LPNMLISTVIEW>(lParam));
					case LVN_KEYDOWN: return FontFeaturesList_OnKeyDown(*reinterpret_cast<LPNMLVKEYDOWN>(lParam));
					case NM_DBLCLK: return FontFeaturesList_OnDblClick(*reinterpret_cast<LPNMITEMACTIVATE>(lParam));
					case NM_CUSTOMDRAW:
						// The result is passed on by the page that holds the list.
						SetWindowLongPtrW(m_hWnd, DWLP_MSGRESULT, FontFeaturesList_OnCustomDraw(*reinterpret_cast<LPNMLVCUSTOMDRAW>(lParam)));
						return TRUE;
				}
				return 0;
			}
			if (nmhdr.idFrom == IDC_LIST_FONT_VARIATIONS && nmhdr.code == LVN_ITEMCHANGED)
				return FontVariationsList_OnItemChanged(*reinterpret_cast<LPNMLISTVIEW>(lParam));
			if (nmhdr.idFrom == IDC_LIST_GLYPHMERGING_MAPPINGS && nmhdr.code == LVN_ITEMCHANGED)
				return GlyphMergingMappingsList_OnItemChanged(*reinterpret_cast<LPNMLISTVIEW>(lParam));
			return 0;
		}
		case WM_CONTEXTMENU: {
			if (m_controls && reinterpret_cast<HWND>(wParam) == m_controls->FontFeaturesList)
				return FontFeaturesList_OnContextMenu({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
			return 0;
		}
		case WM_CLOSE: {
			Reactivate();
			DestroyWindow(m_controls->Window);
			m_bOpened = false;
			return 0;
		}
		case WM_DESTROY: {
			Reactivate();
			m_bOpened = false;
			return 0;
		}
	}
	return 0;
}

INT_PTR __stdcall App::FontElementEditorDialog::DlgProcStatic(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
	return DlgProcStaticImpl<FontElementEditorDialog>(hwnd, message, wParam, lParam);
}

INT_PTR __stdcall App::FontElementEditorDialog::PageDlgProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
		case WM_INITDIALOG:
			return FALSE;

		case WM_COMMAND:
		case WM_NOTIFY:
			SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, SendMessageW(GetParent(hwnd), message, wParam, lParam));
			return TRUE;

		case WM_CONTEXTMENU:
			SendMessageW(GetParent(hwnd), message, wParam, lParam);
			return TRUE;
	}
	return FALSE;
}

void App::FontElementEditorDialog::ShowPage(int index) {
	if (index < 0 || index >= static_cast<int>(m_controls->Pages.size()))
		index = 0;

	s_lastPageIndex = index;
	for (int i = 0; i < static_cast<int>(m_controls->Pages.size()); i++)
		ShowWindow(m_controls->Pages[i], i == index ? SW_SHOW : SW_HIDE);
}
