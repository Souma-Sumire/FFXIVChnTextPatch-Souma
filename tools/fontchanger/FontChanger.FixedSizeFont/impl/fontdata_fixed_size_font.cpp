#include "../include/FontChanger.FixedSizeFont/fontdata_fixed_size_font.h"

using namespace xivres;

#include <algorithm>
#include <format>

#include <nlohmann/json.hpp>

#include "xivres/texture.stream.h"
#include "xivres/util.bitmap_copy.h"

namespace {
	// The columns that the game drops from the left of a glyph when it loads a font (AtkFontManager.BuildFont): in fonts
	// larger than 35 px, the first of the IME indicators (U+E020-E027) and of U+E0BB, which are then drawn a pixel left and
	// advance a pixel less than their files say.
	int GetDroppedColumns(const fontdata::glyph_entry* pEntry, float fontSize) {
		const auto c = pEntry->codepoint();
		return fontSize > 35 && (c >= 0xE020 && c <= 0xE027 || c == 0xE0BB) && *pEntry->BoundingWidth > 0 ? 1 : 0;
	}
}

FontChanger::FixedSizeFont::glyph_metrics FontChanger::FixedSizeFont::fontdata_fixed_size_font::glyph_metrics_from_glyph_entry(const fontdata::glyph_entry* pEntry, int x, int y) {
	glyph_metrics res;
	res.X1 = x;
	res.Y1 = y + pEntry->CurrentOffsetY;
	res.X2 = res.X1 + pEntry->BoundingWidth;
	res.Y2 = res.Y1 + pEntry->BoundingHeight;
	res.AdvanceX = pEntry->BoundingWidth + pEntry->NextOffsetX;
	return res;
}

const FontChanger::FixedSizeFont::fixed_size_font* FontChanger::FixedSizeFont::fontdata_fixed_size_font::get_base_font(char32_t codepoint) const {
	return this;
}

std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font> FontChanger::FixedSizeFont::fontdata_fixed_size_font::get_threadsafe_view() const {
	return std::make_shared<fontdata_fixed_size_font>(*this);
}

bool FontChanger::FixedSizeFont::fontdata_fixed_size_font::draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const {
	const auto pEntry = m_info->Font->get_glyph(codepoint);
	if (!pEntry)
		return false;

	const auto dropped = GetDroppedColumns(pEntry, font_size());
	auto src = glyph_metrics{*pEntry->TextureOffsetX + dropped, *pEntry->TextureOffsetY, *pEntry->TextureOffsetX + *pEntry->BoundingWidth, *pEntry->TextureOffsetY + *pEntry->BoundingHeight};
	auto dest = glyph_metrics_from_glyph_entry(pEntry, drawX, drawY);
	dest.X2 -= dropped;
	const auto& mipmapStream = *m_info->Mipmaps.at(pEntry->texture_file_index());
	const auto planeIndex = fontdata::glyph_entry::ChannelMap[pEntry->texture_plane_index()];
	src.adjust_to_intersection(dest, mipmapStream.Width, mipmapStream.Height, destWidth, destHeight);
	util::bitmap_copy::to_l8()
		.from(&mipmapStream.as_span<uint8_t>()[planeIndex], mipmapStream.Width, mipmapStream.Height, 4, util::bitmap_vertical_direction::TopRowFirst)
		.to(pBuf, destWidth, destHeight, stride, util::bitmap_vertical_direction::TopRowFirst)
		.fore_color(fgColor)
		.fore_opacity(fgOpacity)
		.back_color(bgColor)
		.back_opacity(bgOpacity)
		.gamma_table(m_info->GammaTable)
		.copy(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);
	return true;
}

bool FontChanger::FixedSizeFont::fontdata_fixed_size_font::draw(char32_t codepoint, util::b8g8r8a8* pBuf, int drawX, int drawY, int destWidth, int destHeight, util::b8g8r8a8 fgColor, util::b8g8r8a8 bgColor) const {
	const auto pEntry = m_info->Font->get_glyph(codepoint);
	if (!pEntry)
		return false;

	const auto dropped = GetDroppedColumns(pEntry, font_size());
	auto src = glyph_metrics{*pEntry->TextureOffsetX + dropped, *pEntry->TextureOffsetY, *pEntry->TextureOffsetX + *pEntry->BoundingWidth, *pEntry->TextureOffsetY + *pEntry->BoundingHeight};
	auto dest = glyph_metrics_from_glyph_entry(pEntry, drawX, drawY);
	dest.X2 -= dropped;
	const auto& mipmapStream = *m_info->Mipmaps.at(pEntry->texture_file_index());
	const auto planeIndex = fontdata::glyph_entry::ChannelMap[pEntry->texture_plane_index()];
	src.adjust_to_intersection(dest, mipmapStream.Width, mipmapStream.Height, destWidth, destHeight);
	util::bitmap_copy::to_b8g8r8a8()
		.from(&mipmapStream.as_span<uint8_t>()[planeIndex], mipmapStream.Width, mipmapStream.Height, 4, util::bitmap_vertical_direction::TopRowFirst)
		.to(pBuf, destWidth, destHeight, util::bitmap_vertical_direction::TopRowFirst)
		.fore_color(fgColor)
		.back_color(bgColor)
		.gamma_table(m_info->GammaTable)
		.copy(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);
	return true;
}

