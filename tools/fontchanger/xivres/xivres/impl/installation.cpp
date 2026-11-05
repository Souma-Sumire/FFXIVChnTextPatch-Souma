#include "../include/xivres/installation.h"
#include "../include/xivres/util.thread_pool.h"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <ranges>

xivres::installation::installation(std::filesystem::path gamePath)
	: m_gamePath(std::move(gamePath)) {
	for (const auto& iter : std::filesystem::recursive_directory_iterator(m_gamePath / "sqpack")) {
		if (iter.is_directory() || !iter.path().wstring().ends_with(L".win32.index2"))
			continue;

		auto packFileName = std::filesystem::path{ iter.path().filename() }.replace_extension("").replace_extension("").string();
		if (packFileName.size() < 6)
			continue;

		packFileName.resize(6);

		const auto packFileId = std::strtol(packFileName.data(), nullptr, 16);
		m_readers.emplace(packFileId, std::optional<sqpack::reader>());
		static_cast<void>(m_populateMtx[packFileId]);
	}
}

std::shared_ptr<xivres::packed_stream> xivres::installation::get_file_packed(const path_spec& pathSpec) const {
	return get_sqpack(pathSpec).packed_at(pathSpec);
}

std::shared_ptr<xivres::unpacked_stream> xivres::installation::get_file(const path_spec& pathSpec, std::span<uint8_t> obfuscatedHeaderRewrite) const {
	return std::make_shared<unpacked_stream>(get_sqpack(pathSpec).packed_at(pathSpec), obfuscatedHeaderRewrite);
}

std::vector<uint32_t> xivres::installation::get_sqpack_ids() const {
	std::vector<uint32_t> res;
	res.reserve(m_readers.size());
	for (const auto& key : m_readers | std::views::keys)
		res.emplace_back(key);
	return res;
}

const xivres::sqpack::reader& xivres::installation::get_sqpack(uint8_t categoryId, uint8_t expacId, uint8_t partId) const {
	return get_sqpack((categoryId << 16) | (expacId << 8) | partId);
}

const xivres::sqpack::reader& xivres::installation::get_sqpack(const path_spec& pathSpec) const {
	return get_sqpack(pathSpec.packid());
}

const xivres::sqpack::reader& xivres::installation::get_sqpack(uint32_t packId) const {
	auto& item = m_readers.at(packId);
	if (item)
		return *item;

	const auto lock = std::scoped_lock(m_populateMtx.at(packId));
	if (item)
		return *item;

	const auto expacId = (packId >> 8) & 0xFF;
	if (expacId == 0)
		return item.emplace(sqpack::reader::from_path(m_gamePath / std::format("sqpack/ffxiv/{:0>6x}.win32.index2", packId)));
	else
		return item.emplace(sqpack::reader::from_path(m_gamePath / std::format("sqpack/ex{}/{:0>6x}.win32.index2", expacId, packId)));
}

void xivres::installation::preload_all_sqpacks() const {
	util::thread_pool::task_waiter waiter;
	for (const auto& key : m_readers | std::views::keys)
		waiter.submit([this, key](auto&) { (void)get_sqpack(key); });
	waiter.wait_all();
}

#ifdef _WIN32

namespace {
	std::wstring read_registry_as_wstring(const wchar_t* lpSubKey, const wchar_t* lpValueName, int mode = 0) {
		if (mode == 0) {
			auto res1 = read_registry_as_wstring(lpSubKey, lpValueName, KEY_WOW64_32KEY);
			if (res1.empty())
				res1 = read_registry_as_wstring(lpSubKey, lpValueName, KEY_WOW64_64KEY);
			return res1;
		}
		HKEY hKey;
		if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
			lpSubKey,
			0, KEY_READ | mode, &hKey))
				return {};

		const auto hKeyFreer = std::unique_ptr<std::remove_pointer_t<HKEY>, decltype(&RegCloseKey)>(hKey, &RegCloseKey);

		DWORD buflen = 0;
		if (RegQueryValueExW(hKey, lpValueName, nullptr, nullptr, nullptr, &buflen))
			return {};

		std::wstring buf;
		buf.resize(buflen + 1);
		if (RegQueryValueExW(hKey, lpValueName, nullptr, nullptr, reinterpret_cast<LPBYTE>(buf.data()), &buflen))
			return {};

		buf.erase(std::ranges::find(buf, L'\0'), buf.end());

		return buf;
	}
}

std::filesystem::path xivres::installation::find_installation_global() {
	if (const auto reg = read_registry_as_wstring(LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{2B41E132-07DF-4925-A3D3-F2D1765CCDFE})", L"DisplayIcon"); !reg.empty()) {
		// C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\boot\ffxivboot.exe
		auto path = std::filesystem::path(reg).parent_path().parent_path() / "game";
		if (exists(path))
			return path;
	}

	for (const auto steamAppId : {
			39210,  // paid
			312060,  // free trial
		}) {
		if (const auto reg = read_registry_as_wstring(std::format(LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App {})", steamAppId).c_str(), L"InstallLocation"); !reg.empty()) {
			auto path = std::filesystem::path(reg) / "game";
			if (exists(path))
				return path;
		}
	}
	
	return {};
}

std::filesystem::path xivres::installation::find_installation_china() {
	if (const auto reg = read_registry_as_wstring(LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\FFXIV)", L"DisplayIcon"); !reg.empty()) {
		// C:\Program Files (x86)\SNDA\FFXIV\uninst.exe
		auto path = std::filesystem::path(reg).parent_path() / "game";
		if (exists(path))
			return path;
	}
	
	return {};
}

std::filesystem::path xivres::installation::find_installation_korea() {
	if (const auto reg = read_registry_as_wstring(LR"(SOFTWARE\Classes\ff14kr\shell\open\command)", L""); !reg.empty()) {
		// "C:\Program Files (x86)\FINAL FANTASY XIV - KOREA\boot\FFXIV_Boot.exe" "%1"
		if (int nArgs; const auto ppszArgs = CommandLineToArgvW(reg.c_str(), &nArgs)) {
			const auto freer = std::unique_ptr<std::remove_pointer_t<decltype(ppszArgs)>, decltype(&LocalFree)>(ppszArgs, &LocalFree);
			auto path = std::filesystem::path(ppszArgs[0]).parent_path().parent_path() / "game";
			if (exists(path))
				return path;
		}
	}
	
	return {};
}

#else

std::filesystem::path xivres::installation::find_installation_global() { return {}; }
std::filesystem::path xivres::installation::find_installation_china() { return {}; }
std::filesystem::path xivres::installation::find_installation_korea() { return {}; }

#endif
