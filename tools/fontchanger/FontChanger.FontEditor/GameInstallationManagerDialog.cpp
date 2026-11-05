#include "pch.h"
#include "GameInstallationManagerDialog.h"
#include "resource.h"

static constexpr GUID Guid_IFileDialog_GameExecutablePath{ 0x5c2fc703, 0x7406, 0x4704, {0x92, 0x12, 0xae, 0x41, 0x1d, 0x4b, 0x74, 0x69} };

struct App::GameInstallationManagerDialog::ControlStruct {
	HWND Window;
	HWND OkButton = GetDlgItem(Window, IDOK);
	HWND CancelButton = GetDlgItem(Window, IDCANCEL);
	HWND PathList = GetDlgItem(Window, IDC_PATHLIST);
	HWND DetectButton = GetDlgItem(Window, IDC_DETECT);
	HWND AddButton = GetDlgItem(Window, IDC_ADD);
	HWND RemoveButton = GetDlgItem(Window, IDC_REMOVE);
};

enum : uint8_t {
	ListViewColsPath,
	ListViewColsVendor,
	ListViewColsExists,
};

App::GameInstallationManagerDialog::Installation::Installation(std::filesystem::path path, GameReleaseVendor vendor)
	: Path(path)
	, Vendor(vendor) {
	try {
		Exists = exists(path);
	} catch (...) {
		Exists = false;
	}
}

std::optional<FontGeneratorConfig> App::GameInstallationManagerDialog::Show(HWND hParentWnd, const FontGeneratorConfig& config) {
	const auto hglob = LoadResourceWithLanguageFallback(RT_DIALOG, IDD_GAMEINSTALLATIONMANAGER);

	GameInstallationManagerDialog dlg(hParentWnd, config);
	DialogBoxIndirectParamW(
		g_hInstance,
		static_cast<DLGTEMPLATE*>(LockResource(hglob.get())),
		hParentWnd,
		DlgProcStatic,
		reinterpret_cast<LPARAM>(&dlg));
	return std::move(dlg.m_result);
}

App::GameInstallationManagerDialog::GameInstallationManagerDialog(HWND hParentWnd, const FontGeneratorConfig& config)
	: m_hParentWnd(hParentWnd)
	, m_prevConfig(config) {
}

App::GameInstallationManagerDialog::~GameInstallationManagerDialog() {
	delete m_controls;
}

INT_PTR App::GameInstallationManagerDialog::Dialog_OnInitDialog() {
	m_controls = new ControlStruct{ m_hWnd };
	ListView_SetExtendedListViewStyle(m_controls->PathList, LVS_EX_FULLROWSELECT);

	AddListViewColumn(m_controls->PathList, ListViewColsPath, 180, IDS_GAMEINSTALLATIONLISTVIEW_COLUMN_PATH);
	AddListViewColumn(m_controls->PathList, ListViewColsVendor, 80, IDS_GAMEINSTALLATIONLISTVIEW_COLUMN_VENDOR);
	AddListViewColumn(m_controls->PathList, ListViewColsExists, 80, IDS_GAMEINSTALLATIONLISTVIEW_COLUMN_EXISTS);

	for (size_t i = 0; i < m_prevConfig.GamePaths.size(); i++) {
		for (const auto& p : m_prevConfig.GamePaths[i])
			AddInstallation(p, static_cast<GameReleaseVendor>(i + 1));
	}

	return 0;
}

INT_PTR App::GameInstallationManagerDialog::OkButton_OnCommand(uint16_t notiCode) {
	auto& newConfig = m_result.emplace(m_prevConfig);
	newConfig.GamePaths = {};
	for (const auto& installation : m_installations)
		newConfig.GamePaths[static_cast<size_t>(installation.Vendor) - 1].emplace_back(installation.Path);
	EndDialog(m_controls->Window, IDOK);
	return 0;
}

INT_PTR App::GameInstallationManagerDialog::CancelButton_OnCommand(uint16_t notiCode) {
	EndDialog(m_controls->Window, 0);
	return 0;
}

