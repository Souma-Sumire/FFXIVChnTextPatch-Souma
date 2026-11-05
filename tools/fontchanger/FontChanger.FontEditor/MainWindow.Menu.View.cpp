#include "pch.h"
#include "FontChanger.Presets/Structs.h"
#include "MainWindow.h"
#include "xivres/textools.h"

LRESULT App::FontEditorWindow::Menu_View_NextOrPrevFont(int direction) {
	auto i = static_cast<size_t>(ListBox_GetCurSel(m_hFontsListBox));
	if (i == static_cast<size_t>(LB_ERR)
		|| i + direction < 0
		|| i + direction >= ListBox_GetCount(m_hFontsListBox))
		return 0;

	ListBox_SetCurSel(m_hFontsListBox, i + direction);
	return FontListBox_OnCommand(LBN_SELCHANGE);
}

LRESULT App::FontEditorWindow::Menu_View_WordWrap() {
	m_bWordWrap = !m_bWordWrap;
	Window_Redraw();
	return 0;
}

LRESULT App::FontEditorWindow::Menu_View_Kerning() {
	m_bKerning = !m_bKerning;
	Window_Redraw();
	return 0;
}

LRESULT App::FontEditorWindow::Menu_View_ShowLineMetrics() {
	m_bShowLineMetrics = !m_bShowLineMetrics;
	Window_Redraw();
	return 0;
}

LRESULT App::FontEditorWindow::Menu_View_Zoom(int zoom) {
	m_nZoom = zoom;
	Window_Redraw();
	return 0;
}
