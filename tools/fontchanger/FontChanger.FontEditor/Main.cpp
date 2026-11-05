#include "pch.h"
#include "resource.h"

#include "CommandLineRender.h"
#include "ExportPreviewWindow.h"
#include "FontElementEditorDialog.h"
#include "FontChanger.Presets/ElementFonts.h"
#include "MainWindow.h"
#include "FontGeneratorConfig.h"

#pragma comment(lib, "Comctl32.lib")
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

HINSTANCE g_hInstance;
WORD g_langId;
std::wstring g_localeName;
FontGeneratorConfig g_config;

int __stdcall WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nShowCmd) {
	g_hInstance = hInstance;

	SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
		return -1;

	std::vector<std::wstring> args;
	if (int nArgs{}; LPWSTR* szArgList = CommandLineToArgvW(GetCommandLineW(), &nArgs)) {
		if (szArgList) {
			for (int i = 0; i < nArgs; i++)
				args.emplace_back(szArgList[i]);
			LocalFree(szArgList);
		}
	}

	if (wchar_t localeName[LOCALE_NAME_MAX_LENGTH];
		GetUserDefaultLocaleName(localeName, LOCALE_NAME_MAX_LENGTH)) {
		g_localeName = localeName;
	} else {
		g_localeName = L"en-us";
	}

	g_langId = GetLanguageIdFromLocaleName(g_localeName);

	if (const auto r = TryCatchShowError(nullptr, IDS_ERROR_OPENFILEFAILURE_BODY, 1, [&] {
		g_config = FontGeneratorConfig::Load();
		if (!g_config.LanguageCode.empty()) {
			g_localeName = xivres::util::unicode::convert<std::wstring>(g_config.LanguageCode);
			g_langId = GetLanguageIdFromLocaleName(g_localeName);
		}
		return 0;
	}))
		return r;

	App::ElementFonts::SetGameInstallationPathsProvider([](xivres::font_type fontType) {
		return g_config.GetGamePaths(fontType);
	});
	App::ElementFonts::SetGameFontErrorHandler([](const std::exception& e) {
		if (const auto systemError = dynamic_cast<const std::system_error*>(&e))
			ShowErrorMessageBox(nullptr, IDS_ERROR_GAMENOTFOUND_BODY, *systemError);
		else
			ShowErrorMessageBox(nullptr, IDS_ERROR_GAMENOTFOUND_BODY, e);
	});

	if (const auto exitCode = App::RunCommandLineRender(args))
		return *exitCode;

	App::FontEditorWindow window(std::move(args));
	for (MSG msg{}; GetMessageW(&msg, nullptr, 0, 0);) {
		if (App::BaseWindow::ConsumeMessage(msg))
			continue;

		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	return 0;
}
