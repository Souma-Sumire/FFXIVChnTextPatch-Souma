#include "pch.h"
#include "GameFontReloader.h"

#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <vector>

#include "FontChanger.Presets/Structs.h"

template<typename T>
inline T NotNull(T v) {
	if (!v)
		throw std::runtime_error("Fail");
	return v;
}

extern "C" void asm_call_atkmodule_vf43_via_wndproc();

namespace {

// What the hot reload uses of the game, in FFXIVClientStructs' form: the same signatures as FontChanger.DalamudPlugin's
// (ClientStructs) where it has them, and FFXIVClientStructs' own for the rest.
namespace GameCode {
	// Framework: [StaticAddress] of Instance() (a pointer), and [MemberFunction] GetUIModule.
	constexpr std::string_view FrameworkInstanceSignature = "48 8B 1D ?? ?? ?? ?? 8B 7C 24";
	constexpr size_t FrameworkInstanceOffset = 3;
	constexpr std::string_view FrameworkGetUIModuleSignature = "E8 ?? ?? ?? ?? 80 7B 1D 01";

	// UIModule: [VirtualFunction(7)] GetRaptureAtkModule.
	constexpr size_t UIModuleGetRaptureAtkModuleVtableOffset = 7 * 8;

	// AtkModule: [MemberFunction] of vf43, void LoadFonts(bool lobby, bool forceReload); and whether the lobby's fonts are
	// loaded.
	constexpr std::string_view AtkModuleLoadFontsSignature = "48 89 5C 24 18 48 89 74 24 20 55 57 41 55 41 56 41 57 48 8D AC 24 B0 FB FF FF 48 81 EC 50 05 00 00";
	constexpr size_t AtkModuleIsLobby = 0x8190;

	// AtkStage: [StaticAddress] of Instance() (a pointer), and its RaptureAtkUnitManager*.
	constexpr std::string_view AtkStageInstanceSignature = "48 8B 05 ?? ?? ?? ?? 4C 8B 40 18 45 8B 40 18";
	constexpr size_t AtkStageInstanceOffset = 3;
	constexpr size_t AtkStageRaptureAtkUnitManager = 0x20;

	// AtkUnitManager: [MemberFunction] GetAddonByName.
	constexpr std::string_view AtkUnitManagerGetAddonByNameSignature = "E8 ?? ?? ?? ?? 48 8B F8 41 B0 01";

	// AddonNamePlate: its NamePlateObjects, and their NameText (AtkTextNode*).
	constexpr size_t AddonNamePlateNamePlateObjectArray = 0x480;
	constexpr size_t NamePlateObjectSize = 0x78;
	constexpr size_t NamePlateObjectNameText = 0x20;
	constexpr size_t NamePlateObjectCount = 50;

	// AtkTextNode: [MemberFunction] void ToggleFontCache(bool use), and the byte of its TextFlags with FontCache.
	constexpr std::string_view AtkTextNodeToggleFontCacheSignature = "48 85 C9 74 ?? 48 89 5C 24 08 57 48 83 EC 20 44 0F B6 81 71 01 00 00";
	constexpr size_t AtkTextNodeFontCacheFlags = 0x171;
	constexpr size_t UseFontCacheFlag = 0x40;

	// [StaticAddress] of the font tables: the lobby's two and the game's.
	constexpr std::string_view FontTablesSignature = "48 8D 1D ?? ?? ?? ?? 48 8D 05 ?? ?? ?? ?? 48 0F 45 D8 EB 07 48 8D 1D ?? ?? ?? ??";
	constexpr size_t LobbyTableAOffset = 3;
	constexpr size_t LobbyTableBOffset = 10;
	constexpr size_t GameTableOffset = 23;
	constexpr size_t FontTableEntrySize = 0x18;
}

// The .text section of the game's executable, to find signatures in FFXIVClientStructs' form (hex bytes, ?? for any)
// at their addresses in the running game.
class GameText {
	std::span<const uint8_t> m_text;
	uint64_t m_address{};

public:
	GameText(std::span<const uint8_t> exe, uint64_t realBase) {
		const auto& dosHeader = *reinterpret_cast<const IMAGE_DOS_HEADER*>(exe.data());
		const auto& ntHeader64 = *reinterpret_cast<const IMAGE_NT_HEADERS64*>(&exe[dosHeader.e_lfanew]);
		for (const auto& s : std::span(IMAGE_FIRST_SECTION(&ntHeader64), ntHeader64.FileHeader.NumberOfSections)) {
			if (memcmp(s.Name, ".text", 6) == 0) {
				m_text = exe.subspan(s.PointerToRawData, std::min<size_t>(s.SizeOfRawData, s.Misc.VirtualSize));
				m_address = realBase + s.VirtualAddress;
				return;
			}
		}
		throw std::runtime_error("The game's executable has no .text section.");
	}

