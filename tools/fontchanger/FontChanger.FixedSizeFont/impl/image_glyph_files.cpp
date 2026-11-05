#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "../include/FontChanger.FixedSizeFont/image_glyph_files.h"

using namespace xivres;

#include <algorithm>
#include <format>
#include <fstream>
#include <stdexcept>
#include <tuple>

#include <comdef.h>
#include <wincodec.h>

#include "xivres/util.on_dtor.h"

#pragma comment(lib, "windowscodecs.lib")

_COM_SMARTPTR_TYPEDEF(IWICImagingFactory, __uuidof(IWICImagingFactory));
_COM_SMARTPTR_TYPEDEF(IWICStream, __uuidof(IWICStream));
_COM_SMARTPTR_TYPEDEF(IWICBitmapDecoder, __uuidof(IWICBitmapDecoder));
_COM_SMARTPTR_TYPEDEF(IWICBitmapFrameDecode, __uuidof(IWICBitmapFrameDecode));
_COM_SMARTPTR_TYPEDEF(IWICFormatConverter, __uuidof(IWICFormatConverter));

namespace {
	void success_or_throw(HRESULT hr, const char* what) {
		if (FAILED(hr))
			throw std::runtime_error(std::format("{} failed (HRESULT=0x{:08X})", what, static_cast<uint32_t>(hr)));
	}

	std::string read_file(const std::filesystem::path& path) {
		std::ifstream in(path, std::ios::binary);
		if (!in)
			throw std::runtime_error("Failed to open the file.");
		return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
	}

	std::optional<float> json_number(const nlohmann::json& json, const char* key) {
		if (!json.is_object())
			return std::nullopt;
		if (const auto it = json.find(key); it != json.end() && it->is_number())
			return it->get<float>();
		return std::nullopt;
	}

	std::wstring to_lower(std::wstring s) {
		CharLowerBuffW(s.data(), static_cast<DWORD>(s.size()));
		return s;
	}
}

std::wstring FontChanger::FixedSizeFont::get_glyph_file_stem(char32_t codepoint) {
	if (codepoint < 0x10000)
		return std::format(L"uni{:04X}", static_cast<uint32_t>(codepoint));
	return std::format(L"u{:05X}", static_cast<uint32_t>(codepoint));
}

std::optional<char32_t> FontChanger::FixedSizeFont::parse_glyph_file_name(const std::filesystem::path& path) {
	auto stem = to_lower(path.stem().wstring());
	if (const auto suffix = stem.find(L'_'); suffix != std::wstring::npos)
		stem.resize(suffix);

	std::wstring_view hex(stem);
	if (hex.starts_with(L"uni"))
		hex.remove_prefix(3);
	else if (hex.starts_with(L"u+"))
		hex.remove_prefix(2);
	else if (hex.starts_with(L"u"))
		hex.remove_prefix(1);
	else
		return std::nullopt;

	if (hex.size() < 4 || hex.size() > 6 || !std::ranges::all_of(hex, [](wchar_t c) { return iswxdigit(c) != 0; }))
		return std::nullopt;

	const auto value = std::wcstoul(std::wstring(hex).c_str(), nullptr, 16);
	if (value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF))
		return std::nullopt;
	return static_cast<char32_t>(value);
}

FontChanger::FixedSizeFont::image_glyph_source FontChanger::FixedSizeFont::decode_png_glyph(std::string_view png) {
	// Fonts may be made on threads that have not started COM.
	const auto hrInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	const auto uninit = util::on_dtor([hrInit] {
		if (SUCCEEDED(hrInit))
			CoUninitialize();
	});

	image_glyph_source source;
	std::vector<uint32_t> pixels;
	{
		IWICImagingFactoryPtr factory;
		success_or_throw(factory.CreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER), "CoCreateInstance(WICImagingFactory)");

		IWICStreamPtr stream;
		success_or_throw(factory->CreateStream(&stream), "IWICImagingFactory::CreateStream");
		success_or_throw(stream->InitializeFromMemory(reinterpret_cast<BYTE*>(const_cast<char*>(png.data())), static_cast<DWORD>(png.size())), "IWICStream::InitializeFromMemory");

		IWICBitmapDecoderPtr decoder;
		success_or_throw(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder), "IWICImagingFactory::CreateDecoderFromStream");

		IWICBitmapFrameDecodePtr frame;
		success_or_throw(decoder->GetFrame(0, &frame), "IWICBitmapDecoder::GetFrame");

		IWICFormatConverterPtr converter;
		success_or_throw(factory->CreateFormatConverter(&converter), "IWICImagingFactory::CreateFormatConverter");
		success_or_throw(converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom), "IWICFormatConverter::Initialize");

		UINT w, h;
		success_or_throw(converter->GetSize(&w, &h), "IWICFormatConverter::GetSize");
		if (!w || !h || w > 16384 || h > 16384)
			throw std::runtime_error("Invalid image size");
		pixels.resize(static_cast<size_t>(w) * h);
		success_or_throw(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels.size() * 4), reinterpret_cast<BYTE*>(pixels.data())), "IWICFormatConverter::CopyPixels");
		source.Width = static_cast<int>(w);
		source.Height = static_cast<int>(h);
	}
	source.Pixels = std::make_shared<const std::vector<uint32_t>>(std::move(pixels));
	return source;
}

