#include "pch.h"
#include "FontChanger.Presets/Structs.h"
#include "MainWindow.h"
#include "MainWindow.Internal.h"
#include "xivres/textools.h"
#include "resource.h"
#include "FileHistory.h"
#include "FontGeneratorConfig.h"
#include "GameInstallationManagerDialog.h"

#include <Shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

LRESULT App::FontEditorWindow::Menu_File_New(xivres::font_type fontType) {
	if (Changes_ConfirmIfDirty())
		return 1;

	Structs::MultiFontSet mfs;
	auto& fontSet = *mfs.FontSets.emplace_back(std::make_unique<Structs::FontSet>(Structs::FontSet::NewFromTemplateFont(fontType)));
	fontSet.ExpectedTexCount = GetGameTextureCount(fontSet.TexFilenameFormat).value_or(7);

	SetCurrentMultiFontSet(std::move(mfs), {});
	return 0;
}

// The file types of the dialogs that open and save configuration files.
static std::array<COMDLG_FILTERSPEC, 2> GetJsonFileTypes() {
	static const std::wstring presetJsonFilesName(GetStringResource(IDS_FILTERSPEC_PRESETJSONFILES));
	static const std::wstring allFilesName(GetStringResource(IDS_FILTERSPEC_ALLFILES));
	return {{
		{presetJsonFilesName.c_str(), L"*.json"},
		{allFilesName.c_str(), L"*"},
	}};
}

static void WriteTextFile(const std::filesystem::path& path, const std::string& text) {
	std::ofstream out(path, std::ios::binary);
	if (!out)
		throw std::system_error(std::error_code(static_cast<int>(GetLastError()), std::system_category()));
	out << text;
	out.close();
	if (!out)
		throw std::system_error(std::error_code(ERROR_WRITE_FAULT, std::system_category()));
}

LRESULT App::FontEditorWindow::Menu_File_Open() {
	if (Changes_ConfirmIfDirty())
		return 1;

	return TryCatchShowError(m_hWnd, IDS_ERROR_OPENFILEFAILURE_BODY, LRESULT{1}, [&]() -> LRESULT {
		if (const auto path = PickFile(m_hWnd, false, Guid_IFileDialog_Json, IDS_WINDOWTITLE_OPEN, GetJsonFileTypes()))
			OpenFile(*path);
		return 0;
	});
}

LRESULT App::FontEditorWindow::Menu_File_Save() {
	if (m_currentPath.empty())
		return Menu_File_SaveAs(true);

	return TryCatchShowError(m_hWnd, IDS_ERROR_SAVEFILEFAILURE_BODY, LRESULT{1}, [&]() -> LRESULT {
		WriteTextFile(m_currentPath, nlohmann::json(m_multiFontSet).dump(1, '\t'));
		Changes_MarkFresh();
		FileHistory::Add(m_currentPath);
		return 0;
	});
}

LRESULT App::FontEditorWindow::Menu_File_SaveAs(bool changeCurrentFile) {
	return TryCatchShowError(m_hWnd, IDS_ERROR_SAVEFILEFAILURE_BODY, LRESULT{1}, [&]() -> LRESULT {
		const auto path = PickFile(m_hWnd, true, Guid_IFileDialog_Json, IDS_WINDOWTITLE_SAVE, GetJsonFileTypes(), GetCurrentFileName(), L"json");
		if (!path)
			return 0;

		WriteTextFile(*path, nlohmann::json(m_multiFontSet).dump(1, '\t'));
		FileHistory::Add(*path);
		if (changeCurrentFile) {
			m_currentPath = *path;
			UpdateProjectDirectory();
			Changes_MarkFresh();
		}
		return 0;
	});
}

