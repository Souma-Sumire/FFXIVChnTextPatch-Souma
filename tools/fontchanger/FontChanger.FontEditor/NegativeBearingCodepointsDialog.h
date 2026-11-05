#pragma once

namespace App {
	class NegativeBearingCodepointsDialog {
		struct ControlStruct;
		HWND m_hWnd{};
		ControlStruct* m_controls{};
		const std::vector<std::pair<char32_t, int>> m_entries;

	public:
		static void Show(HWND hParentWnd, std::vector<std::pair<char32_t, int>> entries);

		template<typename T>
		friend INT_PTR __stdcall ::DlgProcStaticImpl(HWND, UINT, WPARAM, LPARAM);

	private:
		NegativeBearingCodepointsDialog(std::vector<std::pair<char32_t, int>> entries);
		~NegativeBearingCodepointsDialog();

		INT_PTR Dialog_OnInitDialog();
		INT_PTR DlgProc(UINT message, WPARAM wParam, LPARAM lParam);
		static INT_PTR __stdcall DlgProcStatic(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
	};
}
