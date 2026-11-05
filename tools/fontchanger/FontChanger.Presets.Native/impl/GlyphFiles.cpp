#include "pch.h"
#include "FontChanger.Presets/GlyphFiles.h"
#include "FontChanger.Presets/ElementFonts.h"

namespace {
	std::vector<uint8_t> DecodeBase64(std::string_view s) {
		std::vector<uint8_t> res;
		res.reserve(s.size() / 4 * 3);
		uint32_t acc = 0;
		int bits = 0;
		for (const auto c : s) {
			int v;
			if (c >= 'A' && c <= 'Z') v = c - 'A';
			else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
			else if (c >= '0' && c <= '9') v = c - '0' + 52;
			else if (c == '+') v = 62;
			else if (c == '/') v = 63;
			else continue;
			acc = acc << 6 | static_cast<uint32_t>(v);
			bits += 6;
			if (bits >= 8) {
				bits -= 8;
				res.push_back(static_cast<uint8_t>(acc >> bits & 0xFF));
			}
		}
		return res;
	}

	std::mutex s_folderCacheMtx;
	std::map<std::filesystem::path, std::shared_ptr<const FontChanger::FixedSizeFont::glyph_file_set>> s_folderCache;
	std::function<void(const std::filesystem::path&)> s_folderObserver;
}

std::filesystem::path FontChanger::GlyphFiles::ResolvePath(const std::string& path) {
	auto p = std::filesystem::path(xivres::util::unicode::convert<std::wstring>(path));
	if (p.is_relative()) {
		if (const auto base = ElementFonts::GetProjectDirectory(); !base.empty())
			p = base / p;
	}
	return p.lexically_normal();
}

std::shared_ptr<const FontChanger::FixedSizeFont::glyph_file_set> FontChanger::GlyphFiles::LoadGlyphSet(const Structs::GlyphImagesStruct& settings) {
	if (settings.IsEmbedded()) {
		auto res = std::make_shared<FontChanger::FixedSizeFont::glyph_file_set>();
		res->Metadata = settings.EmbeddedMetadata.value_or(nlohmann::json());
		for (const auto& [codepoint, glyph] : settings.Embedded) {
			try {
				if (glyph.Svg) {
					res->Glyphs[codepoint].Svg = glyph.Svg;
				} else if (glyph.PngBase64) {
					const auto png = DecodeBase64(*glyph.PngBase64);
					res->Glyphs[codepoint] = FontChanger::FixedSizeFont::decode_png_glyph({reinterpret_cast<const char*>(png.data()), png.size()});
				}
			} catch (const std::exception&) {
				res->SkippedCount++;
			}
		}
		return res;
	}

	if (settings.Path.empty()) {
		auto res = std::make_shared<FontChanger::FixedSizeFont::glyph_file_set>();
		res->Error = L"No folder";
		return res;
	}

	const auto folder = ResolvePath(settings.Path);
	std::function<void(const std::filesystem::path&)> observer;
	{
		const auto lock = std::scoped_lock(s_folderCacheMtx);
		observer = s_folderObserver;
	}
	if (observer)
		observer(folder);
	{
		const auto lock = std::scoped_lock(s_folderCacheMtx);
		if (const auto it = s_folderCache.find(folder); it != s_folderCache.end())
			return it->second;
	}

	std::shared_ptr<const FontChanger::FixedSizeFont::glyph_file_set> res = FontChanger::FixedSizeFont::read_glyph_folder(folder);
	const auto lock = std::scoped_lock(s_folderCacheMtx);
	return s_folderCache.insert_or_assign(folder, std::move(res)).first->second;
}

void FontChanger::GlyphFiles::InvalidateFolder(const std::filesystem::path& folder) {
	const auto lock = std::scoped_lock(s_folderCacheMtx);
	s_folderCache.erase(folder.lexically_normal());
}

void FontChanger::GlyphFiles::SetFolderObserver(std::function<void(const std::filesystem::path& folder)> observer) {
	const auto lock = std::scoped_lock(s_folderCacheMtx);
	s_folderObserver = std::move(observer);
}

std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font> FontChanger::GlyphFiles::CreateFont(
	const Structs::GlyphImagesStruct& settings,
	float size,
	float gamma,
	const FontChanger::FixedSizeFont::font_render_transformation_matrix& matrix) {
	auto params = FontChanger::FixedSizeFont::make_glyph_files_font_params(*LoadGlyphSet(settings), {
		.UnitsPerEm = settings.UnitsPerEm,
		.BaselineY = settings.BaselineY,
		.Ascent = settings.Ascent,
		.LineHeight = settings.LineHeight,
		.BitmapCoverage = settings.BitmapCoverage,
		.FamilyName = settings.IsEmbedded()
			? "(Embedded)"
			: xivres::util::unicode::convert<std::string>(ResolvePath(settings.Path).filename().wstring()),
	}, size);
	return std::make_shared<FontChanger::FixedSizeFont::image_fixed_size_font>(std::move(params), size, gamma, matrix);
}
