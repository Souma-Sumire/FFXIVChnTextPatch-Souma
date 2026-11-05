#include "../include/FontChanger.FixedSizeFont/wrapping_fixed_size_font.h"

using namespace xivres;

#include <algorithm>
#include <cmath>

#include "xivres/util.bitmap_copy.h"
#include "xivres/util.unicode.h"

namespace {
	int floor_half(int value) {
		return value >= 0 ? value / 2 : -((-value + 1) / 2);
	}

	// Squeezes rows of coverage horizontally from srcWidth to destWidth columns; each destination pixel takes the average
	// of the source pixels under it, weighted by how much of each it covers, so that the total ink of a row is kept.
	std::vector<uint8_t> squeeze_coverage(const std::vector<uint8_t>& src, int srcWidth, int height, int destWidth) {
		std::vector<uint8_t> dest(static_cast<size_t>(destWidth) * height);
		for (int y = 0; y < height; y++) {
			const auto srcRow = &src[static_cast<size_t>(y) * srcWidth];
			const auto destRow = &dest[static_cast<size_t>(y) * destWidth];

			// In units of 1 / (srcWidth * destWidth) of the row, source pixel i spans [i * destWidth, (i + 1) * destWidth)
			// and destination pixel j spans [j * srcWidth, (j + 1) * srcWidth).
			for (int j = 0; j < destWidth; j++) {
				const auto from = j * srcWidth;
				const auto to = from + srcWidth;
				int sum = 0;
				for (int i = from / destWidth; i < srcWidth && i * destWidth < to; i++) {
					const auto overlap = (std::min)((i + 1) * destWidth, to) - (std::max)(i * destWidth, from);
					if (overlap > 0)
						sum += srcRow[i] * overlap;
				}
				destRow[j] = static_cast<uint8_t>((sum + srcWidth / 2) / srcWidth);
			}
		}
		return dest;
	}

	const std::vector<uint8_t>& identity_gamma_table() {
		static const auto table = xivres::util::bitmap_copy::create_gamma_table(1.f);
		return table;
	}
}

std::optional<float> FontChanger::FixedSizeFont::wrapping_fixed_size_font::get_monospacing_unit_pixels(const fixed_size_font& font, monospacing_unit unit, char32_t referenceCharacter) {
	switch (unit) {
		case monospacing_unit::Pixels:
			return 1.f;
		case monospacing_unit::Em:
			return font.font_size();
		case monospacing_unit::ReferenceCharacter:
			if (glyph_metrics gm; font.try_get_glyph_metrics(referenceCharacter, gm))
				return static_cast<float>(gm.AdvanceX);
			return std::nullopt;
	}
	return std::nullopt;
}

FontChanger::FixedSizeFont::wrapping_fixed_size_font::placement FontChanger::FixedSizeFont::wrapping_fixed_size_font::place(char32_t codepoint, const glyph_metrics& gm) const {
	const auto& info = *m_info;
	placement p;

	// Glyphs that do not advance, such as combining marks, are left as they are.
	if (info.Monospaced && gm.AdvanceX > 0) {
		p.Monospaced = true;
		const auto cell = std::clamp(gm.AdvanceX, info.MinAdvance, info.MaxAdvance);

		int x1;
		if (gm.is_effectively_empty()) {
			x1 = (std::max)(0, gm.X1);
		} else {
			if (info.HasMaxAdvance && cell > 0 && gm.width() > cell) {
				p.SqueezedWidth = cell;
				p.ScaledFont = find_scaled_glyph(codepoint, gm, cell, p.ScaledMetrics);

				// Rendered narrower, the ink may come out narrower than the cell, as the scales are quantized.
				x1 = p.ScaledFont ? floor_half(cell - p.ScaledMetrics.width()) : 0;
			} else {
				switch (info.Alignment) {
					case monospacing_alignment::Left:
						x1 = gm.X1;
						break;
					case monospacing_alignment::CenterInk:
						x1 = floor_half(cell - gm.width());
						break;
					case monospacing_alignment::Right:
						x1 = gm.X1 + cell - gm.AdvanceX;
						break;
					case monospacing_alignment::CenterAdvance:
					default:
						x1 = gm.X1 + floor_half(cell - gm.AdvanceX);
						break;
				}
			}

			// The horizontal offset nudges the glyph within its cell; the glyph still may not start left of the pen, and
			// it is moved right instead of growing the advance, so that the advance stays the width of the cell.
			x1 = (std::max)(0, x1 + info.HorizontalOffset);
		}

		p.ShiftX = x1 - (p.ScaledFont ? p.ScaledMetrics.X1 : gm.X1);
		p.AdvanceX = cell + info.LetterSpacing;
		return p;
	}

	const auto remainingOffset = gm.X1 + info.HorizontalOffset;
	if (remainingOffset >= 0) {
		p.ShiftX = info.HorizontalOffset;
		p.AdvanceX = gm.AdvanceX + info.LetterSpacing;
	} else {
		// The glyph is moved right so that it does not start left of the pen, and the advance grows by the same amount;
		// all_kerning_pairs adds remainingOffset back where possible, so that net pen movement stays AdvanceX + LetterSpacing.
		p.ShiftX = -gm.X1;
		p.AdvanceX = gm.AdvanceX + info.LetterSpacing - remainingOffset;
		p.NegativeLsbCompensation = remainingOffset;
	}
	return p;
}

