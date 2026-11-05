#pragma once

#include "FontGeneratorConfig.h"
#include "GameInstallationRepository.h"

namespace App {
	class GameInstallationManagerDialog {
		struct ControlStruct;

		struct Installation {
			std::filesystem::path Path;
			GameReleaseVendor Vendor;
			bool Exists;

			Installation(std::filesystem::path path, GameReleaseVendor vendor);
		};

		const HWND m_hParentWnd;
		const FontGeneratorConfig m_prevConfig;
		HWND m_hWnd{};
		ControlStruct* m_controls{};

		std::vector<Installation> m_installations;
		std::optional<FontGeneratorConfig> m_result;
		int m_sortCol = 0;
		bool m_sortAscending = true;

	public:
		static std::optional<FontGeneratorConfig> Show(HWND hParentWnd, const FontGeneratorConfig& config);

		template<typename T>
		friend INT_PTR __stdcall ::DlgProcStaticImpl(HWND, UINT, WPARAM, LPARAM);

	private:
		GameInstallationManagerDialog(HWND hParentWnd, const FontGeneratorConfig& config);

		~GameInstallationManagerDialog();

		INT_PTR Dialog_OnInitDialog();

		INT_PTR OkButton_OnCommand(uint16_t notiCode);

		INT_PTR CancelButton_OnCommand(uint16_t notiCode);

		INT_PTR DetectButton_OnCommand(uint16_t notiCode);

		INT_PTR AddButton_OnCommand(uint16_t notiCode);

		INT_PTR RemoveButton_OnCommand(uint16_t notiCode);

		INT_PTR PathListView_ItemChanged(const NMLISTVIEW& nm);

		INT_PTR PathListView_ColumnClick(const NMLISTVIEW& nm);

		INT_PTR PathListView_DoubleClick(const NMITEMACTIVATE& nm);

		void AddInstallation(std::filesystem::path path, GameReleaseVendor vendor);

		INT_PTR DlgProc(UINT message, WPARAM wParam, LPARAM lParam);

		static INT_PTR __stdcall DlgProcStatic(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
	};
}