	// Gets the address of the signature's match; a call's or jump's target if it starts with E8 or E9.
	uint64_t Scan(std::string_view signature) const {
		const auto offset = Find(signature);
		if (signature.starts_with("E8") || signature.starts_with("E9"))
			return m_address + offset + 5 + Rel32(offset + 1);
		return m_address + offset;
	}

	// Gets the target of the rel32 at an offset in the signature's match, as [StaticAddress] does.
	uint64_t GetStaticAddress(std::string_view signature, size_t offset) const {
		const auto rel32 = Find(signature) + offset;
		return m_address + rel32 + 4 + Rel32(rel32);
	}

private:
	int64_t Rel32(size_t offset) const {
		if (offset + 4 > m_text.size())
			throw std::runtime_error("A rel32 is past the game's code.");
		int32_t value;
		memcpy(&value, &m_text[offset], sizeof value);
		return value;
	}

	// Finds the signature's only match; throws if there is none, or more than one.
	size_t Find(std::string_view signature) const {
		std::vector<int> pattern;  // -1 for any byte
		for (size_t i = 0; i < signature.size(); i++) {
			if (signature[i] == ' ')
				continue;
			const auto token = std::string(signature.substr(i, 2));
			pattern.push_back(token == "??" ? -1 : std::stoi(token, nullptr, 16));
			i++;
		}

		if (pattern.empty() || pattern[0] < 0 || pattern.size() > m_text.size())
			throw std::invalid_argument(std::format("The signature {} can't be looked for.", signature));

		const auto matches = [&](size_t at) {
			for (size_t j = 1; j < pattern.size(); j++) {
				if (pattern[j] >= 0 && m_text[at + j] != pattern[j])
					return false;
			}
			return true;
		};

		std::optional<size_t> found;
		const auto end = m_text.size() - pattern.size() + 1;
		for (size_t i = 0; i < end; i++) {
			const auto next = static_cast<const uint8_t*>(memchr(&m_text[i], pattern[0], end - i));
			if (!next)
				break;
			i = static_cast<size_t>(next - m_text.data());
			if (!matches(i))
				continue;
			if (found)
				throw std::runtime_error(std::format("The signature {} is in the game's code more than once.", signature));
			found = i;
		}
		if (!found)
			throw std::runtime_error(std::format("The signature {} isn't in this version of the game.", signature));
		return *found;
	}
};

}

static std::vector<std::span<uint8_t>> Segmentize(void* pfn) {
	static constexpr std::array<uint8_t, 8> marker{{0x90, 0xcc, 0x90, 0xcc, 0x90, 0xcc, 0x90, 0xcc}};

	auto ptr = reinterpret_cast<uint8_t*>(pfn);
	if (*ptr == 0xE9)
		ptr += 5 + *reinterpret_cast<int*>(ptr + 1);

	std::vector<std::span<uint8_t>> result;

	for (size_t prev = 0, i = 0;; prev = (i += 8)) {
		while (*reinterpret_cast<uint64_t*>(&ptr[i]) != 0xcc90cc90cc90cc90ULL)
			i++;
		if (prev == i)
			break;
		result.emplace_back(&ptr[prev], &ptr[i]);
	}
	return result;
}

static std::filesystem::path GetModulePath(HANDLE hProcess, HMODULE hModule) {
	std::wstring pathStr(PATHCCH_MAX_CCH + 1, L'\0');
	pathStr.resize(GetModuleFileNameExW(hProcess, hModule, pathStr.data(), static_cast<DWORD>(pathStr.size())));
	return pathStr;
}

