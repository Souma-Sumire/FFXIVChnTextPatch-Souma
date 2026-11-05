#include "pch.h"

#include "FontElementEditorDialog.h"
#include "resource.h"
#include "FontChanger.Presets/FontsFromSystemFont.h"
#include "FontChanger.Presets/Structs.h"
#include "MainWindow.h"
#include "MainWindow.Internal.h"
#include "FontGeneratorConfig.h"

namespace {
	// Menu items that choose a value, checked when it is the current one.
	constexpr std::pair<UINT, const char*> LanguageItems[]{
		{ID_FILE_LANGUAGE_AUTO, ""},
		{ID_FILE_LANGUAGE_ENGLISH, "en-us"},
		{ID_FILE_LANGUAGE_KOREAN, "ko-kr"},
		{ID_FILE_LANGUAGE_CHINESE, "zh-hans"},
	};

	constexpr std::pair<UINT, xivres::font_type> HotReloadFontItems[]{
		{ID_HOTRELOAD_FONT_AUTO, xivres::font_type::undefined},
		{ID_HOTRELOAD_FONT_FONT, xivres::font_type::font},
		{ID_HOTRELOAD_FONT_LOBBY, xivres::font_type::font_lobby},
		{ID_HOTRELOAD_FONT_CHNAXIS, xivres::font_type::chn_axis},
		{ID_HOTRELOAD_FONT_KRNAXIS, xivres::font_type::krn_axis},
	};

	constexpr std::pair<UINT, FontChanger::FixedSizeFont::vertical_alignment> AlignmentItems[]{
		{ID_EDIT_VERTICALALIGNMENT_TOP, FontChanger::FixedSizeFont::vertical_alignment::Top},
		{ID_EDIT_VERTICALALIGNMENT_MIDDLE, FontChanger::FixedSizeFont::vertical_alignment::Middle},
		{ID_EDIT_VERTICALALIGNMENT_BASELINE, FontChanger::FixedSizeFont::vertical_alignment::Baseline},
		{ID_EDIT_VERTICALALIGNMENT_BOTTOM, FontChanger::FixedSizeFont::vertical_alignment::Bottom},
		{ID_EDIT_VERTICALALIGNMENT_ROMANBASELINE, FontChanger::FixedSizeFont::vertical_alignment::RomanBaseline},
		{ID_EDIT_VERTICALALIGNMENT_IDEOGRAPHICCENTER, FontChanger::FixedSizeFont::vertical_alignment::IdeographicCenter},
	};

	struct MinimumHeights {
		int List;
		int Edit;
		int Preview;
	};

	// Gets the smallest heights of the list, the edit and the preview in pixels.
	MinimumHeights GetMinimumHeights(double zoom) {
		return {
			static_cast<int>(std::lround(MinFontElementListViewHeight * zoom)),
			static_cast<int>(std::lround(MinPreviewEditHeight * zoom)),
			static_cast<int>(std::lround(MinPreviewHeight * zoom)),
		};
	}
}

bool App::FontEditorWindow::ConsumeDialogMessage(MSG& msg) {
	if (IsDialogMessage(m_hWnd, &msg))
		return true;

	for (const auto& e : m_editors | std::views::values)
		if (e && e->IsOpened() && e->ConsumeDialogMessage(msg))
			return true;

	return false;
}

bool App::FontEditorWindow::ConsumeAccelerator(MSG& msg) {
	if (!m_hAccelerator)
		return false;

	if (GetForegroundWindow() != m_hWnd)
		return false;

	if (msg.message == WM_KEYDOWN && msg.hwnd == m_hEdit) {
		if (msg.wParam == VK_RETURN || msg.wParam == VK_INSERT || msg.wParam == VK_DELETE)
			return false;
		if (!(GetKeyState(VK_CONTROL) & 0x8000) || msg.wParam == 'C' || msg.wParam == 'X' || msg.wParam == 'V' || msg.wParam == 'A')
			return false;
	}
	return TranslateAccelerator(m_hWnd, m_hAccelerator, &msg);
}

