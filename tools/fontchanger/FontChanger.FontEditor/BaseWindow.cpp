#include "pch.h"
#include "BaseWindow.h"

std::set<App::BaseWindow*> App::BaseWindow::s_windows;

App::BaseWindow::BaseWindow() {
	s_windows.insert(this);
}

App::BaseWindow::~BaseWindow() {
	s_windows.erase(this);
}

bool App::BaseWindow::ConsumeMessage(MSG& msg) {
	for (auto& window : s_windows) {
		if (window->ConsumeAccelerator(msg))
			return true;
		if (window->ConsumeDialogMessage(msg))
			return true;
	}
	return false;
}