int FontChanger::FixedSizeFont::fontdata_fixed_size_font::get_adjusted_advance_width(char32_t left, char32_t right) const {
	glyph_metrics gm;
	if (!try_get_glyph_metrics(left, gm))
		return 0;

	return gm.AdvanceX + m_info->Font->get_kerning(left, right);
}

const std::map<std::pair<char32_t, char32_t>, int>& FontChanger::FixedSizeFont::fontdata_fixed_size_font::all_kerning_pairs() const {
	return m_info->KerningPairs;
}

bool FontChanger::FixedSizeFont::fontdata_fixed_size_font::try_get_glyph_metrics(char32_t codepoint, glyph_metrics& gm) const {
	const auto p = m_info->Font->get_glyph(codepoint);
	if (!p)
		return false;

	gm = glyph_metrics_from_glyph_entry(p);
	const auto dropped = GetDroppedColumns(p, font_size());
	gm.X2 -= dropped;
	gm.AdvanceX -= dropped;
	return true;
}

const std::set<char32_t>& FontChanger::FixedSizeFont::fontdata_fixed_size_font::all_codepoints() const {
	return m_info->Codepoints;
}

int FontChanger::FixedSizeFont::fontdata_fixed_size_font::line_height() const {
	return m_info->Font->line_height();
}

int FontChanger::FixedSizeFont::fontdata_fixed_size_font::ascent() const {
	return m_info->Font->ascent();
}

float FontChanger::FixedSizeFont::fontdata_fixed_size_font::font_size() const {
	return m_info->Font->font_size();
}

std::string FontChanger::FixedSizeFont::fontdata_fixed_size_font::subfamily_name() const {
	return m_info->SubfamilyName;
}

std::string FontChanger::FixedSizeFont::fontdata_fixed_size_font::family_name() const {
	return m_info->FamilyName;
}

FontChanger::FixedSizeFont::fontdata_fixed_size_font::fontdata_fixed_size_font(std::shared_ptr<const fontdata::stream> strm, std::vector<std::shared_ptr<texture::memory_mipmap_stream>> mipmapStreams, std::string familyName, std::string subfamilyName) {
	for (const auto& mipmapStream : mipmapStreams) {
		if (mipmapStream->Type != texture::formats::B8G8R8A8)
			throw std::invalid_argument("All mipmap streams must be in A8R8G8B8 format.");
	}

	auto info = std::make_shared<struct info>();
	info->Font = std::move(strm);
	info->FamilyName = std::move(familyName);
	info->SubfamilyName = std::move(subfamilyName);
	info->Mipmaps = std::move(mipmapStreams);
	info->GammaTable = util::bitmap_copy::create_gamma_table(1.f);

	for (const auto& entry : info->Font->get_glyphs())
		info->Codepoints.insert(info->Codepoints.end(), entry.codepoint());

	for (const auto& entry : info->Font->get_kernings())
		info->KerningPairs.emplace_hint(info->KerningPairs.end(), std::make_pair(entry.left(), entry.right()), entry.RightOffset);

	m_info = std::move(info);
}

FontChanger::FixedSizeFont::fontdata_fixed_size_font::fontdata_fixed_size_font() = default;

FontChanger::FixedSizeFont::fontdata_fixed_size_font::fontdata_fixed_size_font(fontdata_fixed_size_font&&) noexcept = default;

FontChanger::FixedSizeFont::fontdata_fixed_size_font::fontdata_fixed_size_font(const fontdata_fixed_size_font& r) = default;

FontChanger::FixedSizeFont::fontdata_fixed_size_font& FontChanger::FixedSizeFont::fontdata_fixed_size_font::operator=(fontdata_fixed_size_font&&) noexcept = default;

FontChanger::FixedSizeFont::fontdata_fixed_size_font& FontChanger::FixedSizeFont::fontdata_fixed_size_font::operator=(const fontdata_fixed_size_font&) = default;

FontChanger::FixedSizeFont::game_fontdata_set::operator bool() const {
	return !m_data.empty();
}