LRESULT App::FontEditorWindow::Window_OnCreate(HWND hwnd) {
	m_hWnd = hwnd;
	StartWatchingGlyphFolders();

	m_hAccelerator =LoadAcceleratorsW(g_hInstance, MAKEINTRESOURCEW(IDR_ACCELERATOR_FONTELEMENTEDITOR));

	const auto hGlob = LoadResourceWithLanguageFallback(RT_MENU, IDR_FONTEDITOR);
	if (!hGlob)
		throw std::system_error(std::error_code(GetLastError(), std::system_category()));
	SetMenu(m_hWnd, LoadMenuIndirectW(LockResource(hGlob.get())));

	{
		const auto hCtxGlob = LoadResourceWithLanguageFallback(RT_MENU, IDR_CONTEXTMENU_FONTELEMENT);
		m_hFontElementContextMenu = LoadMenuIndirectW(LockResource(hCtxGlob.get()));
	}

	NONCLIENTMETRICSW ncm = {sizeof(NONCLIENTMETRICSW)};
	SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
	m_hUiFont = CreateFontIndirectW(&ncm.lfMessageFont);

	m_hFontsListBox = CreateWindowExW(0, WC_LISTBOXW, nullptr,
		WS_CHILD | WS_TABSTOP | WS_BORDER | WS_VISIBLE | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
		0, 0, 0, 0, m_hWnd, reinterpret_cast<HMENU>(Id_FontListBox), reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(m_hWnd, GWLP_HINSTANCE)), nullptr);
	m_hFontElementsListView = CreateWindowExW(0, WC_LISTVIEWW, nullptr,
		WS_CHILD | WS_TABSTOP | WS_BORDER | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
		0, 0, 0, 0, m_hWnd, reinterpret_cast<HMENU>(Id_FontElementListView), reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(m_hWnd, GWLP_HINSTANCE)), nullptr);
	m_hEdit = CreateWindowExW(0, WC_EDITW, nullptr,
		WS_CHILD | WS_TABSTOP | WS_BORDER | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_AUTOVSCROLL | ES_MULTILINE | ES_WANTRETURN,
		0, 0, 0, 0, m_hWnd, reinterpret_cast<HMENU>(Id_Edit), reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(m_hWnd, GWLP_HINSTANCE)), nullptr);

	ListView_SetExtendedListViewStyle(m_hFontElementsListView, LVS_EX_FULLROWSELECT);

	SendMessage(m_hFontsListBox, WM_SETFONT, reinterpret_cast<WPARAM>(m_hUiFont), FALSE);
	SendMessage(m_hFontElementsListView, WM_SETFONT, reinterpret_cast<WPARAM>(m_hUiFont), FALSE);
	SendMessage(m_hEdit, WM_SETFONT, reinterpret_cast<WPARAM>(m_hUiFont), FALSE);

	SetWindowSubclass(m_hEdit, [](HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) -> LRESULT {
		if (msg == WM_GETDLGCODE && wParam == VK_TAB)
			return 0;
		if (msg == WM_KEYDOWN && wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_SHIFT) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000) && !(GetKeyState(VK_LWIN) & 0x8000) && !(GetKeyState(VK_RWIN) & 0x8000))
			Edit_SetSel(hWnd, 0, Edit_GetTextLength(hWnd));
		return DefSubclassProc(hWnd, msg, wParam, lParam);
	}, 1, 0);

	const auto AddColumn = [this](int columnIndex, int width, UINT resId) {
		AddListViewColumn(m_hFontElementsListView, columnIndex, width, resId);
	};
	AddColumn(ListViewColsFamilyName, 120, IDS_FONTLISTVIEW_COLUMN_FAMILY);
	AddColumn(ListViewColsSubfamilyName, 80, IDS_FONTLISTVIEW_COLUMN_SUBFAMILY);
	AddColumn(ListViewColsSize, 80, IDS_FONTLISTVIEW_COLUMN_SIZE);
	AddColumn(ListViewColsLineHeight, 80, IDS_FONTLISTVIEW_COLUMN_LINE_HEIGHT);
	AddColumn(ListViewColsAscent, 80, IDS_FONTLISTVIEW_COLUMN_ASCENT);
	AddColumn(ListViewColsHorizontalOffset, 120, IDS_FONTLISTVIEW_COLUMN_HORIZONTAL_OFFSET);
	AddColumn(ListViewColsLetterSpacing, 100, IDS_FONTLISTVIEW_COLUMN_LETTER_SPACING);
	AddColumn(ListViewColsGamma, 60, IDS_FONTLISTVIEW_COLUMN_GAMMA);
	AddColumn(ListViewColsCodepoints, 80, IDS_FONTLISTVIEW_COLUMN_CODEPOINTS);
	AddColumn(ListViewColsMergeMode, 70, IDS_FONTLISTVIEW_COLUMN_OVERWRITE);
	AddColumn(ListViewColsGlyphCount, 60, IDS_FONTLISTVIEW_COLUMN_GLYPHS);
	AddColumn(ListViewColsSource, 180, IDS_FONTLISTVIEW_COLUMN_SOURCE);
	AddColumn(ListViewColsLookup, 300, IDS_FONTLISTVIEW_COLUMN_LOOKUP);

	if (m_args.size() >= 2 && std::filesystem::exists(m_args[1])) {
		if (const auto r = TryCatchShowError(m_hWnd, IDS_ERROR_OPENFILEFAILURE_BODY, LRESULT{1}, [&]() -> LRESULT {
			OpenFile(m_args[1]);
			return 0;
		}))
			return r;
	}
	if (m_currentPath.empty())
		Menu_File_New(xivres::font_type::font);

	Window_OnSize();
	ShowWindow(m_hWnd, SW_SHOW);
	return 0;
}

