#include "pch.h"
#include "utils.h"

void do_stuff();

HMODULE g_hModule;

HRESULT WINAPI FORWARDER_DirectInput8Create(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter) {
	static const auto s_hDInput8 = LoadLibraryW(LR"(C:\Windows\System32\dinput8.dll)");
	static const auto s_pfnDirectInput8Create = reinterpret_cast<decltype(DirectInput8Create)*>(GetProcAddress(s_hDInput8, "DirectInput8Create"));
	do_stuff();
	return s_pfnDirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
}

static void antidebug() {
	if (void* pfn; utils::loaded_module::current_process().find_imported_function_pointer("kernel32.dll", "IsDebuggerPresent", 0, pfn)) {
		utils::memory_tenderizer m(pfn, sizeof(void*), PAGE_READWRITE);
		*reinterpret_cast<void**>(pfn) = static_cast<decltype(&IsDebuggerPresent)>([]() { return FALSE; });
	}

	if (void* pfn; utils::loaded_module::current_process().find_imported_function_pointer("kernel32.dll", "OpenProcess", 0, pfn)) {
		utils::memory_tenderizer m(pfn, sizeof(void*), PAGE_READWRITE);
		*reinterpret_cast<void**>(pfn) = static_cast<decltype(&OpenProcess)>([](DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwProcessId) {
			if (dwProcessId == GetCurrentProcessId()) {
				if (dwDesiredAccess & PROCESS_VM_WRITE) {
					SetLastError(ERROR_ACCESS_DENIED);
					return HANDLE();
				}
			}

			return ::OpenProcess(dwDesiredAccess, bInheritHandle, dwProcessId);
		});
	}
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
	switch (ul_reason_for_call) {
		case DLL_PROCESS_ATTACH:
			g_hModule = hModule;
			DisableThreadLibraryCalls(hModule);
			MH_Initialize();
			antidebug();
			return TRUE;
		
		case DLL_PROCESS_DETACH:
			TerminateProcess(GetCurrentProcess(), 0);
			return TRUE;
	}

	return TRUE;
}