INT_PTR App::GameInstallationManagerDialog::DetectButton_OnCommand(uint16_t notiCode) {
	auto found = 0;
	for (auto& [region, path] : GameInstallationRepository::AutoDetectInstalledGameReleases()) {
		if (std::ranges::all_of(m_installations, [&](const auto& item) { return item.Path != path; })) {
			AddInstallation(std::move(path), region);
			found++;
		}
	}

	switch (found) {
		case 0:
			MessageBoxW(
				m_hWnd,
				std::wstring(GetStringResource(IDS_GAMEINSTALLATIONDETECT_FOUND_0)).c_str(),
				GetWindowString(m_hWnd).c_str(),
				MB_OK | MB_ICONWARNING);
			break;
		case 1:
			MessageBoxW(
				m_hWnd,
				std::wstring(GetStringResource(IDS_GAMEINSTALLATIONDETECT_FOUND_1)).c_str(),
				GetWindowString(m_hWnd).c_str(),
				MB_OK | MB_ICONINFORMATION);
			break;
		default:
			MessageBoxW(
				m_hWnd,
				std::vformat(GetStringResource(IDS_GAMEINSTALLATIONDETECT_FOUND_N), std::make_wformat_args(found)).c_str(),
				GetWindowString(m_hWnd).c_str(),
				MB_OK | MB_ICONINFORMATION);
			break;
	}

	return INT_PTR();
}

INT_PTR App::GameInstallationManagerDialog::AddButton_OnCommand(uint16_t notiCode) {
	using namespace FontChanger::FixedSizeFont;

	const auto allFilesName = std::wstring(GetStringResource(IDS_FILTERSPEC_ALLFILES));
	COMDLG_FILTERSPEC fileTypes[] = {
		{L"ffxiv.exe, ffxiv_dx11.exe", L"ffxiv.exe;ffxiv_dx11.exe"},
		{allFilesName.c_str(), L"*"},
	};

	return TryCatchShowError(m_hWnd, IDS_ERROR_OPENFILEFAILURE_BODY, INT_PTR{1}, [&]() -> INT_PTR {
		const auto file = PickFile(m_hWnd, false, Guid_IFileDialog_GameExecutablePath, IDS_WINDOWTITLE_OPEN, fileTypes);
		if (!file)
			return 0;

		std::filesystem::path path;
		const auto vendor = GameInstallationRepository::DetermineGameRelease(*file, path);
		if (vendor == GameReleaseVendor::None) {
			MessageBoxW(
				m_hWnd,
				std::wstring(GetStringResource(IDS_GAMEINSTALLATIONADD_NOTAGAME)).c_str(),
				GetWindowString(m_hWnd).c_str(),
				MB_OK | MB_ICONWARNING);
			return 1;
		}

		if (std::ranges::all_of(m_installations, [&](const auto& item) { return item.Path != path; }))
			AddInstallation(std::move(path), vendor);
		return 0;
	});
}

INT_PTR App::GameInstallationManagerDialog::RemoveButton_OnCommand(uint16_t notiCode) {
	if (!ListView_GetSelectedCount(m_controls->PathList))
		return 0;

	// Items refer to installations by their indices, so the list is made again from the remaining ones, in its order.
	std::vector<Installation> remaining;
	for (int i = 0, i_ = ListView_GetItemCount(m_controls->PathList); i < i_; i++) {
		LVITEMW lvi{.mask = LVIF_PARAM | LVIF_STATE, .iItem = i, .stateMask = LVIS_SELECTED};
		ListView_GetItem(m_controls->PathList, &lvi);
		if (!(lvi.state & LVIS_SELECTED))
			remaining.emplace_back(std::move(m_installations[lvi.lParam]));
	}

	m_installations.clear();
	ListView_DeleteAllItems(m_controls->PathList);
	for (auto& installation : remaining)
		AddInstallation(std::move(installation.Path), installation.Vendor);

	EnableWindow(m_controls->RemoveButton, FALSE);
	return 0;
}

INT_PTR App::GameInstallationManagerDialog::PathListView_ItemChanged(const NMLISTVIEW& nm) {
	EnableWindow(m_controls->RemoveButton, !!ListView_GetSelectedCount(m_controls->PathList));
	return 0;
}