LRESULT App::FontEditorWindow::Window_OnSize() {
	RECT rc;
	GetClientRect(m_hWnd, &rc);

	const auto zoom = GetZoom();
	const auto scaledFontListBoxWidth = static_cast<int>(std::lround(FontListBoxWidth * zoom));
	const auto availableHeight = rc.bottom - rc.top;

	auto splitterThickness = static_cast<int>(std::lround(SplitterThickness * zoom));
	splitterThickness = (std::max)(2, splitterThickness);
	splitterThickness = (std::min<int>)(splitterThickness, (std::max<int>)(1, availableHeight / 8));

	const auto [minListPx, minEditPx, minPreviewPx] = GetMinimumHeights(zoom);

	auto listViewHeightPx = static_cast<int>(std::lround(g_config.FontElementListViewHeight * zoom));
	auto editHeightPx = static_cast<int>(std::lround(g_config.PreviewEditHeight * zoom));

	listViewHeightPx = (std::max)(minListPx, listViewHeightPx);
	editHeightPx = (std::max)(minEditPx, editHeightPx);

	if (const auto previewHeightPx = availableHeight - listViewHeightPx - editHeightPx - splitterThickness * 2;
		previewHeightPx < minPreviewPx) {
		auto deficit = minPreviewPx - previewHeightPx;
		const auto reduceEdit = (std::min<int>)(deficit, editHeightPx - minEditPx);
		editHeightPx -= reduceEdit;
		deficit -= reduceEdit;
		if (deficit > 0) {
			const auto reduceList = (std::min<int>)(deficit, listViewHeightPx - minListPx);
			listViewHeightPx -= reduceList;
			// deficit -= reduceList;
		}
		// previewHeightPx = (std::max<int>)(0, availableHeight - listViewHeightPx - editHeightPx - splitterThickness * 2);
	}

	m_scaledListViewHeight = (std::max)(0, listViewHeightPx);
	m_scaledEditHeight = (std::max)(0, editHeightPx);
	m_splitterThicknessPx = splitterThickness;
	m_splitterListEditTop = m_scaledListViewHeight;
	m_splitterListEditBottom = m_splitterListEditTop + m_splitterThicknessPx;
	m_splitterEditPreviewTop = m_splitterListEditBottom + m_scaledEditHeight;
	m_splitterEditPreviewBottom = m_splitterEditPreviewTop + m_splitterThicknessPx;

	auto hdwp = BeginDeferWindowPos(Id_Last_);
	hdwp = DeferWindowPos(
		hdwp,
		m_hFontsListBox,
		nullptr,
		0,
		0,
		scaledFontListBoxWidth,
		rc.bottom - rc.top,
		SWP_NOZORDER | SWP_NOACTIVATE);
	hdwp = DeferWindowPos(
		hdwp,
		m_hFontElementsListView,
		nullptr,
		scaledFontListBoxWidth,
		0,
		(std::max<int>)(0, rc.right - rc.left - scaledFontListBoxWidth),
		m_scaledListViewHeight,
		SWP_NOZORDER | SWP_NOACTIVATE);
	hdwp = DeferWindowPos(
		hdwp,
		m_hEdit,
		nullptr,
		scaledFontListBoxWidth,
		m_splitterListEditBottom,
		(std::max<int>)(0, rc.right - rc.left - scaledFontListBoxWidth),
		m_scaledEditHeight,
		SWP_NOZORDER | SWP_NOACTIVATE);
	EndDeferWindowPos(hdwp);

	m_nDrawLeft = scaledFontListBoxWidth;
	m_nDrawTop = m_splitterEditPreviewBottom;
	m_bNeedRedraw = true;

	return 0;
}

