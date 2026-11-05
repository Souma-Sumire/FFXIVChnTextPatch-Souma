#include "pch.h"
#include "FontElementEditorDialog.h"
#include "FontChanger.Presets/FontsFromSystemFont.h"
#include "resource.h"
#include "FontChanger.Presets/Structs.h"
#include "MainWindow.h"
#include "xivres/textools.h"

using FontChanger::FontsFromSystemFont::GetGameFontFamilyAndSize;

namespace {
	// Returns the size that the font is drawn at: that of its first element, which the merged font takes its size from,
	// or if it has no elements, the size of the game font by its name.
	std::optional<float> GetFontSize(const App::Structs::Font& font) {
		if (!font.Elements.empty())
			return font.Elements.front()->Size;
		if (const auto familyAndSize = GetGameFontFamilyAndSize(font.Name))
			return familyAndSize->second;
		return std::nullopt;
	}

	// Asks for a size in pixels, starting with size; returns whether a positive size was entered.
	bool AskForSize(HWND hParent, float& size) {
		const auto hglob = LoadResourceWithLanguageFallback(RT_DIALOG, IDD_SCALEFONT);
		return IDOK == DialogBoxIndirectParamW(
			g_hInstance,
			static_cast<DLGTEMPLATE*>(LockResource(hglob.get())),
			hParent,
			[](HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) -> INT_PTR {
				switch (message) {
					case WM_INITDIALOG: {
						SetWindowLongPtrW(hwnd, DWLP_USER, lParam);
						const auto edit = GetDlgItem(hwnd, IDC_EDIT_SCALEFONT_SIZE);
						SetWindowTextW(edit, std::format(L"{:g}", *reinterpret_cast<float*>(lParam)).c_str());
						SendMessageW(hwnd, WM_NEXTDLGCTL, reinterpret_cast<WPARAM>(edit), TRUE);
						return FALSE;
					}
					case WM_COMMAND:
						switch (LOWORD(wParam)) {
							case IDOK: {
								const auto text = GetWindowString(GetDlgItem(hwnd, IDC_EDIT_SCALEFONT_SIZE));
								wchar_t* end = nullptr;
								const auto value = std::wcstof(text.c_str(), &end);
								if (end == text.c_str() || *end || !std::isfinite(value) || value <= 0.f) {
									MessageBeep(MB_ICONWARNING);
									SendMessageW(hwnd, WM_NEXTDLGCTL, reinterpret_cast<WPARAM>(GetDlgItem(hwnd, IDC_EDIT_SCALEFONT_SIZE)), TRUE);
									return TRUE;
								}
								*reinterpret_cast<float*>(GetWindowLongPtrW(hwnd, DWLP_USER)) = value;
								EndDialog(hwnd, IDOK);
								return TRUE;
							}
							case IDCANCEL:
								EndDialog(hwnd, IDCANCEL);
								return TRUE;
						}
						break;
				}
				return FALSE;
			},
			reinterpret_cast<LPARAM>(&size));
	}
}

void App::FontEditorWindow::CloseEditors(const Structs::Font& font) {
	for (const auto& pElement : font.Elements) {
		if (const auto it = m_editors.find(pElement.get()); it != m_editors.end()) {
			if (it->second)
				DestroyWindow(it->second->m_hWnd);
			m_editors.erase(it);
		}
	}
}

void App::FontEditorWindow::CloseEditors(const std::vector<Structs::FontElement*>& elements) {
	for (const auto pElement : elements) {
		if (const auto it = m_editors.find(pElement); it != m_editors.end()) {
			if (it->second && it->second->IsOpened())
				DestroyWindow(it->second->m_hWnd);
			m_editors.erase(it);
		}
	}
}

LRESULT App::FontEditorWindow::Edit_OnCommand(uint16_t commandId) {
	switch (commandId) {
		case EN_CHANGE:
			if (m_pActiveFont) {
				auto& font = *m_pActiveFont;
				font.PreviewText = xivres::util::unicode::convert<std::string>(GetWindowString(m_hEdit));
				Changes_MarkDirty();
				Window_Redraw();
			} else
				return -1;
			return 0;
	}

	return 0;
}