size_t FontChanger::FixedSizeFont::game_fontdata_set::count() const {
	return m_data.size();
}

std::shared_ptr<FontChanger::FixedSizeFont::fontdata_fixed_size_font> FontChanger::FixedSizeFont::game_fontdata_set::get_font(std::string_view family, float size) const {
	std::vector<size_t> candidates;
	candidates.reserve(5);

	for (size_t i = 0; i < m_data.size(); i++) {
		if (m_data[i]->family_name() == family)
			candidates.push_back(i);
	}

	if (candidates.empty())
		return {};

	std::ranges::sort(candidates, [this, size](const auto& l, const auto& r) {
		return std::fabsf(size - m_data[l]->font_size()) < std::fabsf(size - m_data[r]->font_size());
	});

	return m_data[candidates[0]];
}

std::shared_ptr<FontChanger::FixedSizeFont::fontdata_fixed_size_font> FontChanger::FixedSizeFont::game_fontdata_set::get_font(size_t i) const {
	return m_data[i];
}

std::shared_ptr<FontChanger::FixedSizeFont::fontdata_fixed_size_font> FontChanger::FixedSizeFont::game_fontdata_set::operator[](size_t i) const {
	return m_data[i];
}

FontChanger::FixedSizeFont::game_fontdata_set::game_fontdata_set(std::vector<std::shared_ptr<fontdata_fixed_size_font>> data)
	: m_data(std::move(data)) {
}

FontChanger::FixedSizeFont::game_fontdata_set::game_fontdata_set() = default;

FontChanger::FixedSizeFont::game_fontdata_set::game_fontdata_set(game_fontdata_set&&) noexcept = default;

FontChanger::FixedSizeFont::game_fontdata_set::game_fontdata_set(const game_fontdata_set&) = default;

FontChanger::FixedSizeFont::game_fontdata_set& FontChanger::FixedSizeFont::game_fontdata_set::operator=(game_fontdata_set&&) noexcept = default;

FontChanger::FixedSizeFont::game_fontdata_set& FontChanger::FixedSizeFont::game_fontdata_set::operator=(const game_fontdata_set&) = default;

namespace {
	// The game's fonts (data/game_fonts.json, which FontChanger.Presets reads too).
#include "game_fonts.json.h"

	struct game_fonts {
		struct font_type_info {
			xivres::font_type FontType;
			std::string TextureFormat;
			int TextureCount;
			std::vector<FontChanger::FixedSizeFont::game_fontdata_definition> Fonts;
			std::vector<FontChanger::FixedSizeFont::game_font_table_entry> Table;
		};

		std::vector<xivres::font_type> FontTypes;
		std::vector<font_type_info> Infos;
		std::vector<std::string> Families;
		std::vector<std::pair<std::string, std::string>> LinkedNames;

		[[nodiscard]] const font_type_info* find(xivres::font_type fontType) const {
			const auto it = std::ranges::find(Infos, fontType, &font_type_info::FontType);
			return it == Infos.end() ? nullptr : &*it;
		}
	};

	xivres::font_type parse_font_type(std::string_view name) {
		if (name == "font") return xivres::font_type::font;
		if (name == "font_lobby") return xivres::font_type::font_lobby;
		if (name == "chn_axis") return xivres::font_type::chn_axis;
		if (name == "krn_axis") return xivres::font_type::krn_axis;
		if (name == "tc_axis") return xivres::font_type::tc_axis;
		throw std::runtime_error(std::format("Unknown font type: {}", name));
	}