const FontChanger::FixedSizeFont::fixed_size_font* FontChanger::FixedSizeFont::wrapping_fixed_size_font::find_scaled_glyph(char32_t codepoint, const glyph_metrics& gm, int width, glyph_metrics& scaledGm) const {
	if (!m_scaledFontFactory || !m_scaledFontCache || gm.width() <= 0)
		return nullptr;

	auto& cache = *m_scaledFontCache;
	auto scale = 0;
	{
		const auto lock = std::scoped_lock(cache.Mutex);
		if (const auto it = cache.GlyphScales.find(codepoint); it != cache.GlyphScales.end()) {
			scale = it->second;
		} else {
			const auto getFont = [&](int s) -> const fixed_size_font* {
				auto it2 = cache.Fonts.find(s);
				if (it2 == cache.Fonts.end()) {
					std::shared_ptr<fixed_size_font> font;

					// Making a font fails now and then while other threads use DirectWrite, so it is tried again.
					for (auto attempt = 0; attempt < 3 && !font; attempt++) {
						try {
							font = m_scaledFontFactory(static_cast<float>(s) / ScaleSteps);
						} catch (...) {
							// Left null if all attempts fail, so that the glyphs are resampled instead.
						}
					}
					it2 = cache.Fonts.emplace(s, std::move(font)).first;
				}
				return it2->second.get();
			};

			// Hinting and rounding make the ink of a scaled glyph only roughly as wide as the scale says; narrower scales
			// are tried until the ink fits.
			auto candidate = std::clamp(width * ScaleSteps / gm.width(), 1, ScaleSteps - 1);
			for (auto attempt = 0; attempt < 8 && candidate >= 1; attempt++) {
				const auto font = getFont(candidate);
				glyph_metrics candidateGm;
				if (!font || !font->try_get_glyph_metrics(codepoint, candidateGm))
					break;
				if (candidateGm.width() <= width) {
					scale = candidate;
					break;
				}
				candidate = (std::min)(candidate - 1, candidate * width / candidateGm.width());
			}
			cache.GlyphScales.emplace(codepoint, scale);
		}
	}
	if (!scale)
		return nullptr;

	auto& font = m_scaledFonts[scale];
	if (!font) {
		const auto lock = std::scoped_lock(cache.Mutex);
		font = cache.Fonts.at(scale)->get_threadsafe_view();
	}
	return font->try_get_glyph_metrics(codepoint, scaledGm) ? font.get() : nullptr;
}

void FontChanger::FixedSizeFont::wrapping_fixed_size_font::apply_placement(glyph_metrics& gm, const placement& p, int verticalOffset) {
	if (p.ScaledFont)
		gm = p.ScaledMetrics;
	else if (p.SqueezedWidth)
		gm.X2 = gm.X1 + p.SqueezedWidth;
	gm.translate(p.ShiftX, verticalOffset);
	gm.AdvanceX = p.AdvanceX;
}

