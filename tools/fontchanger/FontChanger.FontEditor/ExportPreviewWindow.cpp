#include "pch.h"
#include "resource.h"
#include "ExportPreviewWindow.h"
#include "FontChanger.Presets/Structs.h"

LRESULT App::ExportPreviewWindow::Window_OnCreate(HWND hwnd) {
	m_hWnd = hwnd;

	NONCLIENTMETRICSW ncm = {sizeof(NONCLIENTMETRICSW)};
	SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
	m_hUiFont = CreateFontIndirectW(&ncm.lfMessageFont);

	m_hFontsListBox = CreateWindowExW(0, WC_LISTBOXW, nullptr,
		WS_CHILD | WS_TABSTOP | WS_BORDER | WS_VISIBLE | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
		0, 0, 0, 0, m_hWnd, reinterpret_cast<HMENU>(Id_FontListBox), reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(m_hWnd, GWLP_HINSTANCE)), nullptr);
	m_hEdit = CreateWindowExW(0, WC_EDITW, nullptr,
		WS_CHILD | WS_TABSTOP | WS_BORDER | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_AUTOVSCROLL | ES_MULTILINE | ES_WANTRETURN,
		0, 0, 0, 0, m_hWnd, reinterpret_cast<HMENU>(Id_Edit), reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(m_hWnd, GWLP_HINSTANCE)), nullptr);

	SendMessage(m_hFontsListBox, WM_SETFONT, reinterpret_cast<WPARAM>(m_hUiFont), FALSE);
	SendMessage(m_hEdit, WM_SETFONT, reinterpret_cast<WPARAM>(m_hUiFont), FALSE);
	Edit_SetText(m_hEdit, xivres::util::unicode::convert<std::wstring>(Structs::GetDefaultPreviewText()).c_str());

	for (const auto& font : m_fonts)
		ListBox_AddString(m_hFontsListBox, xivres::util::unicode::convert<std::wstring>(font.first).c_str());
	ListBox_SetCurSel(m_hFontsListBox, 0);

	SetWindowSubclass(m_hEdit, [](HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) -> LRESULT {
		if (msg == WM_GETDLGCODE && wParam == VK_TAB)
			return 0;
		if (msg == WM_KEYDOWN && wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_SHIFT) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000) && !(GetKeyState(VK_LWIN) & 0x8000) && !(GetKeyState(VK_RWIN) & 0x8000))
			Edit_SetSel(hWnd, 0, Edit_GetTextLength(hWnd));
		return DefSubclassProc(hWnd, msg, wParam, lParam);
	}, 1, 0);

	Window_OnSize();
	ShowWindow(m_hWnd, SW_SHOW);
	return 0;
}

LRESULT App::ExportPreviewWindow::Window_OnSize() {
	RECT rc;
	GetClientRect(m_hWnd, &rc);

	const auto zoom = GetZoom();
	const auto scaledFontListBoxWidth = static_cast<int>(FontListBoxWidth * zoom);
	const auto scaledEditHeight = static_cast<int>(EditHeight * zoom);

	auto hdwp = BeginDeferWindowPos(Id__Last);
	hdwp = DeferWindowPos(hdwp, m_hFontsListBox, nullptr, 0, 0, scaledFontListBoxWidth, rc.bottom - rc.top, SWP_NOZORDER | SWP_NOACTIVATE);
	hdwp = DeferWindowPos(hdwp, m_hEdit, nullptr, scaledFontListBoxWidth, 0, (std::max<int>)(0, rc.right - rc.left - scaledFontListBoxWidth), scaledEditHeight, SWP_NOZORDER | SWP_NOACTIVATE);
	EndDeferWindowPos(hdwp);

	m_nDrawLeft = scaledFontListBoxWidth;
	m_nDrawTop = scaledEditHeight;
	m_pMipmap = std::make_shared<xivres::texture::memory_mipmap_stream>(
		(std::max<int>)(32, rc.right - rc.left - m_nDrawLeft),
		(std::max<int>)(32, rc.bottom - rc.top - m_nDrawTop),
		1,
		xivres::texture::formats::B8G8R8A8);

	m_bNeedRedraw = true;
	InvalidateRect(m_hWnd, nullptr, FALSE);

	return 0;
}