LRESULT App::FontEditorWindow::FontListBox_OnCommand(uint16_t commandId) {
	switch (commandId) {
		case LBN_SELCHANGE: {
			if (const auto index = ListBox_GetCurSel(m_hFontsListBox); index != LB_ERR) {
				m_pActiveFont = reinterpret_cast<Structs::Font*>(ListBox_GetItemData(m_hFontsListBox, index));
				m_nPreviewScrollY = 0;
				UpdateFontElementList();
			}
			return 0;
		}
	}
	return 0;
}

LRESULT App::FontEditorWindow::FontListBox_OnContextMenu(POINT screenPos) {
	int index;
	if (screenPos.x == -1 && screenPos.y == -1) {
		index = ListBox_GetCurSel(m_hFontsListBox);
		if (index == LB_ERR)
			return 0;

		RECT rc;
		ListBox_GetItemRect(m_hFontsListBox, index, &rc);
		screenPos = {rc.left, rc.bottom};
		ClientToScreen(m_hFontsListBox, &screenPos);
	} else {
		POINT pt = screenPos;
		ScreenToClient(m_hFontsListBox, &pt);
		const auto hit = static_cast<DWORD>(SendMessageW(m_hFontsListBox, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y)));
		if (HIWORD(hit))
			return 0;

		// The menu is about the clicked font, so show it as the current one.
		index = LOWORD(hit);
		if (index != ListBox_GetCurSel(m_hFontsListBox)) {
			ListBox_SetCurSel(m_hFontsListBox, index);
			FontListBox_OnCommand(LBN_SELCHANGE);
		}
	}

	const auto pFont = reinterpret_cast<Structs::Font*>(ListBox_GetItemData(m_hFontsListBox, index));
	const auto pFontSet = pFont ? FindFontSet(*pFont) : nullptr;
	if (!pFontSet)
		return 0;

	const auto familyAndSize = GetGameFontFamilyAndSize(pFont->Name);
	auto hasOtherSizes = false;
	if (familyAndSize) {
		for (const auto& pOther : pFontSet->Fonts) {
			if (const auto other = GetGameFontFamilyAndSize(pOther->Name); pOther.get() != pFont && other && other->first == familyAndSize->first)
				hasOtherSizes = true;
		}
	}

	const auto fontName = xivres::util::unicode::convert<std::wstring>(pFont->Name);
	const auto familyName = familyAndSize ? xivres::util::unicode::convert<std::wstring>(familyAndSize->first) : fontName;
	const auto replaceText = std::vformat(GetStringResource(IDS_FONTLIST_REPLACEOTHERSIZES), std::make_wformat_args(familyName, fontName));
	const auto autoFitText = std::vformat(GetStringResource(IDS_FONTLIST_AUTOFITALLSIZES), std::make_wformat_args(familyName));
	const auto canAutoFit = familyAndSize && std::ranges::any_of(pFont->Elements, [](const auto& pElement) { return FontChanger::FontsFromSystemFont::CanAutoFit(*pElement); });

	enum : UINT {
		IdScaleToSize = 1,
		IdReplaceOtherSizes,
		IdAutoFitAllSizes,
	};
	const auto hMenu = CreatePopupMenu();
	AppendMenuW(hMenu, MF_STRING | (pFont->Elements.empty() ? MF_GRAYED : 0), IdScaleToSize, std::wstring(GetStringResource(IDS_FONTLIST_SCALETOSIZE)).c_str());
	AppendMenuW(hMenu, MF_STRING | (hasOtherSizes ? 0 : MF_GRAYED), IdReplaceOtherSizes, replaceText.c_str());
	AppendMenuW(hMenu, MF_STRING | (canAutoFit ? 0 : MF_GRAYED), IdAutoFitAllSizes, autoFitText.c_str());
	const auto command = TrackPopupMenuEx(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, screenPos.x, screenPos.y, m_hWnd, nullptr);
	DestroyMenu(hMenu);

	switch (command) {
		case IdScaleToSize:
			FontListBox_ScaleToSize(*pFont);
			break;
		case IdReplaceOtherSizes:
			FontListBox_ReplaceOtherSizes(*pFontSet, *pFont);
			break;
		case IdAutoFitAllSizes:
			// The clicked font is the active one.
			Menu_Edit_AutoFit(true, false);
			break;
	}
	return 0;
}