	const game_fonts& get_game_fonts() {
		static const auto s_fonts = [] {
			const auto json = nlohmann::ordered_json::parse(GameFontsJson);
			game_fonts res;

			std::map<std::string, std::string, std::less<>> subfamilies;
			for (const auto& [family, info] : json.at("families").items()) {
				res.Families.push_back(family);
				subfamilies.emplace(family, info.at("subfamily").get<std::string>());
			}

			// Earlier versions of the file called "fonts" and "linkedFonts" "faces" and "linkedFaces".
			const auto at = [](const nlohmann::ordered_json& obj, const char* key, const char* legacyKey) -> const nlohmann::ordered_json& {
				return obj.contains(key) ? obj.at(key) : obj.at(legacyKey);
			};

			// Reserved first: the tables point to the fonts.
			res.Infos.reserve(json.at("fontTypes").size());
			for (const auto& [typeName, info] : json.at("fontTypes").items()) {
				auto& type = res.Infos.emplace_back(game_fonts::font_type_info{
					.FontType = parse_font_type(typeName),
					.TextureFormat = info.at("textures").get<std::string>(),
					.TextureCount = info.at("textureCount").get<int>(),
				});
				res.FontTypes.push_back(type.FontType);
				for (const auto& font : at(info, "fonts", "faces")) {
					const auto name = font.at(0).get<std::string>();
					const auto family = font.at(1).get<std::string>();
					type.Fonts.push_back({
						.FontType = type.FontType,
						.Path = std::format("common/font/{}.fdt", name),
						.Name = name,
						.Family = family,
						.Subfamily = subfamilies.find(family)->second,
						.Size = font.at(2).get<float>(),
					});
				}
			}

			for (const auto& pair : at(json, "linkedFonts", "linkedFaces"))
				res.LinkedNames.emplace_back(pair.at(0).get<std::string>(), pair.at(1).get<std::string>());

			for (const auto& [typeName, info] : json.at("fontTables").items()) {
				auto& type = *std::ranges::find(res.Infos, parse_font_type(typeName), &game_fonts::font_type_info::FontType);
				const auto counts = info.value("textureCounts", nlohmann::ordered_json::object());
				for (const auto& slot : info.at("slots")) {
					const auto name = slot.get<std::string>();
					for (const auto& owner : res.Infos) {
						const auto font = std::ranges::find(owner.Fonts, name, &FontChanger::FixedSizeFont::game_fontdata_definition::Name);
						if (font == owner.Fonts.end())
							continue;

						auto count = owner.TextureCount;
						for (const auto& [ownerName, ownerCount] : counts.items()) {
							if (parse_font_type(ownerName) == owner.FontType)
								count = ownerCount.get<int>();
						}
						type.Table.push_back({&*font, count});
						break;
					}
				}
			}
			return res;
		}();
		return s_fonts;
	}
}

std::span<const xivres::font_type> FontChanger::FixedSizeFont::get_font_types() {
	return get_game_fonts().FontTypes;
}

std::span<const FontChanger::FixedSizeFont::game_fontdata_definition> FontChanger::FixedSizeFont::get_fontdata_definition(font_type fontType) {
	const auto info = get_game_fonts().find(fontType);
	return info ? std::span(info->Fonts) : std::span<const game_fontdata_definition>();
}

const FontChanger::FixedSizeFont::game_fontdata_definition* FontChanger::FixedSizeFont::find_fontdata_definition(std::string_view name) {
	for (const auto& info : get_game_fonts().Infos) {
		if (const auto it = std::ranges::find(info.Fonts, name, &game_fontdata_definition::Name); it != info.Fonts.end())
			return &*it;
	}
	return nullptr;
}

std::span<const std::string> FontChanger::FixedSizeFont::get_font_families() {
	return get_game_fonts().Families;
}

const char* FontChanger::FixedSizeFont::get_font_tex_filename_format(font_type fontType /*= font_type::font*/) {
	const auto info = get_game_fonts().find(fontType);
	return info ? info->TextureFormat.c_str() : nullptr;
}

int FontChanger::FixedSizeFont::get_font_texture_count(font_type fontType /*= font_type::font*/) {
	const auto info = get_game_fonts().find(fontType);
	return info ? info->TextureCount : 0;
}

std::span<const std::pair<std::string, std::string>> FontChanger::FixedSizeFont::get_linked_fontdata_names() {
	return get_game_fonts().LinkedNames;
}

std::span<const FontChanger::FixedSizeFont::game_font_table_entry> FontChanger::FixedSizeFont::get_font_table(font_type fontType /*= font_type::font*/) {
	const auto info = get_game_fonts().find(fontType);
	return info ? std::span(info->Table) : std::span<const game_font_table_entry>();
}

FontChanger::FixedSizeFont::game_fontdata_set FontChanger::FixedSizeFont::get_fontdata_set(const installation& installation, font_type fontType) {
	const auto texturePathPattern = get_font_tex_filename_format(fontType);
	std::vector<std::shared_ptr<texture::memory_mipmap_stream>> textures;
	try {
		for (int i = 1; ; i++)
			textures.emplace_back(texture::memory_mipmap_stream::as_argb8888(*texture::stream(installation.get_file(std::vformat(std::string_view(texturePathPattern), std::make_format_args(i)))).mipmap_at(0, 0)));
	} catch (const std::out_of_range&) {
		// do nothing
	}

	const auto definitions = get_fontdata_definition(fontType);
	std::vector<std::shared_ptr<fontdata_fixed_size_font>> fonts;
	fonts.reserve(definitions.size());
	for (const auto& def : definitions) {
		fonts.emplace_back(std::make_shared<fontdata_fixed_size_font>(
			std::make_shared<fontdata::stream>(*installation.get_file(def.Path)),
			textures,
			def.Family,
			def.Subfamily));
	}

	return {fonts};
}