template<typename TDraw>
bool FontChanger::FixedSizeFont::wrapping_fixed_size_font::draw_squeezed(char32_t codepoint, const glyph_metrics& gm, const placement& p, int drawX, int drawY, TDraw&& draw) const {
	const auto srcWidth = gm.width();
	const auto height = gm.height();
	if (srcWidth <= 0 || height <= 0)
		return true;

	// The coverage that the base font draws, with its gamma applied, is resampled, and then drawn with the colors.
	std::vector<uint8_t> coverage(static_cast<size_t>(srcWidth) * height);
	if (!m_font->draw(codepoint, coverage.data(), 1, -gm.X1, -gm.Y1, srcWidth, height, 255, 0, 255, 0))
		return false;

	const auto squeezed = squeeze_coverage(coverage, srcWidth, height, p.SqueezedWidth);

	auto dest = gm;
	dest.X2 = dest.X1 + p.SqueezedWidth;
	dest.translate(drawX + p.ShiftX, drawY);
	auto src = dest;
	src.translate(-src.X1, -src.Y1);
	draw(squeezed.data(), p.SqueezedWidth, height, src, dest);
	return true;
}

std::vector<std::pair<char32_t, int>> FontChanger::FixedSizeFont::wrapping_fixed_size_font::get_negative_lsb_codepoints() const {
	std::vector<std::pair<char32_t, int>> result;
	glyph_metrics gm;
	for (const auto codepoint : m_info->Codepoints) {
		if (!m_font->try_get_glyph_metrics(translate_codepoint(codepoint), gm))
			continue;

		if (place(translate_codepoint(codepoint), gm).NegativeLsbCompensation < 0)
			result.emplace_back(codepoint, gm.X1);
	}
	return result;
}

char32_t FontChanger::FixedSizeFont::wrapping_fixed_size_font::translate_codepoint(char32_t codepoint) const {
	if (const auto it = m_info->MappedCodepoints.find(codepoint); it != m_info->MappedCodepoints.end())
		return translate_codepoint(it->second);
	return codepoint;
}

const FontChanger::FixedSizeFont::fixed_size_font* FontChanger::FixedSizeFont::wrapping_fixed_size_font::get_base_font(char32_t codepoint) const {
	if (!m_info->Codepoints.contains(codepoint))
		return nullptr;

	// Squeezed glyphs are not the glyphs of the base font, so this font draws them into the texture itself. Only a
	// maximum width squeezes glyphs; measuring glyphs otherwise would cost as much as drawing them, for every glyph.
	if (glyph_metrics gm; m_info->Monospaced && m_info->HasMaxAdvance && m_font->try_get_glyph_metrics(translate_codepoint(codepoint), gm) && place(translate_codepoint(codepoint), gm).SqueezedWidth)
		return this;

	return m_font->get_base_font(codepoint);
}

std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font> FontChanger::FixedSizeFont::wrapping_fixed_size_font::get_threadsafe_view() const {
	auto res = std::make_shared<wrapping_fixed_size_font>(*this);
	res->m_font = m_font->get_threadsafe_view();
	res->m_scaledFonts.clear();
	return res;
}

bool FontChanger::FixedSizeFont::wrapping_fixed_size_font::draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const {
	codepoint = translate_codepoint(codepoint);

	glyph_metrics gm;
	if (!m_font->try_get_glyph_metrics(codepoint, gm))
		return false;

	const auto p = place(codepoint, gm);
	drawY += m_info->VerticalOffset;
	if (p.ScaledFont)
		return p.ScaledFont->draw(codepoint, pBuf, stride, drawX + p.ShiftX, drawY, destWidth, destHeight, fgColor, bgColor, fgOpacity, bgOpacity);
	if (p.SqueezedWidth) {
		return draw_squeezed(codepoint, gm, p, drawX, drawY, [&](const uint8_t* pSrc, int srcWidth, int srcHeight, glyph_metrics src, glyph_metrics dest) {
			src.adjust_to_intersection(dest, srcWidth, srcHeight, destWidth, destHeight);
			if (src.is_effectively_empty() || dest.is_effectively_empty())
				return;

			util::bitmap_copy::to_l8()
				.from(pSrc, srcWidth, srcHeight, 1, util::bitmap_vertical_direction::TopRowFirst)
				.to(pBuf, destWidth, destHeight, stride, util::bitmap_vertical_direction::TopRowFirst)
				.fore_color(fgColor)
				.fore_opacity(fgOpacity)
				.back_color(bgColor)
				.back_opacity(bgOpacity)
				.gamma_table(identity_gamma_table())
				.copy(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);
		});
	}

	return m_font->draw(codepoint, pBuf, stride, drawX + p.ShiftX, drawY, destWidth, destHeight, fgColor, bgColor, fgOpacity, bgOpacity);
}