INT_PTR App::GameInstallationManagerDialog::PathListView_ColumnClick(const NMLISTVIEW& nm) {
	if (nm.iSubItem == m_sortCol) {
		m_sortAscending = !m_sortAscending;
	} else {
		m_sortCol = nm.iSubItem;
		m_sortAscending = true;
	}

	ListView_SortItems(nm.hdr.hwndFrom, [](LPARAM lp1, LPARAM lp2, LPARAM sortParam) -> int {
		auto& t = *reinterpret_cast<GameInstallationManagerDialog*>(sortParam);
		const auto m = t.m_sortAscending ? 1 : -1;
		switch (t.m_sortCol) {
			case ListViewColsPath:
				return m * _wcsicmp(t.m_installations[lp1].Path.c_str(), t.m_installations[lp2].Path.c_str());
			case ListViewColsVendor:
				return m * (static_cast<int>(t.m_installations[lp1].Vendor) - static_cast<int>(t.m_installations[lp2].Vendor));
			case ListViewColsExists:
				return m * (static_cast<int>(t.m_installations[lp1].Exists) - static_cast<int>(t.m_installations[lp2].Exists));
			default:
				return 0;
		}
		}, this);
	return 0;
}

INT_PTR App::GameInstallationManagerDialog::PathListView_DoubleClick(const NMITEMACTIVATE& nm) {
	const auto index = static_cast<size_t>(nm.lParam);
	if (index < m_installations.size())
		ShellExecuteW(m_hWnd, L"explore", m_installations[index].Path.c_str(), nullptr, nullptr, SW_SHOW);
	
	return 0;
}

void App::GameInstallationManagerDialog::AddInstallation(std::filesystem::path path, GameReleaseVendor vendor) {
	const auto i = static_cast<int>(m_installations.size());
	const auto& inst = m_installations.emplace_back(path, vendor);
	LVITEMW lvi{ .mask = LVIF_PARAM, .iItem = i, .lParam = static_cast<LPARAM>(i) };
	ListView_InsertItem(m_controls->PathList, &lvi);
	ListView_SetItemText(m_controls->PathList, i, ListViewColsPath, const_cast<wchar_t*>(inst.Path.c_str()));

	std::wstring tmp(GetStringResource(FontGeneratorConfig::GameReleases[static_cast<size_t>(inst.Vendor) - 1].VendorNameResId));
	ListView_SetItemText(m_controls->PathList, i, ListViewColsVendor, const_cast<wchar_t*>(tmp.c_str()));

	tmp = GetStringResource(inst.Exists
		? IDS_GAMEINSTALLATIONLISTVIEW_COLUMN_EXISTS_YES
		: IDS_GAMEINSTALLATIONLISTVIEW_COLUMN_EXISTS_NO);
	ListView_SetItemText(m_controls->PathList, i, ListViewColsExists, const_cast<wchar_t*>(tmp.c_str()));
}

INT_PTR App::GameInstallationManagerDialog::DlgProc(UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
		case WM_INITDIALOG:
			return Dialog_OnInitDialog();
		case WM_COMMAND: {
			switch (LOWORD(wParam)) {
				case IDOK: return OkButton_OnCommand(HIWORD(wParam));
				case IDCANCEL: return CancelButton_OnCommand(HIWORD(wParam));
				case IDC_DETECT: return DetectButton_OnCommand(HIWORD(wParam));
				case IDC_ADD: return AddButton_OnCommand(HIWORD(wParam));
				case IDC_REMOVE: return RemoveButton_OnCommand(HIWORD(wParam));
			}
			return 0;
		}
		case WM_NOTIFY: {
			const auto& nmhdr = *reinterpret_cast<LPNMHDR>(lParam);
			switch (nmhdr.idFrom) {
				case IDC_PATHLIST: {
					switch (nmhdr.code) {
						case LVN_ITEMCHANGED: return PathListView_ItemChanged(*reinterpret_cast<LPNMLISTVIEW>(lParam));
						case LVN_COLUMNCLICK: return PathListView_ColumnClick(*reinterpret_cast<LPNMLISTVIEW>(lParam));
						case NM_DBLCLK: return PathListView_DoubleClick(*reinterpret_cast<LPNMITEMACTIVATE>(lParam));
					}
					return 0;
				}
			}
			return 0;
		}

		case WM_CLOSE: {
			EndDialog(m_controls->Window, 0);
			return 0;
		}
		case WM_DESTROY: {
			return 0;
		}
	}
	return 0;
}

INT_PTR __stdcall App::GameInstallationManagerDialog::DlgProcStatic(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
	return DlgProcStaticImpl<GameInstallationManagerDialog>(hwnd, message, wParam, lParam);
}