LRESULT App::FontEditorWindow::Window_OnPaint() {
	union {
		char bmibuf[sizeof(BITMAPINFOHEADER) + sizeof(DWORD) * 3];
		BITMAPINFO bmi{};
	};

	PAINTSTRUCT ps;
	const auto hdc = BeginPaint(m_hWnd, &ps);
	RECT clientRect;
	GetClientRect(m_hWnd, &clientRect);
	if (m_bNeedRedraw) {
		m_bNeedRedraw = false;

		m_pMipmap = std::make_shared<xivres::texture::memory_mipmap_stream>(
			(std::max<int>)(1, (clientRect.right - clientRect.left - m_nDrawLeft) * PreviewZoomOne / m_nZoom),
			(std::max<int>)(1, (clientRect.bottom - clientRect.top - m_nDrawTop) * PreviewZoomOne / m_nZoom),
			1,
			xivres::texture::formats::B8G8R8A8);

		const auto pad = 16 * m_nZoom / PreviewZoomOne;
		const auto buf = m_pMipmap->as_span<xivres::util::b8g8r8a8>();
		std::ranges::fill(buf, xivres::util::b8g8r8a8{0x88, 0x88, 0x88, 0xFF});

		const auto measured = [&]() -> std::optional<FontChanger::FixedSizeFont::text_measure_result> {
			if (!m_pActiveFont || m_pActiveFont->PreviewText.empty())
				return std::nullopt;

			return FontChanger::FixedSizeFont::text_measurer(*GetPreviewFont())
			       .max_width(m_bWordWrap ? m_pMipmap->Width - pad * 2 : (std::numeric_limits<int>::max)())
			       .use_kerning(m_bKerning)
			       .measure(m_pActiveFont->PreviewText);
		}();

		if (measured) {
			m_nPreviewContentWidth = measured->Occupied.width();
			m_nPreviewContentHeight = measured->Occupied.height();
			const auto maxScrollX = (std::max)(0, m_nPreviewContentWidth + pad * 2 - m_pMipmap->Width);
			const auto maxScrollY = (std::max)(0, m_nPreviewContentHeight + pad * 2 - m_pMipmap->Height);
			m_nPreviewScrollX = std::clamp(m_nPreviewScrollX, 0, maxScrollX);
			m_nPreviewScrollY = std::clamp(m_nPreviewScrollY, 0, maxScrollY);
			m_nPreviewMayScroll = maxScrollX || maxScrollY;
		} else {
			m_nPreviewContentHeight = 0;
			m_nPreviewScrollX = 0;
			m_nPreviewScrollY = 0;
			m_nPreviewMayScroll = false;
		}

		const RECT previewContentRect = {
			.left = (std::max)(0, pad - m_nPreviewScrollX),
			.top = (std::max)(0, pad - m_nPreviewScrollY),
			.right = (std::max<int>)(
				(std::min<int>)(m_nPreviewContentWidth + pad - m_nPreviewScrollX, m_pMipmap->Width),
				m_pMipmap->Width - pad),
			.bottom = (std::max<int>)(
				(std::min<int>)(m_nPreviewContentHeight + pad - m_nPreviewScrollY, m_pMipmap->Height),
				m_pMipmap->Height - pad),
		};
		for (int y = previewContentRect.top; y < previewContentRect.bottom; y++) {
			for (int x = previewContentRect.left; x < previewContentRect.right; x++)
				buf[y * m_pMipmap->Width + x] = {0x00, 0x00, 0x00, 0xFF};
		}

		if (m_pActiveFont) {
			const auto pMergedFont = GetPreviewFont();
			const auto& mergedFont = *pMergedFont;

			if (int lineHeight = mergedFont.line_height(), ascent = mergedFont.ascent();
				lineHeight > 0 && m_bShowLineMetrics && ascent <= lineHeight) {
				const auto period = ascent < lineHeight ? lineHeight : 2 * lineHeight;
				const auto top = static_cast<int>(previewContentRect.top);
				const auto bottom = static_cast<int>(previewContentRect.bottom);
				for (int y = pad - m_nPreviewScrollY; y < bottom; y += period) {
					for (int y2 = (std::max)(top, y + ascent), y2_ = (std::min)(bottom, y + period); y2 < y2_; y2++)
						for (int x = previewContentRect.left; x < previewContentRect.right; x++)
							buf[y2 * m_pMipmap->Width + x] = {0x33, 0x33, 0x33, 0xFF};
				}
			}

			if (measured) {
				measured->draw_to(
					*m_pMipmap,
					mergedFont,
					pad - m_nPreviewScrollX,
					pad - m_nPreviewScrollY,
					{0xFF, 0xFF, 0xFF, 0xFF},
					{0, 0, 0, 0});
			}
		}
	}

	const auto splitterBrush = GetSysColorBrush(COLOR_3DFACE);
	if (m_splitterListEditTop < m_splitterListEditBottom) {
		RECT splitter{m_nDrawLeft, m_splitterListEditTop, clientRect.right, m_splitterListEditBottom};
		FillRect(hdc, &splitter, splitterBrush);
	}
	if (m_splitterEditPreviewTop < m_splitterEditPreviewBottom) {
		RECT splitter{m_nDrawLeft, m_splitterEditPreviewTop, clientRect.right, m_splitterEditPreviewBottom};
		FillRect(hdc, &splitter, splitterBrush);
	}

	bmi.bmiHeader.biSize = sizeof bmi.bmiHeader;
	bmi.bmiHeader.biWidth = m_pMipmap->Width;
	bmi.bmiHeader.biHeight = -m_pMipmap->Height;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_BITFIELDS;
	reinterpret_cast<xivres::util::b8g8r8a8*>(&bmi.bmiColors[0])->set_components(255, 0, 0, 0);
	reinterpret_cast<xivres::util::b8g8r8a8*>(&bmi.bmiColors[1])->set_components(0, 255, 0, 0);
	reinterpret_cast<xivres::util::b8g8r8a8*>(&bmi.bmiColors[2])->set_components(0, 0, 255, 0);
	StretchDIBits(
		hdc,
		m_nDrawLeft,
		m_nDrawTop,
		m_pMipmap->Width * m_nZoom / PreviewZoomOne,
		m_pMipmap->Height * m_nZoom / PreviewZoomOne,
		0,
		0,
		m_pMipmap->Width,
		m_pMipmap->Height,
		m_pMipmap->as_span<xivres::util::b8g8r8a8>().data(),
		&bmi,
		DIB_RGB_COLORS,
		SRCCOPY);
	EndPaint(m_hWnd, &ps);

	return 0;
}

