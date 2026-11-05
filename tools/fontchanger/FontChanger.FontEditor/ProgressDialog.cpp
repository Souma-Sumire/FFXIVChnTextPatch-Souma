#include "pch.h"
#include "resource.h"
#include "ProgressDialog.h"

struct App::ProgressDialog::ControlStruct {
	HWND Window;
	HWND CancelButton = GetDlgItem(Window, IDCANCEL);
	HWND StepNameStatic = GetDlgItem(Window, IDC_STATIC_STEPNAME);
	HWND ProgrssBar = GetDlgItem(Window, IDC_PROGRESS);
};

App::ProgressDialog::ProgressDialog(HWND hParentWnd, std::wstring windowTitle)
	: m_hParentWnd(hParentWnd)
	, m_windowTitle(std::move(windowTitle)) {
	m_hReadyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!m_hReadyEvent)
		throw std::runtime_error("Failed to open progress dialog");

	bool bFailed = false;
	m_dialogThread = std::thread([this, &bFailed]() {
		const auto hglob = LoadResourceWithLanguageFallback(RT_DIALOG, IDD_PROGRESS);
		if (-1 == DialogBoxIndirectParamW(
			g_hInstance,
			reinterpret_cast<DLGTEMPLATE*>(LockResource(hglob.get())),
			nullptr,
			DlgProcStatic,
			reinterpret_cast<LPARAM>(this))) {
			bFailed = true;
			SetEvent(m_hReadyEvent);
		}
	});

	WaitForSingleObject(m_hReadyEvent, INFINITE);
	CloseHandle(m_hReadyEvent);
	m_hReadyEvent = nullptr;
	if (bFailed)
		throw std::runtime_error("Failed to open progress dialog");

	if (m_controls)
		SetForegroundWindow(m_controls->Window);
}

App::ProgressDialog::~ProgressDialog() {
	SendMessageW(m_controls->Window, WM_CLOSE, 0, 1);
	m_dialogThread.join();
	delete m_controls;
}

void App::ProgressDialog::ThrowIfCancelled() const {
	if (IsCancelled())
		throw ProgressDialogCancelledError();
}

bool App::ProgressDialog::IsCancelled() const {
	return m_bCancelled;
}

void App::ProgressDialog::UpdateStatusMessage(const std::wstring& s) {
	Static_SetText(m_controls->StepNameStatic, s.c_str());
}

void App::ProgressDialog::UpdateStatusMessage(std::wstring_view s) {
	UpdateStatusMessage(std::wstring(s));
}

void App::ProgressDialog::UpdateProgress(float progress) {
	if (std::isnan(progress)) {
		SendMessageW(m_controls->ProgrssBar, PBM_SETMARQUEE, TRUE, 0);
	} else {
		const auto progInt = static_cast<int>(10000.f * progress);
		SendMessageW(m_controls->ProgrssBar, PBM_SETMARQUEE, FALSE, 0);
		SendMessageW(m_controls->ProgrssBar, PBM_SETPOS, progInt, 0);
	}
}

INT_PTR App::ProgressDialog::Dialog_OnInitDialog() {
	SetWindowTextW(m_controls->Window, m_windowTitle.c_str());
	SendMessageW(m_controls->ProgrssBar, PBM_SETRANGE32, 0, 10000);
	UpdateProgress(std::nanf(""));

	CenterWindowOnParent(m_controls->Window, m_hParentWnd);

	SetEvent(m_hReadyEvent);

	return 0;
}

INT_PTR App::ProgressDialog::CancelButton_OnCommand(uint16_t notiCode) {
	m_bCancelled = true;
	EnableWindow(m_controls->CancelButton, FALSE);
	return 0;
}

INT_PTR App::ProgressDialog::DlgProc(UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
		case WM_INITDIALOG:
			return Dialog_OnInitDialog();
		case WM_COMMAND: {
			switch (LOWORD(wParam)) {
				case IDCANCEL: return CancelButton_OnCommand(HIWORD(wParam));
			}
			return 0;
		}
		case WM_CLOSE: {
			if (lParam == 1) {
				SetForegroundWindow(m_hParentWnd);
				EndDialog(m_controls->Window, 0);
			} else
				return 1;
			return 0;
		}
	}
	return 0;
}

INT_PTR __stdcall App::ProgressDialog::DlgProcStatic(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
	if (message == WM_INITDIALOG) {
		auto& params = *reinterpret_cast<ProgressDialog*>(lParam);
		params.m_controls = new ControlStruct{hwnd};
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&params));
		return params.DlgProc(message, wParam, lParam);
	} else {
		return reinterpret_cast<ProgressDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))->DlgProc(message, wParam, lParam);
	}
}