static HMODULE GetFirstModule(HANDLE hProcess) {
	std::vector<HMODULE> modules;
	DWORD needed = 0;
	do {
		modules.resize(modules.size() + 128);
		EnumProcessModules(hProcess, modules.data(), static_cast<DWORD>(std::span(modules).size_bytes()), &needed);
	} while (std::span(modules).size_bytes() < needed);
	modules.resize(needed / sizeof HMODULE);
	if (modules.empty())
		throw std::runtime_error("Process not accessible");
	return modules[0];
}

GameFontReloader::GameProcess::GameProcess(DWORD pid)
	: m_hProcess(NotNull(OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid)), &CloseHandle)
	, m_hModule(GetFirstModule(m_hProcess.get()))
	, m_gameExePath(GetModulePath(m_hProcess.get(), m_hModule)) {
	if (m_gameExePath.filename() != L"ffxiv_dx11.exe")
		throw std::runtime_error("Not a ffxiv executable");

	// The code is read from the executable rather than from the game, so hooks that others placed don't hide it.
	std::vector<uint8_t> exe(std::filesystem::file_size(m_gameExePath));
	if (std::ifstream file(m_gameExePath, std::ios::binary); !file.read(reinterpret_cast<char*>(exe.data()), static_cast<std::streamsize>(exe.size())))
		throw std::runtime_error("Failed to read the game's executable.");

	const auto realBase = reinterpret_cast<uint64_t>(m_hModule);
	const GameText text(exe, realBase);
	m_game.ppFramework = reinterpret_cast<void*>(text.GetStaticAddress(GameCode::FrameworkInstanceSignature, GameCode::FrameworkInstanceOffset));
	m_game.pfnFrameworkGetUiModule = reinterpret_cast<void*>(text.Scan(GameCode::FrameworkGetUIModuleSignature));
	m_game.UIModule_GetRaptureAtkModule_VtableOffset = GameCode::UIModuleGetRaptureAtkModuleVtableOffset;
	m_game.pfnAtkModuleLoadFonts = reinterpret_cast<void*>(text.Scan(GameCode::AtkModuleLoadFontsSignature));
	m_game.AtkModule_IsLobby = GameCode::AtkModuleIsLobby;
	m_game.ppAtkStage = reinterpret_cast<void*>(text.GetStaticAddress(GameCode::AtkStageInstanceSignature, GameCode::AtkStageInstanceOffset));
	m_game.AtkStage_RaptureAtkUnitManager = GameCode::AtkStageRaptureAtkUnitManager;
	m_game.pfnAtkUnitManagerGetAddonByName = reinterpret_cast<void*>(text.Scan(GameCode::AtkUnitManagerGetAddonByNameSignature));
	m_game.AddonNamePlate_NamePlateObjects = GameCode::AddonNamePlateNamePlateObjectArray;
	m_game.NamePlateObject_Size = GameCode::NamePlateObjectSize;
	m_game.NamePlateObject_NameText = GameCode::NamePlateObjectNameText;
	m_game.NamePlateObjectCount = GameCode::NamePlateObjectCount;
	m_game.pfnAtkTextNodeToggleFontCache = reinterpret_cast<void*>(text.Scan(GameCode::AtkTextNodeToggleFontCacheSignature));
	m_game.AtkTextNode_FontCacheFlags = GameCode::AtkTextNodeFontCacheFlags;
	m_game.UseFontCacheFlag = GameCode::UseFontCacheFlag;
	const std::vector<uint64_t> tableAddresses{
		text.GetStaticAddress(GameCode::FontTablesSignature, GameCode::GameTableOffset),
		text.GetStaticAddress(GameCode::FontTablesSignature, GameCode::LobbyTableAOffset),
		text.GetStaticAddress(GameCode::FontTablesSignature, GameCode::LobbyTableBOffset),
	};
	static_assert(sizeof(FontSetInGame::Font) == GameCode::FontTableEntrySize);

	// The tables as the executable has them; their pointers are of the executable's preferred base.
	const auto& dosHeader = *reinterpret_cast<const IMAGE_DOS_HEADER*>(exe.data());
	const auto& ntHeader64 = *reinterpret_cast<const IMAGE_NT_HEADERS64*>(&exe[dosHeader.e_lfanew]);
	const auto sectionHeaders = std::span(IMAGE_FIRST_SECTION(&ntHeader64), ntHeader64.FileHeader.NumberOfSections);
	const auto imageBase = ntHeader64.OptionalHeader.ImageBase;
	const auto fileOffset = [&](uint64_t rva) -> size_t {
		for (const auto& s : sectionHeaders) {
			if (s.VirtualAddress <= rva && rva < s.VirtualAddress + s.Misc.VirtualSize)
				return rva - s.VirtualAddress + s.PointerToRawData;
		}
		throw std::runtime_error("rva");
	};
	for (const auto address : tableAddresses) {
		if (std::ranges::any_of(m_tables, [&](const auto& t) { return t.Address == reinterpret_cast<void*>(address); }))
			continue;

		auto& table = m_tables.emplace_back(reinterpret_cast<void*>(address));
		memcpy(&table.Original, &exe[fileOffset(address - realBase)], sizeof table.Original);
		for (size_t i = 0; i < FontCount; i++) {
			auto& source = table.Original.Fonts[i];
			auto& target = table.Fonts.Fonts[i];
			target.TexCount = source.TexCount;
			target.TexPattern = reinterpret_cast<const char*>(&exe[fileOffset(reinterpret_cast<uint64_t>(source.TexPattern) - imageBase)]);
			target.Fdt = reinterpret_cast<const char*>(&exe[fileOffset(reinterpret_cast<uint64_t>(source.Fdt) - imageBase)]);
			source.TexPattern += realBase - imageBase;
			source.Fdt += realBase - imageBase;
		}
	}

	for (const auto type : {xivres::font_type::font, xivres::font_type::chn_axis, xivres::font_type::krn_axis}) {
		const auto defaults = FontChanger::FixedSizeFont::get_font_table(type);
		if (defaults.size() == FontCount && std::ranges::equal(m_tables.front().Fonts.Fonts, defaults, [](const auto& f, const auto& d) { return f.Fdt == d.Font->Name + ".fdt"; }))
			m_fontType = type;
	}
}

