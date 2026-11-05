#include "pch.h"
#include "FontChanger.Presets/HResult.h"

HRESULT FontChanger::SuccessOrThrow(HRESULT hr, std::initializer_list<HRESULT> acceptables) {
	if (SUCCEEDED(hr) || std::ranges::find(acceptables, hr) != acceptables.end())
		return hr;

	wchar_t* pszMsg = nullptr;
	FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, hr, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), reinterpret_cast<LPWSTR>(&pszMsg), 0, nullptr);
	if (!pszMsg)
		throw std::runtime_error(std::format("Error (HRESULT=0x{:08X})", static_cast<uint32_t>(hr)));

	const std::unique_ptr<wchar_t, decltype(&LocalFree)> pszMsgFree(pszMsg, &LocalFree);
	throw std::runtime_error(std::format("Error (HRESULT=0x{:08X}): {}", static_cast<uint32_t>(hr), xivres::util::unicode::convert<std::string>(std::wstring(pszMsg))));
}