bool FontChanger::FixedSizeFont::wrapping_fixed_size_font::draw(char32_t codepoint, util::b8g8r8a8* pBuf, int drawX, int drawY, int destWidth, int destHeight, util::b8g8r8a8 fgColor, util::b8g8r8a8 bgColor) const {
	codepoint = translate_codepoint(codepoint);

	glyph_metrics gm;
	if (!m_font->try_get_glyph_metrics(codepoint, gm))
		return false;

	const auto p = place(codepoint, gm);
	drawY += m_info->VerticalOffset;
	if (p.ScaledFont)
		return p.ScaledFont->draw(codepoint, pBuf, drawX + p.ShiftX, drawY, destWidth, destHeight, fgColor, bgColor);
	if (p.SqueezedWidth) {
		return draw_squeezed(codepoint, gm, p, drawX, drawY, [&](const uint8_t* pSrc, int srcWidth, int srcHeight, glyph_metrics src, glyph_metrics dest) {
			src.adjust_to_intersection(dest, srcWidth, srcHeight, destWidth, destHeight);
			if (src.is_effectively_empty() || dest.is_effectively_empty())
				return;

			util::bitmap_copy::to_b8g8r8a8()
				.from(pSrc, srcWidth, srcHeight, 1, util::bitmap_vertical_direction::TopRowFirst)
				.to(pBuf, destWidth, destHeight, util::bitmap_vertical_direction::TopRowFirst)
				.fore_color(fgColor)
				.back_color(bgColor)
				.gamma_table(identity_gamma_table())
				.copy(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);
		});
	}

	return m_font->draw(codepoint, pBuf, drawX + p.ShiftX, drawY, destWidth, destHeight, fgColor, bgColor);
}

const std::map<std::pair<char32_t, char32_t>, int>& FontChanger::FixedSizeFont::wrapping_fixed_size_font::all_kerning_pairs() const {
	if (m_kerningPairs)
		return *m_kerningPairs;

	std::map<util::unicode::blocks::negative_lsb_group, std::map<char32_t, int>> negativeLsbChars;

	std::map<char32_t, std::set<char32_t>> reverseMappedCodepoints;

	// Glyphs of the base font whose kerning pairs are dropped, as monospacing applies to them.
	std::set<char32_t> kerningDroppedGlyphs;

	for (const auto codepoint : m_info->Codepoints) {
		const auto mapped = translate_codepoint(codepoint);

		reverseMappedCodepoints[mapped].insert(codepoint);

		if (mapped < U' ')
			continue;

		// Measuring a glyph costs about as much as drawing it, so only the glyphs that the metrics matter for are measured:
		// those whose kerning monospacing may drop, and those whose negative bearings kerning may make up for.
		const auto& block = util::unicode::blocks::block_for(mapped);
		const auto mayCompensate = block.NegativeLsbGroup != util::unicode::blocks::None && !(block.Purpose & util::unicode::blocks::UsedWithCombining);
		if (!mayCompensate && !(m_info->Monospaced && m_info->DropKerning))
			continue;

		glyph_metrics gm;
		if (!m_font->try_get_glyph_metrics(mapped, gm))
			continue;

		const auto p = place(mapped, gm);
		if (p.Monospaced && m_info->DropKerning)
			kerningDroppedGlyphs.insert(mapped);

		if (p.NegativeLsbCompensation < 0 && mayCompensate)
			negativeLsbChars[block.NegativeLsbGroup][mapped] = p.NegativeLsbCompensation;
	}

	// Kerning follows the glyphs actually drawn: a codepoint replaced with another takes the kerning of the replacement.
	m_kerningPairs.emplace();
	for (const auto& [pair, value] : m_font->all_kerning_pairs()) {
		if (kerningDroppedGlyphs.contains(pair.first) || kerningDroppedGlyphs.contains(pair.second))
			continue;

		const auto left = reverseMappedCodepoints.find(pair.first);
		const auto right = reverseMappedCodepoints.find(pair.second);
		if (left == reverseMappedCodepoints.end() || right == reverseMappedCodepoints.end())
			continue;

		for (const auto leftUnmapped : left->second)
			for (const auto rightUnmapped : right->second)
				(*m_kerningPairs)[std::make_pair(leftUnmapped, rightUnmapped)] = value;
	}
#pragma warning(push)
#pragma warning(disable: 26812)
	for (const auto& [group, chars] : negativeLsbChars) {
		for (const auto& [rightc, offset] : chars) {
			if (!reverseMappedCodepoints.contains(rightc))
				continue;

			for (const auto& block : util::unicode::blocks::all_blocks()) {
				if (block.NegativeLsbGroup != group && (group != util::unicode::blocks::Combining || !(block.Purpose & util::unicode::blocks::UsedWithCombining)))
					continue;
#pragma warning(pop)
				for (auto leftc = block.First; leftc <= block.Last; leftc++) {
					if (!reverseMappedCodepoints.contains(leftc))
						continue;

					for (const auto leftUnmapped : reverseMappedCodepoints.at(leftc))
						for (const auto rightUnmapped : reverseMappedCodepoints.at(rightc))
							(*m_kerningPairs)[std::make_pair(leftUnmapped, rightUnmapped)] += offset;
				}
			}
		}
	}

	for (auto it = m_kerningPairs->begin(); it != m_kerningPairs->end();) {
		if (m_info->Codepoints.contains(it->first.first) && m_info->Codepoints.contains(it->first.second) && it->second != 0)
			++it;
		else
			it = m_kerningPairs->erase(it);
	}

	return *m_kerningPairs;
}