void GameFontReloader::GameProcess::RefreshFonts(const FontChanger::Structs::MultiFontSet* pProject, xivres::font_type fontType) const {
	if (pProject && fontType != xivres::font_type::undefined && fontType != xivres::font_type::font && fontType != m_fontType)
		throw std::runtime_error("The game has no fonts of the font type.");

	// What each table will be: as the executable has it without a project; with the fonts of the project loaded from the
	// project's textures; or the game's table of a font type, the lobby's staying.
	std::vector<std::optional<FontSet>> targets(m_tables.size());
	if (pProject) {
		for (size_t i = 0; i < m_tables.size(); i++) {
			if (fontType != xivres::font_type::undefined) {
				if (i == 0)
					targets[i] = GetDefaultFontSet(fontType);
				continue;
			}

			auto& target = targets[i].emplace(m_tables[i].Fonts);
			for (const auto& f : pProject->FontSets) {
				std::string texNameFormat = f->TexFilenameFormat;
				for (size_t pos; (pos = texNameFormat.find("{}")) != std::string::npos;)
					texNameFormat.replace(pos, 2, "%d");

				for (const auto& font : f->Fonts) {
					for (auto& slot : target.Fonts) {
						if (slot.Fdt == font->Name + ".fdt" && slot.TexPattern != texNameFormat) {
							slot.TexPattern = texNameFormat;
							slot.TexCount = f->ExpectedTexCount;
						}
					}
				}
			}
		}
	}

	HWND hGameWindow = nullptr;
	{
		struct EnumWindowsStruct {
			const GameProcess& Process;
			HWND Result;
		} wnd{*this, nullptr};
		EnumWindows([](HWND hWnd, LPARAM lParam) -> BOOL {
			auto& self = *reinterpret_cast<EnumWindowsStruct*>(lParam);
			DWORD pid;
			GetWindowThreadProcessId(hWnd, &pid);
			if (pid != GetProcessId(self.Process.m_hProcess.get()))
				return true;

			wchar_t name[256];
			GetClassNameW(hWnd, name, 256);
			if (wcscmp(name, L"FFXIVGAME") != 0)
				return true;

			self.Result = hWnd;

			return false;
		}, reinterpret_cast<LPARAM>(&wnd));
		if (wnd.Result)
			hGameWindow = wnd.Result;
		else
			throw std::runtime_error("Game window not found.");
	}

	auto segments = Segmentize(&asm_call_atkmodule_vf43_via_wndproc);
	std::vector<uint8_t> code(1 + &segments.back().back() - &segments.front().front());
	memcpy(code.data(), &segments.front().front(), code.size());

	// The tables to write, the original if unchanged; the strings of a changed one go after the code, at offsets from
	// it until the code is placed.
	std::vector<FontSetInGame> written(m_tables.size());
	for (size_t t = 0; t < m_tables.size(); t++) {
		written[t] = m_tables[t].Original;
		if (!targets[t])
			continue;
		for (size_t i = 0; i < FontCount; i++) {
			const auto& local = targets[t]->Fonts[i];
			auto& remote = written[t].Fonts[i];
			remote.TexCount = local.TexCount;

			remote.TexPattern = reinterpret_cast<char*>(code.size());
			code.insert(code.end(),
				reinterpret_cast<const uint8_t*>(local.TexPattern.data()),
				reinterpret_cast<const uint8_t*>(local.TexPattern.data()) + local.TexPattern.size());
			code.push_back(0);

			remote.Fdt = reinterpret_cast<char*>(code.size());
			code.insert(code.end(),
				reinterpret_cast<const uint8_t*>(local.Fdt.data()),
				reinterpret_cast<const uint8_t*>(local.Fdt.data()) + local.Fdt.size());
			code.push_back(0);
		}
	}

	const auto offsetMyWndProc = &segments[1].front() - &segments[0].front();
	const auto offsetRedirectWndProc = &segments[2].front() - &segments[0].front();
	const std::unique_ptr<void, decltype(&CloseHandle)> hEvent1(NotNull(CreateEventW(nullptr, TRUE, FALSE, nullptr)), &CloseHandle);
	const std::unique_ptr<void, decltype(&CloseHandle)> hEvent2(NotNull(CreateEventW(nullptr, TRUE, FALSE, nullptr)), &CloseHandle);
	const auto pRemote = reinterpret_cast<char*>(NotNull(VirtualAllocEx(m_hProcess.get(), nullptr, code.capacity(), MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE)));
	std::shared_ptr<void> remotePtrFreer;

	HANDLE hEvent1Target;
	if (!DuplicateHandle(GetCurrentProcess(), hEvent1.get(), m_hProcess.get(), &hEvent1Target, 0, FALSE, DUPLICATE_SAME_ACCESS))
		throw std::runtime_error("DuplicateHandle fail");

	HANDLE hEvent2Target;
	if (!DuplicateHandle(GetCurrentProcess(), hEvent2.get(), m_hProcess.get(), &hEvent2Target, 0, FALSE, DUPLICATE_SAME_ACCESS))
		throw std::runtime_error("DuplicateHandle fail");

	const auto pData = reinterpret_cast<void**>(code.data());
	pData[0] = NotNull(GetProcAddress(NotNull(GetModuleHandleW(L"user32.dll")), "CallWindowProcW"));
	pData[1] = NotNull(GetProcAddress(NotNull(GetModuleHandleW(L"user32.dll")), "SetWindowLongPtrW"));
	pData[2] = NotNull(GetProcAddress(NotNull(GetModuleHandleW(L"user32.dll")), "GetWindowLongPtrW"));
	pData[3] = NotNull(GetProcAddress(NotNull(GetModuleHandleW(L"kernel32.dll")), "SetEvent"));
	pData[4] = NotNull(GetProcAddress(NotNull(GetModuleHandleW(L"kernel32.dll")), "ResetEvent"));
	pData[5] = NotNull(GetProcAddress(NotNull(GetModuleHandleW(L"kernel32.dll")), "WaitForSingleObject"));
	pData[6] = m_game.ppFramework;
	pData[7] = m_game.pfnFrameworkGetUiModule;
	pData[8] = hGameWindow;
	pData[9] = hEvent1Target;
	pData[10] = hEvent2Target;
	// pData[11] is the previous window procedure, set by the injected code.
	pData[12] = m_game.pfnAtkUnitManagerGetAddonByName;
	pData[13] = m_game.pfnAtkTextNodeToggleFontCache;
	pData[14] = m_game.ppAtkStage;
	pData[15] = reinterpret_cast<void*>(m_game.AtkModule_IsLobby);
	pData[16] = reinterpret_cast<void*>(m_game.AtkStage_RaptureAtkUnitManager);
	pData[17] = reinterpret_cast<void*>(m_game.AddonNamePlate_NamePlateObjects);
	pData[18] = reinterpret_cast<void*>(m_game.NamePlateObject_Size);
	pData[19] = reinterpret_cast<void*>(m_game.NamePlateObject_NameText);
	pData[20] = reinterpret_cast<void*>(m_game.AtkTextNode_FontCacheFlags);
	pData[21] = reinterpret_cast<void*>(m_game.NamePlateObjectCount);
	pData[22] = reinterpret_cast<void*>(m_game.UIModule_GetRaptureAtkModule_VtableOffset);
	pData[23] = m_game.pfnAtkModuleLoadFonts;
	pData[24] = reinterpret_cast<void*>(m_game.UseFontCacheFlag);

	for (size_t t = 0; t < m_tables.size(); t++) {
		if (!targets[t])
			continue;
		for (auto& remote : written[t].Fonts) {
			remote.Fdt = pRemote + reinterpret_cast<size_t>(remote.Fdt);
			remote.TexPattern = pRemote + reinterpret_cast<size_t>(remote.TexPattern);
		}
	}

	// The memory can be freed afterwards unless a table points to strings in it.
	if (std::ranges::none_of(targets, [](const auto& t) { return t.has_value(); }))
		remotePtrFreer = {pRemote, [hProcess = m_hProcess.get()](void* p) { VirtualFreeEx(hProcess, p, 0, MEM_RELEASE); }};

	WriteProcessMemory(m_hProcess.get(), pRemote, code.data(), code.size(), nullptr);

	const auto hRemoteThread = NotNull(CreateRemoteThread(m_hProcess.get(), nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(pRemote + offsetRedirectWndProc), nullptr, 0, nullptr));
	WaitForSingleObject(hRemoteThread, INFINITE);
	PostMessage(hGameWindow, WM_NULL, 0, 0);

	WaitForSingleObject(hEvent1.get(), INFINITE);
	for (size_t t = 0; t < m_tables.size(); t++)
		WriteProcessMemory(m_hProcess.get(), m_tables[t].Address, &written[t], sizeof written[t], nullptr);
	ResetEvent(hEvent1.get());
	SetEvent(hEvent2.get());

	SendMessage(hGameWindow, WM_NULL, 0, 0);

	DuplicateHandle(m_hProcess.get(), hEvent1Target, nullptr, nullptr, 0, FALSE, DUPLICATE_CLOSE_SOURCE);
	DuplicateHandle(m_hProcess.get(), hEvent2Target, nullptr, nullptr, 0, FALSE, DUPLICATE_CLOSE_SOURCE);
}

GameFontReloader::FontSet GameFontReloader::GetDefaultFontSet(xivres::font_type type) {
	const auto table = FontChanger::FixedSizeFont::get_font_table(type);
	if (table.size() != FontCount)
		throw std::out_of_range("font/lobby/chn/krn are supported");

	FontSet res;
	for (size_t i = 0; i < FontCount; i++) {
		std::string_view format(FontChanger::FixedSizeFont::get_font_tex_filename_format(table[i].Font->FontType));
		auto texPattern = std::string(format.substr(format.rfind('/') + 1));
		texPattern.replace(texPattern.find("{}"), 2, "%d");
		res.Fonts[i] = {table[i].TextureCount, std::move(texPattern), table[i].Font->Name + ".fdt"};
	}
	return res;
}
