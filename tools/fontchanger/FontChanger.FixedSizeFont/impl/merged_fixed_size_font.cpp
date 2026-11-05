#include "../include/FontChanger.FixedSizeFont/merged_fixed_size_font.h"

using namespace xivres;

#include <cmath>

#include "../include/FontChanger.FixedSizeFont/util.truetype.h"

int FontChanger::FixedSizeFont::merged_fixed_size_font::get_vertical_adjustment(const info& info, const FontChanger::FixedSizeFont::fixed_size_font& font) {
	switch (info.Alignment) {
		case vertical_alignment::Top:
			return 0;
		case vertical_alignment::Middle:
			return 0 + (info.LineHeight - font.line_height()) / 2;
		case vertical_alignment::Baseline:
			return 0 + info.Ascent - font.ascent();
		case vertical_alignment::Bottom:
			return 0 + info.LineHeight - font.line_height();
		case vertical_alignment::RomanBaseline:
			return static_cast<int>(std::lround(info.RomanBaselineY - get_roman_baseline_y(font)));
		case vertical_alignment::IdeographicCenter:
			return static_cast<int>(std::lround(info.IdeographicCenterY - get_ideographic_center_y(font)));
		default:
			throw std::runtime_error("Invalid alignment value set");
	}
}

float FontChanger::FixedSizeFont::merged_fixed_size_font::get_roman_baseline_y(const fixed_size_font& font) {
	return static_cast<float>(font.ascent()) - font.get_baseline(truetype::Base::RomanBaselineTag.NativeValue).value_or(0.f);
}

float FontChanger::FixedSizeFont::merged_fixed_size_font::get_ideographic_center_y(const fixed_size_font& font) {
	using truetype::Base;
	const auto bottom = font.get_baseline(Base::IdeographicFaceBottomTag.NativeValue);
	const auto top = font.get_baseline(Base::IdeographicFaceTopTag.NativeValue);
	if (bottom && top)
		return static_cast<float>(font.ascent()) - (*bottom + *top) / 2;

	const auto emBottom = font.get_baseline(Base::IdeographicEmBoxBottomTag.NativeValue);
	const auto emTop = font.get_baseline(Base::IdeographicEmBoxTopTag.NativeValue);
	if (emBottom && emTop)
		return static_cast<float>(font.ascent()) - (*emBottom + *emTop) / 2;

	return static_cast<float>(font.line_height()) / 2;
}

std::optional<float> FontChanger::FixedSizeFont::merged_fixed_size_font::get_baseline(uint32_t baselineTag) const {
	return m_fonts.empty() ? std::nullopt : m_fonts.front()->get_baseline(baselineTag);
}

const FontChanger::FixedSizeFont::fixed_size_font* FontChanger::FixedSizeFont::merged_fixed_size_font::get_base_font(char32_t codepoint) const {
	if (const auto it = m_info->UsedFontIndices.find(codepoint); it != m_info->UsedFontIndices.end())
		return m_fonts[it->second]->get_base_font(codepoint);

	return nullptr;
}

std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font> FontChanger::FixedSizeFont::merged_fixed_size_font::get_threadsafe_view() const {
	auto res = std::make_shared<merged_fixed_size_font>(*this);
	res->m_fonts.clear();
	for (const auto& font : m_fonts)
		res->m_fonts.emplace_back(font->get_threadsafe_view());
	return res;
}

bool FontChanger::FixedSizeFont::merged_fixed_size_font::draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const {
	if (const auto it = m_info->UsedFontIndices.find(codepoint); it != m_info->UsedFontIndices.end())
		return m_fonts[it->second]->draw(codepoint, pBuf, stride, drawX, drawY + get_vertical_adjustment(*m_info, *m_fonts[it->second]), destWidth, destHeight, fgColor, bgColor, fgOpacity, bgOpacity);

	return false;
}

bool FontChanger::FixedSizeFont::merged_fixed_size_font::draw(char32_t codepoint, util::b8g8r8a8* pBuf, int drawX, int drawY, int destWidth, int destHeight, util::b8g8r8a8 fgColor, util::b8g8r8a8 bgColor) const {
	if (const auto it = m_info->UsedFontIndices.find(codepoint); it != m_info->UsedFontIndices.end())
		return m_fonts[it->second]->draw(codepoint, pBuf, drawX, drawY + get_vertical_adjustment(*m_info, *m_fonts[it->second]), destWidth, destHeight, fgColor, bgColor);

	return false;
}

const std::map<std::pair<char32_t, char32_t>, int>& FontChanger::FixedSizeFont::merged_fixed_size_font::all_kerning_pairs() const {
	if (m_kerningPairs)
		return *m_kerningPairs;

	std::map<fixed_size_font*, std::set<char32_t>> charsPerFonts;
	for (const auto& [codepoint, fontIndex] : m_info->UsedFontIndices)
		charsPerFonts[m_fonts[fontIndex].get()].insert(codepoint);

	m_kerningPairs.emplace();
	for (const auto& [font, chars] : charsPerFonts) {
		for (const auto& kerningPair : font->all_kerning_pairs()) {
			if (kerningPair.first.first < U' ' || kerningPair.first.second < U' ')
				continue;

			if (chars.contains(kerningPair.first.first) && chars.contains(kerningPair.first.second) && kerningPair.second)
				m_kerningPairs->emplace(kerningPair);
		}
	}

	return *m_kerningPairs;
}