char32_t FontChanger::FixedSizeFont::wrapping_fixed_size_font::uniqid_to_glyph(const void* pc) const {
	// Squeezed glyphs are identified by the items of m_info->Codepoints; see get_base_font_glyph_uniqid.
	if (!pc)
		return 0;
	if (const auto it = m_info->Codepoints.find(*static_cast<const char32_t*>(pc)); it != m_info->Codepoints.end() && &*it == pc)
		return *it;

	return m_font->uniqid_to_glyph(pc);
}

const void* FontChanger::FixedSizeFont::wrapping_fixed_size_font::get_base_font_glyph_uniqid(char32_t codepoint) const {
	codepoint = translate_codepoint(codepoint);

	const auto it = m_info->Codepoints.find(codepoint);
	if (it == m_info->Codepoints.end())
		return nullptr;

	if (glyph_metrics gm; m_info->Monospaced && m_info->HasMaxAdvance && m_font->try_get_glyph_metrics(codepoint, gm) && place(codepoint, gm).SqueezedWidth)
		return &*it;

	return m_font->get_base_font_glyph_uniqid(codepoint);
}

bool FontChanger::FixedSizeFont::wrapping_fixed_size_font::try_get_glyph_metrics(char32_t codepoint, glyph_metrics& gm) const {
	codepoint = translate_codepoint(codepoint);

	if (!m_font->try_get_glyph_metrics(codepoint, gm))
		return false;

	apply_placement(gm, place(codepoint, gm), m_info->VerticalOffset);
	return true;
}

bool FontChanger::FixedSizeFont::wrapping_fixed_size_font::try_get_glyph_outline(char32_t codepoint, glyph_outline& outline) const {
	codepoint = translate_codepoint(codepoint);

	glyph_metrics gm;
	if (!m_font->try_get_glyph_metrics(codepoint, gm))
		return false;

	const auto p = place(codepoint, gm);
	if (p.ScaledFont) {
		if (!p.ScaledFont->try_get_glyph_outline(codepoint, outline))
			return false;
	} else {
		if (!m_font->try_get_glyph_outline(codepoint, outline))
			return false;
		if (p.SqueezedWidth && gm.width() > 0) {
			const auto scaleX = static_cast<float>(p.SqueezedWidth) / static_cast<float>(gm.width());
			for (auto& pt : outline.Points)
				pt.X = static_cast<float>(gm.X1) + (pt.X - static_cast<float>(gm.X1)) * scaleX;
		}
	}
	outline.translate(static_cast<float>(p.ShiftX), static_cast<float>(m_info->VerticalOffset));
	return true;
}