LRESULT App::FontEditorWindow::Window_OnInitMenuPopup(HMENU hMenu, int index, bool isWindowMenu) {
	for (int i = 0, i_ = GetMenuItemCount(hMenu); i < i_; i++) {
		if (GetMenuItemID(hMenu, i) == ID_FILE_EXIT) {
			PopulateRecentFilesMenu(hMenu);
			break;
		}
	}

	const auto setChecked = [hMenu](UINT id, bool checked) {
		const MENUITEMINFOW mii{.cbSize = sizeof mii, .fMask = MIIM_STATE, .fState = static_cast<UINT>(checked ? MFS_CHECKED : 0)};
		SetMenuItemInfoW(hMenu, id, FALSE, &mii);
	};
	for (const auto& [id, languageCode] : LanguageItems)
		setChecked(id, g_config.LanguageCode == languageCode);
	setChecked(ID_VIEW_WORDWRAP, m_bWordWrap);
	setChecked(ID_VIEW_KERNING, m_bKerning);
	setChecked(ID_VIEW_SHOWLINEMETRICS, m_bShowLineMetrics);
	for (const auto& mapping : ExportMappings)
		setChecked(mapping.MenuId, m_multiFontSet.*mapping.Enabled);
	for (const auto& [id, fontType] : HotReloadFontItems)
		setChecked(id, m_hotReloadFontType == fontType);

	{
		const auto i = ListView_GetNextItem(m_hFontElementsListView, -1, LVNI_SELECTED);
		const bool hasElement = m_pActiveFont && i >= 0;
		const bool notEmpty = hasElement && m_pActiveFont->Elements[i]->Source != Structs::ElementSourceEnum::Empty;
		const auto setState = [&](UINT id, bool enabled) {
			const MENUITEMINFOW mii{
				.cbSize = sizeof mii, .fMask = MIIM_STATE,
				.fState = static_cast<UINT>(enabled ? MFS_ENABLED : MFS_GRAYED)
			};
			SetMenuItemInfoW(hMenu, id, FALSE, &mii);
		};
		setState(ID_EDIT_DETAILS, hasElement);
		setState(ID_CONTEXTMENU_SHOWNEGATIVEBEARING, notEmpty);
		setState(ID_EDIT_CUT, hasElement);
		setState(ID_EDIT_COPY, hasElement);
		setState(ID_CONTEXTMENU_CLONE, hasElement);
		setState(ID_EDIT_DELETE, hasElement);
		setState(ID_EXPORT_GLYPHS, notEmpty);
		setState(ID_EXPORT_GLYPHSWITHADJUSTMENTS, notEmpty);
		setState(ID_EXPORT_FONTGLYPHS, m_pActiveFont != nullptr);

		const auto isGameFont = m_pActiveFont && FontChanger::FontsFromSystemFont::GetGameFontFamilyAndSize(m_pActiveFont->Name);
		setState(ID_EDIT_ADDELEMENTFROMFONT, m_pActiveFont != nullptr);
		setState(ID_EDIT_ADDFROMFONT, isGameFont);
		setState(ID_EDIT_AUTOFIT, isGameFont);
		setState(ID_EDIT_AUTOFITALLSIZES, isGameFont);
	}
	for (const auto& [id, alignment] : AlignmentItems) {
		const MENUITEMINFOW mii{
			.cbSize = sizeof mii,
			.fMask = MIIM_STATE | MIIM_FTYPE,
			.fType = MFT_RADIOCHECK,
			.fState = static_cast<UINT>((m_pActiveFont ? MFS_ENABLED : MFS_GRAYED) | (m_pActiveFont && m_pActiveFont->VerticalAlignment == alignment ? MFS_CHECKED : 0)),
		};
		SetMenuItemInfoW(hMenu, id, FALSE, &mii);
	}
	return 0;
}

LRESULT App::FontEditorWindow::Window_OnMouseMove(uint16_t states, int16_t x, int16_t y) {
	if (FontElementsListView_OnDragProcessMouseMove(x, y))
		return 0;

	if (m_activeSplitter != VerticalSplitter::None) {
		UpdateSplitterDragPosition(y);
		return 0;
	}

	if (m_previewPan.Active) {
		POINT pt;
		GetCursorPos(&pt);
		SetCursorPos(m_previewPan.DragRelativePosition.x, m_previewPan.DragRelativePosition.y);
		m_nPreviewScrollX += m_previewPan.DragRelativePosition.x - pt.x;
		m_nPreviewScrollY += m_previewPan.DragRelativePosition.y - pt.y;
		Window_Redraw();
		return 0;
	}

	return 0;
}

LRESULT App::FontEditorWindow::Window_OnMouseLButtonDown(uint16_t states, int16_t x, int16_t y) {
	if (m_bIsReorderingFontElementList)
		return 0;

	const auto splitter = HitTestSplitter(x, y);
	if (splitter != VerticalSplitter::None) {
		m_activeSplitter = splitter;
		SetCapture(m_hWnd);
		SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
		return 0;
	}

	if (x >= m_nDrawLeft && y >= m_nDrawTop && m_nPreviewMayScroll) {
		SetFocus(m_hWnd);
		m_previewPan.Active = true;
		GetCursorPos(&m_previewPan.StartPosition);
		m_previewPan.DragRelativePosition = {
			GetSystemMetrics(SM_CXSCREEN) / 2,
			GetSystemMetrics(SM_CYSCREEN) / 2,
		};
		if (const auto hm = MonitorFromPoint(m_previewPan.StartPosition, MONITOR_DEFAULTTONEAREST)) {
			if (MONITORINFO mi; GetMonitorInfoW(hm, &mi)) {
				m_previewPan.DragRelativePosition = {
					mi.rcMonitor.left + (mi.rcMonitor.right - mi.rcMonitor.left) / 2,
					mi.rcMonitor.top + (mi.rcMonitor.bottom - mi.rcMonitor.top) / 2,
				};
			}
		}
		SetCapture(m_hWnd);
		SetCursorPos(m_previewPan.DragRelativePosition.x, m_previewPan.DragRelativePosition.y);
		ShowCursor(FALSE);
	}

	return 0;
}

LRESULT App::FontEditorWindow::Window_OnMouseLButtonUp(uint16_t states, int16_t x, int16_t y) {
	if (FontElementsListView_OnDragProcessMouseUp(x, y))
		return 0;

	if (m_activeSplitter != VerticalSplitter::None) {
		EndSplitterDrag();
		return 0;
	}

	if (m_previewPan.Active) {
		// Inactive first, so that WM_CAPTURECHANGED does not show the cursor again.
		m_previewPan.Active = false;
		ReleaseCapture();
		SetCursorPos(m_previewPan.StartPosition.x, m_previewPan.StartPosition.y);
		ShowCursor(TRUE);
	}

	return 0;
}