bool FontChanger::FixedSizeFont::merged_fixed_size_font::try_get_glyph_metrics(char32_t codepoint, glyph_metrics& gm) const {
	if (const auto it = m_info->UsedFontIndices.find(codepoint); it != m_info->UsedFontIndices.end()) {
		const auto& font = *m_fonts[it->second];
		if (!font.try_get_glyph_metrics(codepoint, gm))
			return false;

		gm.translate(0, get_vertical_adjustment(*m_info, font));
		return true;
	}

	return false;
}

bool FontChanger::FixedSizeFont::merged_fixed_size_font::try_get_glyph_outline(char32_t codepoint, glyph_outline& outline) const {
	const auto it = m_info->UsedFontIndices.find(codepoint);
	if (it == m_info->UsedFontIndices.end())
		return false;

	const auto& font = *m_fonts[it->second];
	if (!font.try_get_glyph_outline(codepoint, outline))
		return false;

	outline.translate(0, static_cast<float>(get_vertical_adjustment(*m_info, font)));
	return true;
}

char32_t FontChanger::FixedSizeFont::merged_fixed_size_font::uniqid_to_glyph(const void* pc) const {
	for (const auto& font : m_fonts) {
		if (const auto r = font->uniqid_to_glyph(pc))
			return r;
	}
	return 0;
}

const void* FontChanger::FixedSizeFont::merged_fixed_size_font::get_base_font_glyph_uniqid(char32_t codepoint) const {
	if (const auto it = m_info->UsedFontIndices.find(codepoint); it != m_info->UsedFontIndices.end())
		return m_fonts[it->second]->get_base_font_glyph_uniqid(codepoint);

	return nullptr;
}

const std::set<char32_t>& FontChanger::FixedSizeFont::merged_fixed_size_font::all_codepoints() const {
	return m_info->Codepoints;
}

int FontChanger::FixedSizeFont::merged_fixed_size_font::line_height() const {
	return m_info->LineHeight;
}

int FontChanger::FixedSizeFont::merged_fixed_size_font::ascent() const {
	return m_info->Ascent;
}

float FontChanger::FixedSizeFont::merged_fixed_size_font::font_size() const {
	return m_info->Size;
}

std::string FontChanger::FixedSizeFont::merged_fixed_size_font::subfamily_name() const {
	return {};
}

std::string FontChanger::FixedSizeFont::merged_fixed_size_font::family_name() const {
	return "Merged";
}

FontChanger::FixedSizeFont::merged_fixed_size_font::merged_fixed_size_font(std::vector<std::pair<std::shared_ptr<fixed_size_font>, codepoint_merge_mode>> fonts, vertical_alignment verticalAlignment) {
	auto info = std::make_shared<struct info>();
	if (fonts.empty())
		fonts.emplace_back(std::make_shared<empty_fixed_size_font>(), codepoint_merge_mode::AddAll);

	info->Alignment = verticalAlignment;
	info->Size = fonts.front().first->font_size();
	info->Ascent = fonts.front().first->ascent();
	info->LineHeight = fonts.front().first->line_height();
	info->RomanBaselineY = get_roman_baseline_y(*fonts.front().first);
	info->IdeographicCenterY = get_ideographic_center_y(*fonts.front().first);

	for (size_t i = 0; i < fonts.size(); i++) {
		auto& [font, mergeMode] = fonts[i];

		for (const auto c : font->all_codepoints()) {
			switch (mergeMode) {
				case codepoint_merge_mode::AddNew:
					if (info->UsedFontIndices.emplace(c, i).second)
						info->Codepoints.insert(c);
					break;
				case codepoint_merge_mode::Replace:
					if (const auto it = info->UsedFontIndices.find(c); it != info->UsedFontIndices.end())
						it->second = i;
					break;
				case codepoint_merge_mode::AddAll:
					info->UsedFontIndices.insert_or_assign(c, i);
					info->Codepoints.insert(c);
					break;
				default:
					throw std::invalid_argument("Invalid MergedFontCodepointMode");
			}
		}

		m_fonts.emplace_back(std::move(font));
	}

	m_info = std::move(info);
}

FontChanger::FixedSizeFont::merged_fixed_size_font::merged_fixed_size_font() = default;

FontChanger::FixedSizeFont::merged_fixed_size_font::merged_fixed_size_font(merged_fixed_size_font&&) noexcept = default;

FontChanger::FixedSizeFont::merged_fixed_size_font::merged_fixed_size_font(const merged_fixed_size_font&) = default;

FontChanger::FixedSizeFont::merged_fixed_size_font& FontChanger::FixedSizeFont::merged_fixed_size_font::operator=(merged_fixed_size_font&&) noexcept = default;

FontChanger::FixedSizeFont::merged_fixed_size_font& FontChanger::FixedSizeFont::merged_fixed_size_font::operator=(const merged_fixed_size_font&) = default;
