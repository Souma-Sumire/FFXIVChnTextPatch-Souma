#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <filesystem>

#include "../include/xivres/util.module_relative.h"

std::string xivres::util::module_relative::to_string() const {
	HMODULE hModule{};
	if (!GetModuleHandleExW(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		static_cast<LPCWSTR>(Address),
		&hModule))
		return std::format("0x{:X}", reinterpret_cast<uintptr_t>(Address));

	std::wstring path(MAX_PATH, L'\0');
	while (true) {
		const auto len = GetModuleFileNameW(hModule, path.data(), static_cast<DWORD>(path.size()));
		if (!len)
			return std::format("0x{:X}", reinterpret_cast<uintptr_t>(Address));
		if (len < path.size()) {
			path.resize(len);
			break;
		}
		path.resize(path.size() * 2);
	}

	const auto base = reinterpret_cast<const char*>(hModule);
	const auto offset = static_cast<const char*>(Address) - base;
	const auto name = std::filesystem::path(path).filename().string();

	return std::format("{}+0x{:X}", name, offset);
}