void App::FontEditorWindow::FontListBox_ScaleToSize(Structs::Font& font) {
	const auto currentSize = GetFontSize(font);
	if (font.Elements.empty() || !currentSize || *currentSize <= 0.f)
		return;

	auto size = *currentSize;
	if (!AskForSize(m_hWnd, size) || size == *currentSize)
		return;

	// Editors would show, and revert to, the values from before the scaling.
	CloseEditors(font);

	for (const auto& pElement : font.Elements) {
		pElement->Scale(size / *currentSize);
		UpdateFontElementListViewItem(*pElement);
	}
	font.OnElementChange();
	Changes_MarkDirty();
	Window_Redraw();
}

std::vector<App::Structs::Font*> App::FontEditorWindow::FontListBox_ReplaceOtherSizes(Structs::FontSet& fontSet, const Structs::Font& font) {
	const auto familyAndSize = GetGameFontFamilyAndSize(font.Name);
	const auto fontSize = GetFontSize(font);
	if (!familyAndSize || !fontSize || *fontSize <= 0.f)
		return {};

	std::vector<std::pair<Structs::Font*, float>> targets;
	std::wstring targetNames;
	for (const auto& pOther : fontSet.Fonts) {
		const auto other = GetGameFontFamilyAndSize(pOther->Name);
		if (pOther.get() == &font || !other || other->first != familyAndSize->first)
			continue;

		const auto size = GetFontSize(*pOther).value_or(other->second);
		targets.emplace_back(pOther.get(), size);
		if (!targetNames.empty())
			targetNames += L'\n';
		targetNames += std::format(L"{} ({})", xivres::util::unicode::convert<std::wstring>(pOther->Name), FormatPixelValue(size));
	}
	if (targets.empty())
		return {};

	const auto fontName = xivres::util::unicode::convert<std::wstring>(font.Name);
	if (MessageBoxW(
		m_hWnd,
		std::vformat(GetStringResource(IDS_FONTLIST_CONFIRMREPLACEOTHERSIZES), std::make_wformat_args(fontName, targetNames)).c_str(),
		GetWindowString(m_hWnd).c_str(),
		MB_YESNO | MB_ICONQUESTION) != IDYES)
		return {};

	std::vector<Structs::Font*> res;
	for (const auto& [pTarget, size] : targets) {
		CloseEditors(*pTarget);

		pTarget->Elements.clear();
		for (const auto& pElement : font.Elements)
			pTarget->Elements.emplace_back(std::make_unique<Structs::FontElement>(*pElement))->Scale(size / *fontSize);
		pTarget->VerticalAlignment = font.VerticalAlignment;
		pTarget->OnElementChange();
		res.push_back(pTarget);
	}

	Changes_MarkDirty();
	return res;
}

LRESULT App::FontEditorWindow::FontElementsListView_OnBeginDrag(NM_LISTVIEW& nmlv) {
	if (!m_pActiveFont)
		return -1;

	m_bIsReorderingFontElementList = true;
	SetCapture(m_hWnd);
	SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
	return 0;
}

bool App::FontEditorWindow::FontElementsListView_OnDragProcessMouseUp(int16_t x, int16_t y) {
	if (!m_bIsReorderingFontElementList)
		return false;

	m_bIsReorderingFontElementList = false;
	ReleaseCapture();
	FontElementsListView_DragProcessDragging(x, y);
	return true;
}

bool App::FontEditorWindow::FontElementsListView_OnDragProcessMouseMove(int16_t x, int16_t y) {
	if (!m_bIsReorderingFontElementList)
		return false;

	FontElementsListView_DragProcessDragging(x, y);
	return true;
}

