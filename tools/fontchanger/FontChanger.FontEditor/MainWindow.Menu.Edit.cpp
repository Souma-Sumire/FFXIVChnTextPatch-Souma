#include "pch.h"

#include <commdlg.h>

#include "FontChanger.Presets/FontsFromSystemFont.h"
#include "FontChanger.Presets/Structs.h"
#include "FontElementEditorDialog.h"
#include "MainWindow.h"
#include "NegativeBearingCodepointsDialog.h"
#include "resource.h"
#include "xivres/textools.h"

LRESULT App::FontEditorWindow::Menu_Edit_Add() {
	if (!m_pActiveFont)
		return 0;

	auto& elements = m_pActiveFont->Elements;
	auto indices = GetSelectedElementIndices();
	if (indices.empty())
		indices.push_back(static_cast<int>(elements.size()));

	// Each new element is empty, at the size of the font, with the fields that fonts are fit by Auto: once it draws
	// characters with a font, it is fit to the game's font.
	const auto size = FontChanger::FontsFromSystemFont::GetGameFontSize(*m_pActiveFont).value_or(12.f);
	std::set<const Structs::FontElement*> added;
	for (const auto pos : indices | std::views::reverse) {
		auto& element = **elements.emplace(elements.begin() + pos, std::make_unique<Structs::FontElement>());
		element.Size = size;
		element.Auto = {.Size = true, .HorizontalOffset = true, .VerticalOffset = true, .RenderMode = true};
		added.insert(&element);
	}

	UpdateFontElementList();
	SelectFontElements(added);
	OnActiveFontElementsChanged();

	if (indices.size() == 1)
		ShowEditor(*elements[indices.front()]);

	return 0;
}

namespace FontsFromSystemFont = FontChanger::FontsFromSystemFont;

std::optional<LOGFONTW> App::FontEditorWindow::ChooseSystemFont() {
	LOGFONTW logFont{};
	CHOOSEFONTW chooseFont{
		.lStructSize = sizeof chooseFont,
		.hwndOwner = m_hWnd,
		.lpLogFont = &logFont,
		.Flags = CF_SCREENFONTS | CF_SCALABLEONLY | CF_NOVERTFONTS | CF_NOSIZESEL | CF_NOSCRIPTSEL | CF_FORCEFONTEXIST,
	};
	if (!ChooseFontW(&chooseFont))
		return std::nullopt;
	return logFont;
}

LRESULT App::FontEditorWindow::Menu_Edit_AddElementFromFont() {
	if (!m_pActiveFont)
		return 0;

	const auto logFont = ChooseSystemFont();
	if (!logFont)
		return 0;

	// Added before the first selected element, as Add does, or after all.
	const auto indices = GetSelectedElementIndices();
	const auto index = indices.empty() ? m_pActiveFont->Elements.size() : static_cast<size_t>(indices.front());

	std::set<const Structs::FontElement*> added;
	try {
		for (const auto pElement : FontsFromSystemFont::AddElement(*m_pActiveFont, FontsFromSystemFont::GetLookupFromLogFont(*logFont), index))
			added.insert(pElement);
	} catch (const std::exception& e) {
		const auto message = xivres::util::unicode::convert<std::wstring>(e.what());
		MessageBoxW(m_hWnd,
			std::vformat(GetStringResource(IDS_ADDFROMFONT_FAILED), std::make_wformat_args(message)).c_str(),
			std::wstring(GetStringResource(IDS_APP)).c_str(),
			MB_OK | MB_ICONERROR);
	}

	UpdateFontElementList();
	SelectFontElements(added);
	OnActiveFontElementsChanged();
	return 0;
}

LRESULT App::FontEditorWindow::Menu_Edit_AddFromFont() {
	if (!m_pActiveFont)
		return 0;

	const auto familyAndSize = FontsFromSystemFont::GetGameFontFamilyAndSize(m_pActiveFont->Name);
	if (!familyAndSize) {
		const auto name = xivres::util::unicode::convert<std::wstring>(m_pActiveFont->Name);
		MessageBoxW(m_hWnd,
			std::vformat(GetStringResource(IDS_ADDFROMFONT_NOTGAMEFONT), std::make_wformat_args(name)).c_str(),
			std::wstring(GetStringResource(IDS_APP)).c_str(),
			MB_OK | MB_ICONWARNING);
		return 0;
	}

	const auto logFont = ChooseSystemFont();
	if (!logFont)
		return 0;

	// The elements are added to the fonts of the family in the font set of the active font, and fit at each size.
	try {
		const auto lookup = FontsFromSystemFont::GetLookupFromLogFont(*logFont);
		if (const auto pFontSet = FindFontSet(*m_pActiveFont))
			FontsFromSystemFont::AddFamilyElements(*pFontSet, familyAndSize->first, lookup);
	} catch (const std::exception& e) {
		const auto message = xivres::util::unicode::convert<std::wstring>(e.what());
		MessageBoxW(m_hWnd,
			std::vformat(GetStringResource(IDS_ADDFROMFONT_FAILED), std::make_wformat_args(message)).c_str(),
			std::wstring(GetStringResource(IDS_APP)).c_str(),
			MB_OK | MB_ICONERROR);
	}

	UpdateFontElementList();
	Changes_MarkDirty();
	Window_Redraw();
	return 0;
}