LRESULT App::FontEditorWindow::Window_OnMouseWheel(int16_t delta, int16_t x, int16_t y) {
	POINT pt{x, y};
	ScreenToClient(m_hWnd, &pt);

	if (pt.x >= m_nDrawLeft && pt.y >= m_nDrawTop) {
		if (GetKeyState(VK_CONTROL) & 0x8000) {
			m_nZoom = std::clamp(
				m_nZoom + PreviewZoomScrollUnit * delta / WHEEL_DELTA,
				PreviewZoomMin,
				PreviewZoomMax);
		} else if (GetKeyState(VK_SHIFT) & 0x8000) {
			m_nPreviewScrollX = (std::max)(m_nPreviewScrollX - delta, 0);
		} else {
			m_nPreviewScrollY = (std::max)(m_nPreviewScrollY - delta, 0);
		}

		Window_Redraw();
		return 0;
	}

	return DefWindowProcW(m_hWnd, WM_MOUSEWHEEL, MAKEWPARAM(0, delta), MAKELPARAM(x, y));
}

LRESULT App::FontEditorWindow::Window_OnSetCursor(HWND hContainer, int hittest, int wm) {
	if (hContainer != m_hWnd || hittest != HTCLIENT)
		return DefWindowProcW(m_hWnd, WM_SETCURSOR, reinterpret_cast<WPARAM>(hContainer), MAKELPARAM(hittest, wm));

	POINT pt;
	GetCursorPos(&pt);
	ScreenToClient(m_hWnd, &pt);
	const auto x = static_cast<int16_t>(pt.x);
	const auto y = static_cast<int16_t>(pt.y);

	if (m_activeSplitter != VerticalSplitter::None ||
		HitTestSplitter(x, y) != VerticalSplitter::None)
		SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
	else if (x >= m_nDrawLeft && y >= m_nDrawTop && m_nPreviewMayScroll)
		SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
	else
		SetCursor(LoadCursorW(nullptr, IDC_ARROW));
	return TRUE;
}

LRESULT App::FontEditorWindow::Window_OnDestroy() {
	StopWatchingGlyphFolders();
	DeleteFont(m_hUiFont);
	PostQuitMessage(0);
	return 0;
}

void App::FontEditorWindow::Window_Redraw() {
	if (!m_pActiveFont)
		return;

	m_bNeedRedraw = true;
	InvalidateRect(m_hWnd, nullptr, FALSE);
}