std::shared_ptr<FontChanger::FixedSizeFont::glyph_file_set> FontChanger::FixedSizeFont::read_glyph_folder(const std::filesystem::path& folder) {
	auto res = std::make_shared<glyph_file_set>();
	std::error_code ec;
	if (!is_directory(folder, ec)) {
		res->Error = std::format(L"Folder not found: {}", folder.wstring());
		return res;
	}

	// SVG files are taken over PNG files of the same codepoint, and files without suffixes over ones with them.
	std::vector<std::filesystem::path> files;
	for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
		if (entry.is_regular_file(ec))
			files.push_back(entry.path());
	}
	std::ranges::sort(files, {}, [](const std::filesystem::path& p) {
		return std::make_tuple(to_lower(p.extension().wstring()) == L".svg" ? 0 : 1, p.stem().wstring().find(L'_') != std::wstring::npos, p.filename().wstring());
	});

	for (const auto& path : files) {
		const auto ext = to_lower(path.extension().wstring());
		if (lstrcmpiW(path.filename().c_str(), glyph_files_metadata_file_name) == 0) {
			try {
				res->Metadata = nlohmann::json::parse(read_file(path));
			} catch (const std::exception&) {
				res->SkippedCount++;
			}
			continue;
		}
		if (ext != L".svg" && ext != L".png")
			continue;

		// Files of codepoints already taken, such as the PNG files that are exported beside SVG files, are not counted.
		const auto codepoint = parse_glyph_file_name(path);
		if (!codepoint) {
			res->SkippedCount++;
			continue;
		}
		if (res->Glyphs.contains(*codepoint))
			continue;

		try {
			auto data = read_file(path);
			if (ext == L".svg") {
				image_fixed_size_font::svg_metrics metrics;
				if (!image_fixed_size_font::try_read_svg_metrics(data, metrics))
					throw std::runtime_error("Not an SVG document");
				res->Glyphs[*codepoint].Svg = std::make_shared<const std::string>(std::move(data));
			} else {
				res->Glyphs[*codepoint] = decode_png_glyph(data);
				res->PngFiles[*codepoint] = std::make_shared<const std::string>(std::move(data));
			}
		} catch (const std::exception&) {
			res->SkippedCount++;
		}
	}
	return res;
}

FontChanger::FixedSizeFont::image_fixed_size_font::create_struct FontChanger::FixedSizeFont::make_glyph_files_font_params(const glyph_file_set& set, const glyph_files_font_options& options, float size) {
	const auto& metadata = set.Metadata;

	image_fixed_size_font::create_struct params;
	params.Glyphs = set.Glyphs;
	params.BitmapCoverage = options.BitmapCoverage;
	params.FamilyName = options.FamilyName;

	// The options override font.json, which tells the units of the files that have no attributes for them.
	const auto metadataUnitsPerEm = json_number(metadata, "unitsPerEm").value_or(glyph_files_units_per_em);
	params.UnitsPerEm = options.UnitsPerEm.value_or(metadataUnitsPerEm);
	params.BaselineY = options.BaselineY.value_or(json_number(metadata, "baselineY").value_or(glyph_files_baseline_y));
	params.UnitsPerEmOverridesAttribute = options.UnitsPerEm.has_value();
	params.BaselineYOverridesAttribute = options.BaselineY.has_value();

	const auto fromMetadataUnits = params.UnitsPerEm / metadataUnitsPerEm;
	if (options.Ascent)
		params.Ascent = *options.Ascent;
	else if (const auto v = json_number(metadata, "ascent"))
		params.Ascent = *v * fromMetadataUnits;
	else
		params.Ascent = params.BaselineY;
	if (options.LineHeight)
		params.LineHeight = *options.LineHeight;
	else if (const auto v = json_number(metadata, "lineHeight"))
		params.LineHeight = *v * fromMetadataUnits;
	else
		params.LineHeight = params.UnitsPerEm;

	if (const auto it = metadata.is_object() ? metadata.find("kerning") : metadata.end(); metadata.is_object() && it != metadata.end() && it->is_array()) {
		for (const auto& entry : *it) {
			if (entry.is_array() && entry.size() == 3 && entry[0].is_number_unsigned() && entry[1].is_number_unsigned() && entry[2].is_number())
				params.KerningPairs[{entry[0].get<char32_t>(), entry[1].get<char32_t>()}] = entry[2].get<float>() * fromMetadataUnits;
		}
	}

	// PNG files are in pixels at the size that they were exported at, with the baseline at the ascent; without font.json,
	// they are drawn as they are at any size.
	params.BitmapUnitsPerEm = options.UnitsPerEm.value_or(json_number(metadata, "exportedSize").value_or(size));
	params.BitmapBaselineY = options.BaselineY.value_or(json_number(metadata, "ascentPx").value_or(std::round(params.BitmapUnitsPerEm * glyph_files_baseline_y / glyph_files_units_per_em)));
	if (const auto it = metadata.is_object() ? metadata.find("glyphs") : metadata.end(); metadata.is_object() && it != metadata.end() && it->is_object()) {
		for (auto& [codepoint, source] : params.Glyphs) {
			if (!source.Pixels)
				continue;
			const auto entry = it->find(std::format("U+{:04X}", static_cast<uint32_t>(codepoint)));
			if (entry == it->end() || !entry->is_object())
				continue;
			source.Advance = json_number(*entry, "advancePx");
			if (const auto origin = entry->find("pngOrigin"); origin != entry->end() && origin->is_array() && origin->size() == 2 && (*origin)[0].is_number() && (*origin)[1].is_number()) {
				source.OriginX = (*origin)[0].get<int>();
				source.OriginY = (*origin)[1].get<int>();
			}
		}
	}

	return params;
}
