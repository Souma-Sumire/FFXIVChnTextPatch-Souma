#pragma once

namespace App {
	class ProgressDialog {
		struct ControlStruct;

		const HWND m_hParentWnd;
		const std::wstring m_windowTitle;

		HANDLE m_hReadyEvent = nullptr;

		ControlStruct* m_controls = nullptr;

		std::thread m_dialogThread;
		bool m_bCancelled = false;

	public:
		ProgressDialog(HWND hParentWnd, std::wstring windowTitle);

		~ProgressDialog();

		class ProgressDialogCancelledError : public std::runtime_error {
		public:
			ProgressDialogCancelledError() : std::runtime_error("Cancelled by user") {}
		};

		void ThrowIfCancelled() const;

		bool IsCancelled() const;

		void UpdateStatusMessage(const std::wstring& s);
		void UpdateStatusMessage(std::wstring_view s);

		void UpdateProgress(float progress);

	private:
		INT_PTR Dialog_OnInitDialog();

		INT_PTR CancelButton_OnCommand(uint16_t notiCode);

		INT_PTR DlgProc(UINT message, WPARAM wParam, LPARAM lParam);

		static INT_PTR __stdcall DlgProcStatic(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
	};
}