LRESULT App::FontEditorWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == GetGlyphFoldersChangedMessage())
		return OnGlyphFoldersChanged();

	switch (msg) {
		case WM_COMMAND:
			for (const auto& [id, languageCode] : LanguageItems) {
				if (LOWORD(wParam) == id)
					return Menu_File_Language(languageCode);
			}
			for (const auto& [id, fontType] : HotReloadFontItems) {
				if (LOWORD(wParam) == id)
					return Menu_HotReload_Font(fontType);
			}
			for (const auto& [id, alignment] : AlignmentItems) {
				if (LOWORD(wParam) == id)
					return Menu_Edit_SetVerticalAlignment(alignment);
			}
			for (const auto& mapping : ExportMappings) {
				if (LOWORD(wParam) == mapping.MenuId)
					return Menu_Export_ToggleMapping(mapping);
			}
			switch (LOWORD(wParam)) {
				case Id_Edit: return Edit_OnCommand(HIWORD(wParam));
				case Id_FontListBox: return FontListBox_OnCommand(HIWORD(wParam));
				case ID_FILE_NEW_MAINGAMEFONT: return Menu_File_New(xivres::font_type::font);
				case ID_FILE_NEW_LOBBYFONT: return Menu_File_New(xivres::font_type::font_lobby);
				case ID_FILE_NEW_CHNAXIS: return Menu_File_New(xivres::font_type::chn_axis);
				case ID_FILE_NEW_TCAXIS: return Menu_File_New(xivres::font_type::tc_axis);
				case ID_FILE_NEW_KRNAXIS: return Menu_File_New(xivres::font_type::krn_axis);
				case ID_FILE_OPEN: return Menu_File_Open();
				case ID_FILE_SAVE: return Menu_File_Save();
				case ID_FILE_SAVEAS: return Menu_File_SaveAs(true);
				case ID_FILE_SAVECOPYAS: return Menu_File_SaveAs(false);
				case ID_FILE_RECENT_CLEAR: return Menu_File_ClearRecent();
				case ID_FILE_GAMEINSTALLATIONMANAGER: return Menu_File_GameInstallationManager();
				case ID_FILE_EXIT: return Menu_File_Exit();
				case ID_EDIT_ADD: return Menu_Edit_Add();
				case ID_EDIT_ADDELEMENTFROMFONT: return Menu_Edit_AddElementFromFont();
				case ID_EDIT_ADDFROMFONT: return Menu_Edit_AddFromFont();
				case ID_EDIT_AUTOFIT: return Menu_Edit_AutoFit(false);
				case ID_EDIT_AUTOFITALLSIZES: return Menu_Edit_AutoFit(true);
				case ID_EDIT_CUT: return Menu_Edit_Cut();
				case ID_EDIT_COPY: return Menu_Edit_Copy();
				case ID_EDIT_PASTE: return Menu_Edit_Paste();
				case ID_EDIT_DELETE: return Menu_Edit_Delete();
				case ID_EDIT_SELECTALL: return Menu_Edit_SelectAll();
				case ID_EDIT_DETAILS: return Menu_Edit_Details();
				case ID_EDIT_DECREASEVERTICALOFFSET: return Menu_Edit_ChangeParams(-1, 0, 0, 0);
				case ID_EDIT_INCREASEVERTICALOFFSET: return Menu_Edit_ChangeParams(+1, 0, 0, 0);
				case ID_EDIT_DECREASEHORIZONTALOFFSET: return Menu_Edit_ChangeParams(0, -1, 0, 0);
				case ID_EDIT_INCREASEHORIZONTALOFFSET: return Menu_Edit_ChangeParams(0, +1, 0, 0);
				case ID_EDIT_DECREASELETTERSPACING: return Menu_Edit_ChangeParams(0, 0, -1, 0);
				case ID_EDIT_INCREASELETTERSPACING: return Menu_Edit_ChangeParams(0, 0, +1, 0);
				case ID_EDIT_DECREASEFONTSIZEBY1: return Menu_Edit_ChangeParams(0, 0, 0, -1.f);
				case ID_EDIT_INCREASEFONTSIZEBY1: return Menu_Edit_ChangeParams(0, 0, 0, +1.f);
				case ID_EDIT_DECREASEFONTSIZEBY0_2: return Menu_Edit_ChangeParams(0, 0, 0, -0.2f);
				case ID_EDIT_INCREASEFONTSIZEBY0_2: return Menu_Edit_ChangeParams(0, 0, 0, +0.2f);
				case ID_EDIT_TOGGLEMERGEMODE: return Menu_Edit_ToggleMergeMode();
				case ID_EDIT_MOVEUP: return Menu_Edit_MoveUpOrDown(-1);
				case ID_EDIT_MOVEDOWN: return Menu_Edit_MoveUpOrDown(+1);
				case ID_EDIT_CREATEEMPTYCOPYFROMSELECTION: return Menu_Edit_CreateEmptyCopyFromSelection();
				case ID_CONTEXTMENU_CLONE: return FontElementsListView_Clone();
				case ID_CONTEXTMENU_SHOWNEGATIVEBEARING: return FontElementsListView_ShowNegativeBearingCodepoints();
				case ID_VIEW_PREVIOUSFONT: return Menu_View_NextOrPrevFont(-1);
				case ID_VIEW_NEXTFONT: return Menu_View_NextOrPrevFont(1);
				case ID_VIEW_WORDWRAP: return Menu_View_WordWrap();
				case ID_VIEW_KERNING: return Menu_View_Kerning();
				case ID_VIEW_SHOWLINEMETRICS: return Menu_View_ShowLineMetrics();
				case ID_VIEW_100: return Menu_View_Zoom(1 * PreviewZoomOne);
				case ID_VIEW_200: return Menu_View_Zoom(2 * PreviewZoomOne);
				case ID_VIEW_300: return Menu_View_Zoom(3 * PreviewZoomOne);
				case ID_VIEW_400: return Menu_View_Zoom(4 * PreviewZoomOne);
				case ID_VIEW_500: return Menu_View_Zoom(5 * PreviewZoomOne);
				case ID_VIEW_600: return Menu_View_Zoom(6 * PreviewZoomOne);
				case ID_VIEW_700: return Menu_View_Zoom(7 * PreviewZoomOne);
				case ID_VIEW_800: return Menu_View_Zoom(8 * PreviewZoomOne);
				case ID_VIEW_900: return Menu_View_Zoom(9 * PreviewZoomOne);
				case ID_EXPORT_PREVIEW: return Menu_Export_Preview();
				case ID_EXPORT_RAW: return Menu_Export_Raw();
				case ID_EXPORT_GLYPHS: return Menu_Export_Glyphs(false);
				case ID_EXPORT_GLYPHSWITHADJUSTMENTS: return Menu_Export_Glyphs(true);
				case ID_EXPORT_FONTGLYPHS: return Menu_Export_FontGlyphs();
				case ID_EXPORT_TOTTMP_COMPRESSWHILEPACKING: return Menu_Export_TTMP(CompressionMode::CompressWhilePacking);
				case ID_EXPORT_TOTTMP_COMPRESSAFTERPACKING: return Menu_Export_TTMP(CompressionMode::CompressAfterPacking);
				case ID_EXPORT_TOTTMP_DONOTCOMPRESS: return Menu_Export_TTMP(CompressionMode::DoNotCompress);
				case ID_HOTRELOAD_RELOAD: return Menu_HotReload_Reload(false);
				case ID_HOTRELOAD_RESTORE: return Menu_HotReload_Reload(true);
			}
			if (LOWORD(wParam) >= ID_FILE_RECENT_FIRST && LOWORD(wParam) <= ID_FILE_RECENT_LAST)
				return Menu_File_OpenRecent(LOWORD(wParam) - ID_FILE_RECENT_FIRST);
			break;

		case WM_NOTIFY:
			switch (auto& hdr = *reinterpret_cast<NMHDR*>(lParam); hdr.idFrom) {
				case Id_FontElementListView:
					switch (hdr.code) {
						case LVN_BEGINDRAG: return FontElementsListView_OnBeginDrag(*reinterpret_cast<NM_LISTVIEW*>(lParam));
						case NM_DBLCLK: return FontElementsListView_OnDblClick(*reinterpret_cast<NMITEMACTIVATE*>(lParam));
						case NM_RCLICK: return FontElementsListView_OnRightClick(*reinterpret_cast<NMITEMACTIVATE*>(lParam));
						case NM_CUSTOMDRAW: return FontElementsListView_OnCustomDraw(*reinterpret_cast<NMLVCUSTOMDRAW*>(lParam));
					}
					break;
			}
			return 0;

		case WM_CONTEXTMENU:
			if (reinterpret_cast<HWND>(wParam) == m_hFontsListBox)
				return FontListBox_OnContextMenu({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
			break;

		case WM_CREATE: return Window_OnCreate(hwnd);
		case WM_MOUSEMOVE: return Window_OnMouseMove(static_cast<uint16_t>(wParam), LOWORD(lParam), HIWORD(lParam));
		case WM_LBUTTONDOWN: return Window_OnMouseLButtonDown(static_cast<uint16_t>(wParam), LOWORD(lParam), HIWORD(lParam));
		case WM_LBUTTONUP: return Window_OnMouseLButtonUp(static_cast<uint16_t>(wParam), LOWORD(lParam), HIWORD(lParam));
		case WM_SIZE: return Window_OnSize();
		case WM_PAINT: return Window_OnPaint();
		case WM_INITMENUPOPUP: return Window_OnInitMenuPopup(reinterpret_cast<HMENU>(wParam), LOWORD(lParam), !!HIWORD(lParam));
		case WM_MOUSEWHEEL: return Window_OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam), LOWORD(lParam), HIWORD(lParam));
		case WM_SETCURSOR: return Window_OnSetCursor(reinterpret_cast<HWND>(wParam), LOWORD(lParam), HIWORD(lParam));
		case WM_CLOSE: return Menu_File_Exit();
		case WM_DESTROY: return Window_OnDestroy();
		case WM_CAPTURECHANGED:
			if (reinterpret_cast<HWND>(lParam) != m_hWnd) {
				m_activeSplitter = VerticalSplitter::None;
				if (m_previewPan.Active) {
					ShowCursor(TRUE);
					m_previewPan.Active = false;
				}
			}
			return 0;
	}

	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT WINAPI App::FontEditorWindow::WndProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	return reinterpret_cast<FontEditorWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))->WndProc(hwnd, msg, wParam, lParam);
}