bool App::FontEditorWindow::FontElementsListView_DragProcessDragging(int16_t x, int16_t y) {
	const auto tempDisableRedraw = SuppressRedraw(m_hFontElementsListView);

	// Determine the dropped item
	LVHITTESTINFO lvhti{
		.pt = {x, y},
	};
	ClientToScreen(m_hWnd, &lvhti.pt);
	ScreenToClient(m_hFontElementsListView, &lvhti.pt);
	ListView_HitTest(m_hFontElementsListView, &lvhti);

	// Out of the ListView?
	if (lvhti.iItem == -1) {
		POINT ptRef{};
		ListView_GetItemPosition(m_hFontElementsListView, 0, &ptRef);
		if (lvhti.pt.y < ptRef.y)
			lvhti.iItem = 0;
		else {
			RECT rcListView;
			GetClientRect(m_hFontElementsListView, &rcListView);
			ListView_GetItemPosition(m_hFontElementsListView, ListView_GetItemCount(m_hFontElementsListView) - 1, &ptRef);
			if (lvhti.pt.y >= ptRef.y || lvhti.pt.y >= rcListView.bottom - rcListView.top)
				lvhti.iItem = ListView_GetItemCount(m_hFontElementsListView) - 1;
			else
				return false;
		}
	}

	// The selected elements move, in their order, to end at the dropped item.
	auto& elements = m_pActiveFont->Elements;
	const auto sourceIndices = GetSelectedElementIndices();
	std::vector<const Structs::FontElement*> previousOrder;
	for (const auto& pElement : elements)
		previousOrder.push_back(pElement.get());

	std::vector<std::unique_ptr<Structs::FontElement>> moved, others;
	for (int i = 0, i_ = static_cast<int>(elements.size()); i < i_; i++)
		(std::ranges::binary_search(sourceIndices, i) ? moved : others).emplace_back(std::move(elements[i]));

	const auto insertAt = (std::min)(others.size(), static_cast<size_t>((std::max<int>)(0, 1 + lvhti.iItem - static_cast<int>(moved.size()))));
	std::vector<std::unique_ptr<Structs::FontElement>> reordered;
	std::ranges::move(others.begin(), others.begin() + insertAt, std::back_inserter(reordered));
	std::ranges::move(moved, std::back_inserter(reordered));
	std::ranges::move(others.begin() + insertAt, others.end(), std::back_inserter(reordered));

	elements = std::move(reordered);
	if (std::ranges::equal(elements, previousOrder, {}, &std::unique_ptr<Structs::FontElement>::get))
		return false;

	UpdateFontElementList();
	OnActiveFontElementsChanged();
	return true;
}

LRESULT App::FontEditorWindow::FontElementsListView_OnDblClick(NMITEMACTIVATE& nmia) {
	if (nmia.iItem == -1)
		return 0;
	if (m_pActiveFont == nullptr)
		return 0;
	if (static_cast<size_t>(nmia.iItem) >= m_pActiveFont->Elements.size())
		return 0;
	ShowEditor(*m_pActiveFont->Elements[nmia.iItem]);
	return 0;
}

LRESULT App::FontEditorWindow::FontElementsListView_OnRightClick(NMITEMACTIVATE& nmia) {
	if (!m_pActiveFont || !m_hFontElementContextMenu)
		return 0;
	POINT pt{nmia.ptAction};
	ClientToScreen(m_hFontElementsListView, &pt);

	// On the background, the menu adds fonts, after all the elements.
	if (nmia.iItem < 0)
		ListView_SetItemState(m_hFontElementsListView, -1, 0, LVIS_SELECTED);
	TrackPopupMenuEx(GetSubMenu(m_hFontElementContextMenu, nmia.iItem < 0 ? 1 : 0), TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd, nullptr);
	return 0;
}

LRESULT App::FontEditorWindow::FontElementsListView_OnCustomDraw(NMLVCUSTOMDRAW& nmcd) {
	switch (nmcd.nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;
		case CDDS_ITEMPREPAINT:
			return CDRF_NOTIFYSUBITEMDRAW;
		case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
			const auto& element = *reinterpret_cast<const Structs::FontElement*>(nmcd.nmcd.lItemlParam);
			auto isAuto = false;
			if (element.Auto.Any()) {
				switch (nmcd.iSubItem) {
					case ListViewColsSize: isAuto = element.Auto.Size; break;
					case ListViewColsAscent: isAuto = element.Auto.VerticalOffset; break;
					case ListViewColsHorizontalOffset: isAuto = element.Auto.HorizontalOffset; break;
					case ListViewColsSource: isAuto = element.Auto.RenderMode; break;
				}
			}
			nmcd.clrText = isAuto ? GetSysColor(COLOR_GRAYTEXT) : CLR_DEFAULT;
			return CDRF_DODEFAULT;
		}
	}
	return CDRF_DODEFAULT;
}

double App::FontEditorWindow::GetZoom() const noexcept {
	return GetZoomFromWindow(m_hWnd);
}