LRESULT App::ExportPreviewWindow::Window_OnPaint() {
	union {
		struct {
			BITMAPINFOHEADER bmih;
			DWORD bitfields[3];
		};

		BITMAPINFO bmi{};
	};

	PAINTSTRUCT ps;
	const auto hdc = BeginPaint(m_hWnd, &ps);
	if (m_bNeedRedraw) {
		m_bNeedRedraw = false;
		const auto pad = 16;
		const auto buf = m_pMipmap->as_span<xivres::util::b8g8r8a8>();
		std::ranges::fill(buf, xivres::util::b8g8r8a8{0x88, 0x88, 0x88, 0xFF});

		for (int y = pad; y < m_pMipmap->Height - pad; y++) {
			for (int x = pad; x < m_pMipmap->Width - pad; x++)
				buf[y * m_pMipmap->Width + x] = {0x00, 0x00, 0x00, 0xFF};
		}

		auto sel = ListBox_GetCurSel(m_hFontsListBox);
		sel = (std::max)(0, (std::min)(static_cast<int>(m_fonts.size() - 1), sel));
		if (sel < m_fonts.size()) {
			const auto& font = *m_fonts.at(sel).second;
			FontChanger::FixedSizeFont::text_measurer(font)
				.max_width(m_pMipmap->Width - pad * 2)
				.measure(GetWindowString(m_hEdit))
				.draw_to(*m_pMipmap, font, 16, 16, {0xFF, 0xFF, 0xFF, 0xFF}, {0, 0, 0, 0});
		}
	}

	bmih.biSize = sizeof bmih;
	bmih.biWidth = m_pMipmap->Width;
	bmih.biHeight = -m_pMipmap->Height;
	bmih.biPlanes = 1;
	bmih.biBitCount = 32;
	bmih.biCompression = BI_BITFIELDS;
	reinterpret_cast<xivres::util::b8g8r8a8*>(&bitfields[0])->set_components(255, 0, 0, 0);
	reinterpret_cast<xivres::util::b8g8r8a8*>(&bitfields[1])->set_components(0, 255, 0, 0);
	reinterpret_cast<xivres::util::b8g8r8a8*>(&bitfields[2])->set_components(0, 0, 255, 0);
	StretchDIBits(hdc, m_nDrawLeft, m_nDrawTop, m_pMipmap->Width, m_pMipmap->Height, 0, 0, m_pMipmap->Width, m_pMipmap->Height, m_pMipmap->as_span<xivres::util::b8g8r8a8>().data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
	EndPaint(m_hWnd, &ps);

	return 0;
}

LRESULT App::ExportPreviewWindow::Window_OnDestroy() {
	DeleteFont(m_hUiFont);
	SetWindowLongPtrW(m_hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(DefWindowProcW));
	delete this;
	return 0;
}

LRESULT App::ExportPreviewWindow::Edit_OnCommand(uint16_t commandId) {
	switch (commandId) {
		case EN_CHANGE:
			m_bNeedRedraw = true;
			InvalidateRect(m_hWnd, nullptr, FALSE);
			return 0;
	}

	return 0;
}

LRESULT App::ExportPreviewWindow::FontListBox_OnCommand(uint16_t commandId) {
	switch (commandId) {
		case LBN_SELCHANGE: {
			m_bNeedRedraw = true;
			InvalidateRect(m_hWnd, nullptr, FALSE);
			return 0;
		}
	}
	return 0;
}

LRESULT App::ExportPreviewWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_COMMAND:
			switch (LOWORD(wParam)) {
				case Id_Edit: return Edit_OnCommand(HIWORD(wParam));
				case Id_FontListBox: return FontListBox_OnCommand(HIWORD(wParam));
			}
			break;

		case WM_CREATE: return Window_OnCreate(hwnd);
		case WM_SIZE: return Window_OnSize();
		case WM_PAINT: return Window_OnPaint();
		case WM_DESTROY: return Window_OnDestroy();
	}

	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT WINAPI App::ExportPreviewWindow::WndProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	return reinterpret_cast<ExportPreviewWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))->WndProc(hwnd, msg, wParam, lParam);
}

LRESULT WINAPI App::ExportPreviewWindow::WndProcInitial(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg != WM_NCCREATE)
		return DefWindowProcW(hwnd, msg, wParam, lParam);

	const auto pCreateStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
	const auto pImpl = reinterpret_cast<ExportPreviewWindow*>(pCreateStruct->lpCreateParams);
	SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pImpl));
	SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProcStatic));

	return pImpl->WndProc(hwnd, msg, wParam, lParam);
}

double App::ExportPreviewWindow::GetZoom() const noexcept {
	return GetZoomFromWindow(m_hWnd);
}

App::ExportPreviewWindow::ExportPreviewWindow(std::vector<std::pair<std::string, std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font>>> fonts) : m_fonts(fonts) {
	WNDCLASSEXW wcex{};
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.hInstance = g_hInstance;
	wcex.hIcon = LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_FONTEDITOR));
	wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	wcex.hbrBackground = GetStockBrush(WHITE_BRUSH);
	wcex.lpszClassName = ClassName;
	wcex.lpfnWndProc = WndProcInitial;

	RegisterClassExW(&wcex);

	CreateWindowExW(0, ClassName, L"Export Preview", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT, 1200, 640,
		nullptr, nullptr, nullptr, this);
}

bool App::ExportPreviewWindow::ConsumeDialogMessage(MSG& msg) {
	if (IsDialogMessage(m_hWnd, &msg))
		return true;

	return false;
}

bool App::ExportPreviewWindow::ConsumeAccelerator(MSG& msg) {
	return false;
}