LRESULT WINAPI App::FontEditorWindow::WndProcInitial(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg != WM_NCCREATE)
		return DefWindowProcW(hwnd, msg, wParam, lParam);

	const auto pCreateStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
	const auto pImpl = static_cast<FontEditorWindow*>(pCreateStruct->lpCreateParams);
	SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pImpl));
	SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProcStatic));

	return pImpl->WndProc(hwnd, msg, wParam, lParam);
}

App::FontEditorWindow::VerticalSplitter App::FontEditorWindow::HitTestSplitter(int16_t x, int16_t y) const {
	if (x < m_nDrawLeft)
		return VerticalSplitter::None;

	if (y >= m_splitterListEditTop && y < m_splitterListEditBottom)
		return VerticalSplitter::ListEdit;
	if (y >= m_splitterEditPreviewTop && y < m_splitterEditPreviewBottom)
		return VerticalSplitter::EditPreview;

	return VerticalSplitter::None;
}

void App::FontEditorWindow::UpdateSplitterDragPosition(int16_t y) {
	if (m_activeSplitter == VerticalSplitter::None)
		return;

	RECT rc;
	GetClientRect(m_hWnd, &rc);
	const auto zoom = GetZoom();
	const auto [minListPx, minEditPx, minPreviewPx] = GetMinimumHeights(zoom);
	const auto availableHeight = static_cast<int>(rc.bottom - rc.top);

	auto updated = false;
	switch (m_activeSplitter) {
		case VerticalSplitter::ListEdit: {
			const auto minY = minListPx;
			const auto maxYRaw = availableHeight - (m_splitterThicknessPx * 2 + minEditPx + minPreviewPx);
			const auto maxY = (std::max<int>)(minY, maxYRaw);
			const auto clampedY = std::clamp(static_cast<int>(y), minY, maxY);
			const auto newDip = (std::max)(MinFontElementListViewHeight, static_cast<int>(std::lround(clampedY / zoom)));
			if (newDip != g_config.FontElementListViewHeight) {
				g_config.FontElementListViewHeight = newDip;
				g_config.MarkDirty();
				updated = true;
			}
			break;
		}
		case VerticalSplitter::EditPreview: {
			const auto minY = m_splitterListEditBottom + minEditPx;
			const auto maxYRaw = availableHeight - (m_splitterThicknessPx + minPreviewPx);
			const auto effectiveMin = (std::min<int>)(minY, availableHeight);
			const auto maxY = (std::max<int>)(effectiveMin, maxYRaw);
			const auto clampedY = std::clamp(static_cast<int>(y), effectiveMin, maxY);
			const auto newEditPx = (std::max<int>)(minEditPx, clampedY - m_splitterListEditBottom);
			const auto newDip = (std::max)(MinPreviewEditHeight, static_cast<int>(std::lround(newEditPx / zoom)));
			if (newDip != g_config.PreviewEditHeight) {
				g_config.PreviewEditHeight = newDip;
				g_config.MarkDirty();
				updated = true;
			}
			break;
		}
		case VerticalSplitter::None:
			break;
	}

	if (updated) {
		Window_OnSize();
		Window_Redraw();
	}
}

void App::FontEditorWindow::EndSplitterDrag() {
	if (m_activeSplitter == VerticalSplitter::None)
		return;
	m_activeSplitter = VerticalSplitter::None;
	if (GetCapture() == m_hWnd)
		ReleaseCapture();
	g_config.SaveIfDirty();
}