LRESULT App::FontEditorWindow::Menu_Edit_AutoFit(bool allSizes, bool selectedOnly) {
	if (!m_pActiveFont)
		return 0;

	auto& font = *m_pActiveFont;
	auto indices = selectedOnly ? GetSelectedElementIndices() : std::vector<int>{};
	if (indices.empty()) {
		for (int i = 0; i < static_cast<int>(font.Elements.size()); i++)
			indices.push_back(i);
	}

	std::vector<Structs::FontElement*> targets;
	for (const auto i : indices) {
		if (FontsFromSystemFont::CanAutoFit(*font.Elements[i]))
			targets.push_back(font.Elements[i].get());
	}
	if (targets.empty() || !FontsFromSystemFont::GetGameFontFamilyAndSize(font.Name) || !FontsFromSystemFont::GetGameFontSize(font)) {
		MessageBoxW(m_hWnd, std::wstring(GetStringResource(IDS_AUTOFIT_NOTHINGTOFIT)).c_str(), std::wstring(GetStringResource(IDS_APP)).c_str(), MB_OK | MB_ICONWARNING);
		return 0;
	}

	// Editors would show, and revert to, the values from before the fitting.
	CloseEditors(targets);

	std::set<const Structs::FontElement*> fitted;
	try {
		const auto result = FontsFromSystemFont::AutoFit(font, targets);
		if (!result)
			return 0;

		std::vector<size_t> fittedIndices;
		for (const auto& [pElement, group] : result->Fitted) {
			fitted.insert(pElement);
			fittedIndices.push_back(std::ranges::find(font.Elements, pElement, &std::unique_ptr<Structs::FontElement>::get) - font.Elements.begin());
		}

		// The other sizes get copies of the elements, split as they are now, each fit at its size.
		if (const auto pFontSet = allSizes ? FindFontSet(font) : nullptr) {
			for (const auto pOther : FontListBox_ReplaceOtherSizes(*pFontSet, font)) {
				std::vector<Structs::FontElement*> others;
				for (const auto i : fittedIndices)
					others.push_back(pOther->Elements[i].get());
				FontsFromSystemFont::AutoFit(*pOther, others);
			}
		}
	} catch (const std::exception& e) {
		const auto message = xivres::util::unicode::convert<std::wstring>(e.what());
		MessageBoxW(m_hWnd,
			std::vformat(GetStringResource(IDS_AUTOFIT_FAILED), std::make_wformat_args(message)).c_str(),
			std::wstring(GetStringResource(IDS_APP)).c_str(),
			MB_OK | MB_ICONERROR);
	}

	UpdateFontElementList();
	for (const auto& pElement : font.Elements)
		UpdateFontElementListViewItem(*pElement);
	SelectFontElements(fitted);
	OnActiveFontElementsChanged();
	return 0;
}

LRESULT App::FontEditorWindow::Menu_Edit_Cut() {
	if (Menu_Edit_Copy())
		return 1;

	Menu_Edit_Delete();
	return 0;
}

LRESULT App::FontEditorWindow::Menu_Edit_Copy() {
	if (!m_pActiveFont)
		return 1;

	auto objs = nlohmann::json::array();
	for (const auto i : GetSelectedElementIndices())
		objs.emplace_back(*m_pActiveFont->Elements[i]);

	const auto wstr = xivres::util::unicode::convert<std::wstring>(objs.dump());

	const auto clipboard = OpenClipboard(m_hWnd);
	if (!clipboard)
		return 1;
	EmptyClipboard();

	bool copied = false;
	HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, (wstr.size() + 1) * 2);
	if (hg) {
		if (const auto pLock = GlobalLock(hg)) {
			memcpy(pLock, wstr.data(), (wstr.size() + 1) * 2);
			copied = SetClipboardData(CF_UNICODETEXT, pLock);
		}
		GlobalUnlock(hg);
		if (!copied)
			GlobalFree(hg);
	}
	CloseClipboard();
	return copied ? 0 : 1;
}

