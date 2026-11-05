#include "pch.h"
#include "GameFontReloader.h"
#include "MainWindow.h"
#include "xivres/textools.h"

// Whether the process runs the game, by the name of its executable; other processes are not opened with full access.
static bool IsGameProcess(DWORD pid) {
	const std::unique_ptr<void, decltype(&CloseHandle)> hProcess(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid), &CloseHandle);
	if (!hProcess)
		return false;

	std::wstring path(PATHCCH_MAX_CCH, L'\0');
	auto length = static_cast<DWORD>(path.size());
	if (!QueryFullProcessImageNameW(hProcess.get(), 0, path.data(), &length))
		return false;
	path.resize(length);
	return std::filesystem::path(path).filename() == L"ffxiv_dx11.exe";
}

LRESULT App::FontEditorWindow::Menu_HotReload_Reload(bool restore) {
	std::vector<DWORD> pids;
	pids.resize(4096);
	DWORD cb;
	EnumProcesses(pids.data(), static_cast<DWORD>(std::span(pids).size_bytes()), &cb);
	pids.resize(cb / sizeof DWORD);
	for (const auto pid : pids) {
		if (!IsGameProcess(pid))
			continue;

		try {
			GameFontReloader::GameProcess(pid).RefreshFonts(restore ? nullptr : &m_multiFontSet, m_hotReloadFontType);
		} catch (const std::exception& e) {
			// pass, don't really care
			OutputDebugStringA(std::format("Failed to reload fonts: {}\n", e.what()).c_str());
		}
	}
	return 0;
}

LRESULT App::FontEditorWindow::Menu_HotReload_Font(xivres::font_type mode) {
	m_hotReloadFontType = mode;
	return 0;
}
