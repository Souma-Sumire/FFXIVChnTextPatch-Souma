#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <iostream>
#include <ranges>
#include <string>
#include <string_view>

#include <Windows.h>
#include <windowsx.h>

#include <comdef.h>
#include <CommCtrl.h>
#include <dwrite.h>
#include <PathCch.h>
#include <Psapi.h>
#include <shellapi.h>
#include <ShellScalingApi.h>
#include <ShObjIdl.h>
#include <ShlGuid.h>
#include <wincrypt.h>

#include <nlohmann/json.hpp>

#include "FontChanger.FixedSizeFont/directwrite_fixed_size_font.h"
#include "FontChanger.FixedSizeFont/fontdata_fixed_size_font.h"
#include "FontChanger.FixedSizeFont/fontdata_packer.h"
#include "FontChanger.FixedSizeFont/freetype_fixed_size_font.h"
#include "FontChanger.FixedSizeFont/merged_fixed_size_font.h"
#include "FontChanger.FixedSizeFont/text_measurer.h"
#include "FontChanger.FixedSizeFont/glyph_merging_fixed_size_font.h"
#include "FontChanger.FixedSizeFont/image_fixed_size_font.h"
#include "FontChanger.FixedSizeFont/wrapping_fixed_size_font.h"
#include "xivres/fontdata.h"
#include "xivres/installation.h"
#include "xivres/packed_stream.standard.h"
#include "xivres/packed_stream.texture.h"
#include "xivres/texture.h"
#include "xivres/texture.mipmap_stream.h"
#include "xivres/texture.preview.h"
#include "xivres/unpacked_stream.h"
#include "xivres/util.on_dtor.h"
#include "xivres/util.pixel_formats.h"

#include "FontChanger.Presets/HResult.h"

// The preset structures and the fonts made from them, from FontChanger.Presets.Native.
namespace FontChanger::Structs {}
namespace FontChanger::ElementFonts {}
namespace FontChanger::GlyphFiles {}

namespace App {
	namespace Structs = FontChanger::Structs;
	namespace ElementFonts = FontChanger::ElementFonts;
	namespace GlyphFiles = FontChanger::GlyphFiles;
}

using FontChanger::SuccessOrThrow;

#include "MiscUtil.h"

extern HINSTANCE g_hInstance;
extern struct FontGeneratorConfig g_config;
extern WORD g_langId;
extern std::wstring g_localeName;

_COM_SMARTPTR_TYPEDEF(IFileDialog, __uuidof(IFileDialog));
_COM_SMARTPTR_TYPEDEF(IFileSaveDialog, __uuidof(IFileSaveDialog));
_COM_SMARTPTR_TYPEDEF(IFileOpenDialog, __uuidof(IFileOpenDialog));
_COM_SMARTPTR_TYPEDEF(IShellItem, __uuidof(IShellItem));
_COM_SMARTPTR_TYPEDEF(IShellItemArray, __uuidof(IShellItemArray));
_COM_SMARTPTR_TYPEDEF(IDWriteFont, __uuidof(IDWriteFont));
_COM_SMARTPTR_TYPEDEF(IDWriteFactory, __uuidof(IDWriteFactory));
_COM_SMARTPTR_TYPEDEF(IDWriteGdiInterop, __uuidof(IDWriteGdiInterop));

inline std::wstring GetWindowString(HWND hwnd, bool trim = false) {
	std::wstring buf(GetWindowTextLengthW(hwnd) + static_cast<size_t>(1), L'\0');
	buf.resize(GetWindowTextW(hwnd, buf.data(), static_cast<int>(buf.size())));
	if (trim) {
		const auto l = buf.find_first_not_of(L"\t\r\n ");
		if (l == std::wstring::npos)
			return {};

		const auto r = buf.find_last_not_of(L"\t\r\n ");
		return buf.substr(l, r - l + 1);
	}
	return buf;
}

template<typename T>
inline T GetWindowNumber(HWND hwnd) {
	return static_cast<T>(std::wcstod(GetWindowString(hwnd, true).c_str(), nullptr));
}

template<typename T>
inline void SetWindowNumber(HWND hwnd, T v) {
	if constexpr (std::is_floating_point_v<T>)
		SetWindowTextW(hwnd, std::format(L"{:g}", v).c_str());
	else if constexpr (std::is_integral_v<T>)
		SetWindowTextW(hwnd, std::format(L"{}", v).c_str());
	else
		static_assert(!sizeof(T), "no match");
}

// Fills the combobox with the values and the names from their string resources, and selects the current value.
template<typename T, typename TItems = std::initializer_list<std::pair<T, UINT>>, typename = std::enable_if_t<(sizeof(T) <= sizeof(LPARAM))>>
inline void SetComboboxContent(HWND hCombo, T currentValue, const TItems& args) {
	ComboBox_ResetContent(hCombo);
	std::wstring text;
	for (const auto& [val, resId] : args) {
		text = GetStringResource(resId);
		const auto index = ComboBox_AddString(hCombo, text.c_str());
		ComboBox_SetItemData(hCombo, index, val);
		if (currentValue == val)
			ComboBox_SetCurSel(hCombo, index);
	}
}

template<typename T, typename = std::enable_if_t<(sizeof(T) <= sizeof(LPARAM))>>
inline T GetComboboxSelData(HWND hCombo) {
	return static_cast<T>(ComboBox_GetItemData(hCombo, ComboBox_GetCurSel(hCombo)));
}