LRESULT App::FontEditorWindow::Menu_Edit_Paste() {
	if (!m_pActiveFont)
		return 0;

	const auto clipboard = OpenClipboard(m_hWnd);
	if (!clipboard)
		return 0;

	std::string data;
	if (const auto pData = GetClipboardData(CF_UNICODETEXT))
		data = xivres::util::unicode::convert<std::string>(static_cast<const wchar_t*>(pData));
	CloseClipboard();

	std::vector<Structs::FontElement> parsedTemplateElements;
	try {
		const auto parsed = nlohmann::json::parse(data);
		if (!parsed.is_array())
			return 0;
		for (const auto& p : parsed) {
			parsedTemplateElements.emplace_back();
			from_json(p, parsedTemplateElements.back());
		}
		if (parsedTemplateElements.empty())
			return 0;
	} catch (const nlohmann::json::exception&) {
		return 0;
	}

	auto& elements = m_pActiveFont->Elements;
	auto indices = GetSelectedElementIndices();
	if (indices.empty())
		indices.push_back(static_cast<int>(elements.size()));

	std::set<const Structs::FontElement*> added;
	for (const auto pos : indices | std::views::reverse) {
		for (const auto& templateElement : parsedTemplateElements | std::views::reverse)
			added.insert(elements.emplace(elements.begin() + pos, std::make_unique<Structs::FontElement>(templateElement))->get());
	}

	UpdateFontElementList();
	SelectFontElements(added);
	OnActiveFontElementsChanged();
	return 0;
}

LRESULT App::FontEditorWindow::Menu_Edit_Delete() {
	if (!m_pActiveFont)
		return 0;

	const auto indices = GetSelectedElementIndices();
	if (indices.empty())
		return 0;

	for (const auto index : indices | std::views::reverse)
		m_pActiveFont->Elements.erase(m_pActiveFont->Elements.begin() + index);

	UpdateFontElementList();
	OnActiveFontElementsChanged();
	return 0;
}

LRESULT App::FontEditorWindow::Menu_Edit_SelectAll() {
	ListView_SetItemState(m_hFontElementsListView, -1, LVIS_SELECTED, LVIS_SELECTED);
	return 0;
}

LRESULT App::FontEditorWindow::Menu_Edit_Details() {
	for (const auto i : GetSelectedElementIndices())
		ShowEditor(*m_pActiveFont->Elements[i]);
	return 0;
}

LRESULT App::FontEditorWindow::Menu_Edit_ChangeParams(int verticalOffset, int horizontalOffset, int letterSpacing, float fontSize) {
	const auto indices = GetSelectedElementIndices();
	if (indices.empty())
		return 0;

	const auto tempDisableRedraw = SuppressRedraw(m_hFontElementsListView);
	for (const auto i : indices) {
		auto& e = *m_pActiveFont->Elements[i];
		auto baseChanged = false;
		if (e.Source == Structs::ElementSourceEnum::Empty) {
			baseChanged |= !!verticalOffset;
			baseChanged |= !!(letterSpacing + horizontalOffset);
			e.RendererSpecific.Empty.Ascent += verticalOffset;
			e.RendererSpecific.Empty.LineHeight += letterSpacing + horizontalOffset;
		} else {
			e.WrapModifiers.VerticalOffset += verticalOffset;
			e.WrapModifiers.HorizontalOffset += horizontalOffset;
			e.WrapModifiers.LetterSpacing += letterSpacing;

			// Values stepped from are the user's from then on.
			if (verticalOffset)
				e.Auto.VerticalOffset = false;
			if (horizontalOffset)
				e.Auto.HorizontalOffset = false;
		}
		if (fontSize != 0.f) {
			e.Size = std::roundf((e.Size + fontSize) * 10.f) / 10.f;
			e.Auto.Size = false;
			baseChanged = true;
		}
		if (baseChanged)
			e.OnFontCreateParametersChange();
		else
			e.OnFontWrappingParametersChange();
		UpdateFontElementListViewItem(e);

		if (const auto it = m_editors.find(&e); it != m_editors.end() && it->second && it->second->IsOpened())
			it->second->RefreshAutoFields();
	}

	OnActiveFontElementsChanged();
	return 0;
}

LRESULT App::FontEditorWindow::Menu_Edit_ToggleMergeMode() {
	const auto indices = GetSelectedElementIndices();
	if (indices.empty())
		return 0;

	const auto tempDisableRedraw = SuppressRedraw(m_hFontElementsListView);
	for (const auto i : indices) {
		auto& e = *m_pActiveFont->Elements[i];
		e.MergeMode = static_cast<FontChanger::FixedSizeFont::codepoint_merge_mode>((static_cast<int>(e.MergeMode) + 1) % static_cast<int>(FontChanger::FixedSizeFont::codepoint_merge_mode::Enum_Count_));
		e.OnFontWrappingParametersChange();
		UpdateFontElementListViewItem(e);
	}

	OnActiveFontElementsChanged();
	return 0;
}

