#include "pch.h"
#include "FontChanger.Presets/DirectWriteUtil.h"

uint32_t FontChanger::DirectWriteUtil::StringToTag(std::string_view tag) {
	uint32_t res = 0;
	for (size_t i = 0; i < 4; i++)
		res |= static_cast<uint32_t>(static_cast<uint8_t>(i < tag.size() ? tag[i] : ' ')) << (8 * i);
	return res;
}

std::string FontChanger::DirectWriteUtil::TagToString(uint32_t tag) {
	std::string res(4, ' ');
	for (size_t i = 0; i < 4; i++)
		res[i] = static_cast<char>(tag >> (8 * i) & 0xFF);
	return res;
}

std::wstring FontChanger::DirectWriteUtil::GetLocalizedString(IDWriteLocalizedStrings* strings, std::initializer_list<const wchar_t*> locales) {
	if (!strings || !strings->GetCount())
		return {};

	UINT32 index = 0;
	for (const auto locale : locales) {
		if (BOOL exists; SUCCEEDED(strings->FindLocaleName(locale, &index, &exists)) && exists)
			break;
		index = 0;
	}

	UINT32 length;
	if (FAILED(strings->GetStringLength(index, &length)))
		return {};
	std::wstring res(length + 1, L'\0');
	if (FAILED(strings->GetString(index, res.data(), length + 1)))
		return {};
	res.resize(length);
	return res;
}

std::wstring FontChanger::DirectWriteUtil::GetEnglishFamilyName(IDWriteFontFamily* family) {
	IDWriteLocalizedStringsPtr names;
	SuccessOrThrow(family->GetFamilyNames(&names));
	return GetLocalizedString(names, {L"en-us", L"en"});
}

std::vector<DWRITE_FONT_AXIS_RANGE> FontChanger::DirectWriteUtil::GetFontAxisRanges(IDWriteFont* font) {
	IDWriteFontFacePtr face;
	IDWriteFontFace5Ptr face5;
	if (FAILED(font->CreateFontFace(&face)) || FAILED(face.QueryInterface(decltype(face5)::GetIID(), &face5)) || !face5->HasVariations())
		return {};

	IDWriteFontResourcePtr resource;
	SuccessOrThrow(face5->GetFontResource(&resource));
	std::vector<DWRITE_FONT_AXIS_RANGE> ranges(resource->GetFontAxisCount());
	SuccessOrThrow(resource->GetFontAxisRanges(ranges.data(), static_cast<UINT32>(ranges.size())));
	return ranges;
}