void App::FontEditorWindow::PopulateRecentFilesMenu(HMENU hMenu) {
	// Remove recent items from the last time
	for (auto i = GetMenuItemCount(hMenu); i-- > 0;) {
		MENUITEMINFOW mii{.cbSize = sizeof mii, .fMask = MIIM_ID};
		if (GetMenuItemInfoW(hMenu, i, TRUE, &mii) && mii.wID >= ID_FILE_RECENT_CLEAR && mii.wID <= ID_FILE_RECENT_LAST)
			DeleteMenu(hMenu, i, MF_BYPOSITION);
	}

	const auto history = FileHistory::Load();
	if (history.Files.empty())
		return;

	// The files go above the separator before Exit, as a group of their own.
	auto position = 0;
	for (auto i = 0, i_ = GetMenuItemCount(hMenu); i < i_; i++) {
		if (GetMenuItemID(hMenu, i) == ID_FILE_EXIT)
			position = (std::max)(0, i - 1);
	}
	const auto insert = [&](UINT type, UINT id, const wchar_t* text) {
		const MENUITEMINFOW mii{
			.cbSize = sizeof mii,
			.fMask = MIIM_FTYPE | MIIM_ID | (text ? MIIM_STRING : 0u),
			.fType = type,
			.wID = id,
			.dwTypeData = const_cast<wchar_t*>(text),
		};
		InsertMenuItemW(hMenu, position++, TRUE, &mii);
	};

	insert(MFT_SEPARATOR, ID_FILE_RECENT_SEPARATOR, nullptr);
	for (size_t i = 0; i < history.Files.size() && i <= ID_FILE_RECENT_LAST - ID_FILE_RECENT_FIRST; i++) {
		// Long paths are shortened in the middle; ampersands in paths are not mnemonics.
		wchar_t compact[MAX_PATH]{};
		if (!PathCompactPathExW(compact, history.Files[i].c_str(), 64, 0))
			wcsncpy_s(compact, history.Files[i].c_str(), _TRUNCATE);
		std::wstring escaped;
		for (const auto c : std::wstring_view(compact)) {
			if (c == L'&')
				escaped += L'&';
			escaped += c;
		}

		const auto label = i < 9 ? std::format(L"&{} {}", i + 1, escaped) : std::format(L"1&0 {}", escaped);
		insert(MFT_STRING, ID_FILE_RECENT_FIRST + static_cast<UINT>(i), label.c_str());
	}
	insert(MFT_STRING, ID_FILE_RECENT_CLEAR, std::wstring(GetStringResource(IDS_RECENTFILES_CLEAR)).c_str());
}

LRESULT App::FontEditorWindow::Menu_File_OpenRecent(size_t index) {
	const auto history = FileHistory::Load();
	if (index >= history.Files.size())
		return 0;

	const auto path = history.Files[index];
	if (!std::filesystem::exists(path)) {
		if (MessageBoxW(
			m_hWnd,
			std::format(L"{}\n\n{}", path.wstring(), std::wstring(GetStringResource(IDS_RECENTFILES_NOTFOUND))).c_str(),
			std::wstring(GetStringResource(IDS_APP)).c_str(),
			MB_YESNO | MB_ICONWARNING) == IDYES)
			FileHistory::Remove(path);
		return 0;
	}

	if (Changes_ConfirmIfDirty())
		return 1;

	return TryCatchShowError(m_hWnd, IDS_ERROR_OPENFILEFAILURE_BODY, LRESULT{1}, [&]() -> LRESULT {
		OpenFile(path);
		return 0;
	});
}

LRESULT App::FontEditorWindow::Menu_File_ClearRecent() {
	FileHistory::Clear();
	return 0;
}

LRESULT App::FontEditorWindow::Menu_File_Language(const char* languageCode) {
	if (g_config.LanguageCode != languageCode) {
		g_config.LanguageCode = languageCode;
		g_config.Save();

		const auto langId = GetLanguageIdFromLocaleName(xivres::util::unicode::convert<std::wstring>(std::string_view(languageCode)));
		MessageBoxW(
			nullptr,
			std::wstring(GetStringResource(IDS_LANGUAGE_RESTARTREQUIRED, langId)).c_str(),
			std::wstring(GetStringResource(IDS_APP, langId)).c_str(),
			MB_OK);
	}
	return 0;
}

LRESULT App::FontEditorWindow::Menu_File_GameInstallationManager() {
	if (auto newConf = GameInstallationManagerDialog::Show(m_hWnd, g_config); newConf.has_value()) {
		g_config.GamePaths = std::move(newConf->GamePaths);
		g_config.Save();
		ElementFonts::FlushCachedFonts();
		this->m_multiFontSet.FlushCache();
		Window_Redraw();
	}

	return 0;
}

LRESULT App::FontEditorWindow::Menu_File_Exit() {
	if (Changes_ConfirmIfDirty())
		return 1;

	DestroyWindow(m_hWnd);
	return 0;
}