LRESULT App::FontEditorWindow::Menu_Edit_SetVerticalAlignment(FontChanger::FixedSizeFont::vertical_alignment alignment) {
	if (!m_pActiveFont || m_pActiveFont->VerticalAlignment == alignment)
		return 0;

	m_pActiveFont->VerticalAlignment = alignment;
	m_pActiveFont->OnElementChange();
	Changes_MarkDirty();
	Window_Redraw();
	return 0;
}

LRESULT App::FontEditorWindow::Menu_Edit_MoveUpOrDown(int direction) {
	auto indices = GetSelectedElementIndices();
	if (indices.empty())
		return 0;

	// Each selected element swaps places with its neighbor, starting from the one nearest to where they move.
	auto& elements = m_pActiveFont->Elements;
	if (direction > 0)
		std::ranges::reverse(indices);

	auto any = false;
	for (const auto index : indices) {
		const auto target = index + direction;
		if (target < 0 || target >= static_cast<int>(elements.size()))
			continue;

		any = true;
		std::swap(elements[index], elements[target]);
	}
	if (!any)
		return 0;

	UpdateFontElementList();
	OnActiveFontElementsChanged();
	return 0;
}

LRESULT App::FontEditorWindow::FontElementsListView_Clone() {
	if (!m_pActiveFont)
		return 0;

	const auto indices = GetSelectedElementIndices();
	if (indices.empty())
		return 0;

	// The copies go after the last selected element, in their order.
	auto& elements = m_pActiveFont->Elements;
	std::vector<std::unique_ptr<Structs::FontElement>> clones;
	for (const auto i : indices)
		clones.emplace_back(std::make_unique<Structs::FontElement>(*elements[i]));

	std::set<const Structs::FontElement*> added;
	for (const auto& pClone : clones)
		added.insert(pClone.get());
	elements.insert(elements.begin() + indices.back() + 1, std::make_move_iterator(clones.begin()), std::make_move_iterator(clones.end()));

	UpdateFontElementList();
	SelectFontElements(added);
	OnActiveFontElementsChanged();
	return 0;
}

LRESULT App::FontEditorWindow::FontElementsListView_ShowNegativeBearingCodepoints() {
	if (!m_pActiveFont) return 0;
	const auto index = ListView_GetNextItem(m_hFontElementsListView, -1, LVNI_SELECTED);
	if (index < 0)
		return 0;

	const auto& element = *m_pActiveFont->Elements[index];
	if (element.Source == Structs::ElementSourceEnum::Empty)
		return 0;

	const auto hPrevCursor = SetCursor(LoadCursor(nullptr, IDC_WAIT));
	auto entries = element.GetWrappedFont()->get_negative_lsb_codepoints();
	if (entries.empty()) {
		SetCursor(hPrevCursor);
		MessageBoxW(m_hWnd,
			std::wstring(GetStringResource(IDS_NEGATIVEBEARING_NONE_FOUND)).c_str(),
			std::wstring(GetStringResource(IDS_APP)).c_str(),
			MB_OK | MB_ICONINFORMATION);
		return 0;
	}

	SetCursor(hPrevCursor);
	NegativeBearingCodepointsDialog::Show(m_hWnd, std::move(entries));
	return 0;
}

LRESULT App::FontEditorWindow::Menu_Edit_CreateEmptyCopyFromSelection() {
	if (!m_pActiveFont)
		return 0;

	const auto refIndex = ListView_GetNextItem(m_hFontElementsListView, -1, LVNI_SELECTED);
	if (refIndex == -1)
		return 0;

	// An empty element of the same metrics, first so that it decides the metrics of the font.
	auto& elements = m_pActiveFont->Elements;
	const auto& ref = *elements[refIndex];
	auto element = std::make_unique<Structs::FontElement>();
	element->Size = ref.Size;
	element->RendererSpecific = {
		.Empty = {
			.Ascent = static_cast<float>(ref.GetWrappedFont()->ascent()) + ref.WrapModifiers.VerticalOffset,
			.LineHeight = static_cast<float>(ref.GetWrappedFont()->line_height()),
		},
	};
	const auto& added = **elements.emplace(elements.begin(), std::move(element));

	UpdateFontElementList();
	SelectFontElements({&added});
	OnActiveFontElementsChanged();
	return 0;
}
