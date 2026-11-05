#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "FontChanger.FixedSizeFont/directwrite_fixed_size_font.h"

namespace FontChanger::DirectWriteUtil {
	// In DWRITE_MAKE_OPENTYPE_TAG byte order, padded with spaces as OpenType pads tags shorter than 4 characters.
	[[nodiscard]] uint32_t StringToTag(std::string_view tag);

	[[nodiscard]] std::string TagToString(uint32_t tag);

	// Falls back to the first string when none of the locales is there.
	[[nodiscard]] std::wstring GetLocalizedString(IDWriteLocalizedStrings* strings, std::initializer_list<const wchar_t*> locales);

	// Lookups name families in English.
	[[nodiscard]] std::wstring GetEnglishFamilyName(IDWriteFontFamily* family);

	[[nodiscard]] std::vector<DWRITE_FONT_AXIS_RANGE> GetFontAxisRanges(IDWriteFont* font);
}