const std::set<char32_t>& FontChanger::FixedSizeFont::wrapping_fixed_size_font::all_codepoints() const {
	return m_info->Codepoints;
}

std::optional<float> FontChanger::FixedSizeFont::wrapping_fixed_size_font::get_baseline(uint32_t baselineTag) const {
	return m_font->get_baseline(baselineTag);
}

int FontChanger::FixedSizeFont::wrapping_fixed_size_font::line_height() const {
	return m_font->line_height();
}

int FontChanger::FixedSizeFont::wrapping_fixed_size_font::ascent() const {
	return m_font->ascent();
}

float FontChanger::FixedSizeFont::wrapping_fixed_size_font::font_size() const {
	return m_font->font_size();
}

std::string FontChanger::FixedSizeFont::wrapping_fixed_size_font::subfamily_name() const {
	return m_font->subfamily_name();
}

std::string FontChanger::FixedSizeFont::wrapping_fixed_size_font::family_name() const {
	return m_font->family_name();
}

FontChanger::FixedSizeFont::wrapping_fixed_size_font::wrapping_fixed_size_font(std::shared_ptr<const fixed_size_font> font, const wrap_modifiers& wrapModifiers, scaled_font_factory scaledFontFactory)
	: m_font(std::move(font))
	, m_scaledFontFactory(std::move(scaledFontFactory))
	, m_scaledFontCache(std::make_shared<scaled_font_cache>()) {
	auto info = std::make_shared<struct info>();
	info->LetterSpacing = static_cast<int>(std::lround(wrapModifiers.LetterSpacing));
	info->HorizontalOffset = static_cast<int>(std::lround(wrapModifiers.HorizontalOffset));
	info->VerticalOffset = static_cast<int>(std::lround(wrapModifiers.VerticalOffset));

	if (const auto& monospacing = wrapModifiers.Monospacing; monospacing.is_enabled()) {
		if (const auto unitPixels = get_monospacing_unit_pixels(*m_font, monospacing.Unit, monospacing.ReferenceCharacter)) {
			const auto toPixels = [unitPixels = *unitPixels](float value) {
				return std::clamp(static_cast<int>(std::lround(value * unitPixels)), 0, MaxMonospacingAdvance);
			};
			info->Monospaced = true;
			if (monospacing.MinAdvance)
				info->MinAdvance = toPixels(*monospacing.MinAdvance);
			if (monospacing.MaxAdvance) {
				info->MaxAdvance = toPixels(*monospacing.MaxAdvance);
				info->HasMaxAdvance = true;
			}
			info->MinAdvance = (std::min)(info->MinAdvance, info->MaxAdvance);
			info->Alignment = monospacing.Alignment;
			info->DropKerning = monospacing.DropKerning;
		}
	}

	for (const auto& c : m_font->all_codepoints()) {
		auto found = false;
		for (const auto& [c1, c2] : wrapModifiers.Codepoints) {
			if (c1 <= c && c <= c2) {
				found = true;
				break;
			}
		}
		if (found) {
			info->Codepoints.insert(c);
			if (const auto it = wrapModifiers.CodepointReplacements.find(c); it != wrapModifiers.CodepointReplacements.end())
				info->MappedCodepoints[c] = it->second;
		}
	}

	m_info = std::move(info);
}

FontChanger::FixedSizeFont::wrapping_fixed_size_font::wrapping_fixed_size_font() = default;

FontChanger::FixedSizeFont::wrapping_fixed_size_font::wrapping_fixed_size_font(const wrapping_fixed_size_font& r) = default;

FontChanger::FixedSizeFont::wrapping_fixed_size_font::wrapping_fixed_size_font(wrapping_fixed_size_font&& r) noexcept = default;

FontChanger::FixedSizeFont::wrapping_fixed_size_font& FontChanger::FixedSizeFont::wrapping_fixed_size_font::operator=(const wrapping_fixed_size_font& r) = default;

FontChanger::FixedSizeFont::wrapping_fixed_size_font& FontChanger::FixedSizeFont::wrapping_fixed_size_font::operator=(wrapping_fixed_size_font&& r) noexcept = default;
