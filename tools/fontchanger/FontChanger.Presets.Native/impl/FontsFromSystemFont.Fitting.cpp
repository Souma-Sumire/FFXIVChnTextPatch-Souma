#include "pch.h"
#include "FontsFromSystemFont.Internal.h"

DWRITE_RENDERING_MODE1 FontChanger::FontsFromSystemFont::GetRenderMode(const FontProbe& font, float size) {
	return Internal::GetSizeAndMode(font, size).second;
}

namespace FontChanger::FontsFromSystemFont::Internal {
	std::optional<InkRange> MeasureInk(const FontProbe& font, std::u32string_view samples) {
		std::vector<float> tops, bottoms;
		for (const auto c : samples) {
			if (const auto glyph = font.GetGlyph(c); glyph && glyph->Top > glyph->Bottom) {
				tops.push_back(glyph->Top);
				bottoms.push_back(glyph->Bottom);
			}
		}
		if (tops.empty() || tops.size() * 2 < samples.size())
			return std::nullopt;

		const auto median = [](std::vector<float>& v) {
			std::ranges::sort(v);
			return (v[v.size() / 2] + v[(v.size() - 1) / 2]) / 2;
		};
		return InkRange{median(tops), median(bottoms)};
	}

	// Below the CJK blocks, the game draws some symbols an em wide as Japanese fonts do, such as arrows and circled digits;
	// the game's font may be drawn at a size other than the one made.
	Ranges GetEmWideSymbols(const FontProbe& font, const FontChanger::FixedSizeFont::fixed_size_font* gameFont) {
		if (!gameFont)
			return {};

		std::set<char32_t> res;
		for (const auto c : gameFont->all_codepoints()) {
			const auto isSymbol = (c >= 0x2000 && c < 0x2E80) || (c >= 0xA0 && c <= 0xBF) || c == 0xD7 || c == 0xF7;
			if (!isSymbol)
				continue;

			FontChanger::FixedSizeFont::glyph_metrics gm;
			if (!gameFont->try_get_glyph_metrics(c, gm) || gm.AdvanceX < gameFont->font_size() * 0.9f)
				continue;

			if (const auto glyph = font.GetGlyph(c); glyph && glyph->Advance >= 0.9f)
				res.insert(c);
		}
		return FontChanger::CodepointRanges::FromCodepoints(res);
	}

	bool HasText(const FontProbe& font, std::u32string_view text) {
		// A leading "_" is the underline of an IME indicator's box, not text.
		if (text.starts_with(U'_'))
			text.remove_prefix(1);
		return std::ranges::all_of(text, [&font](char32_t c) { return c == U'\n' || font.GetGlyph(c); });
	}

	// Tabular figures (tnum) don't count, as features only apply to shaped text, which glyphs drawn one by one aren't.
	bool HasTabularDigits(const FontProbe& font) {
		const auto zero = font.GetGlyph(U'0');
		for (auto c = U'0'; c <= U'9'; c++) {
			const auto glyph = font.GetGlyph(c);
			if (!zero || !glyph || glyph->Advance != zero->Advance)  // NOLINT(clang-diagnostic-float-equal)
				return false;
		}
		return true;
	}

	// Natural symmetric downsampled trades quality for speed, which glyphs rendered once don't need, and outline is drawn as
	// natural symmetric, so neither is used.
	std::pair<float, DWRITE_RENDERING_MODE1> GetSizeAndMode(const FontProbe& font, float exactSize) {
		if (const auto rounded = std::round(exactSize); rounded > 0 && font.HasBitmapOf(static_cast<int>(rounded)))
			return {rounded, DWRITE_RENDERING_MODE1_NATURAL};
		const auto size = std::round(exactSize * 10) / 10;
		return {size, font.PrefersNatural(size) ? DWRITE_RENDERING_MODE1_NATURAL : DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC};
	}

	GroupPlacement FitInk(const FontProbe& font, const InkRange& ink, float targetTop, float targetBottom, float latinSize, float horizontalOffset) {
		// The clamp keeps a script whose samples are drawn unusually near the Latin letters' size.
		const auto exactSize = std::clamp((targetTop - targetBottom) / (ink.Top - ink.Bottom), latinSize * 0.8f, latinSize * 1.25f);
		const auto [size, renderMode] = GetSizeAndMode(font, exactSize);
		const auto up = (targetTop + targetBottom) / 2 - size * (ink.Top + ink.Bottom) / 2;
		return {size, renderMode, horizontalOffset, std::round(-up)};
	}

	GroupPlacement KeepWithinLine(const FontProbe& font, const InkRange& ink, const GroupPlacement& placement, const std::optional<LineMetrics>& line, float latinSize) {
		if (!line || line->Descent <= 0)
			return placement;

		const auto top = ink.Top * placement.Size - placement.VerticalOffset;
		const auto bottom = ink.Bottom * placement.Size - placement.VerticalOffset;
		if (top <= line->Ascent && bottom >= -line->Descent)
			return placement;
		return FitInk(font, ink, (std::min)(top, line->Ascent), (std::max)(bottom, -line->Descent), latinSize, placement.HorizontalOffset);
	}

	bool IsLeftToGame(const FontProbe& font, const FontProbe* baseFont, const FontChanger::FixedSizeFont::fixed_size_font* gameFont, const CjkGroup& group) {
		if (!gameFont)
			return false;

		size_t total = 0, left = 0;
		for (const auto c : gameFont->all_codepoints()) {
			if (!FontChanger::CodepointRanges::Contains(group.Codepoints, c))
				continue;
			total++;
			if (!font.GetGlyph(c) && (!baseFont || !baseFont->GetGlyph(c)))
				left++;
		}
		return left * 20 > total;
	}

	std::vector<std::pair<ScriptGroup, Ranges>> SplitByScriptGroup(const FontProbe& face, const Ranges& codepoints, const FontChanger::FixedSizeFont::fixed_size_font* gameFont, bool hasText) {
		std::vector<std::pair<ScriptGroup, Ranges>> parts;
		if (!hasText) {
			parts.emplace_back(ScriptGroup::NonCjk, codepoints);
			return parts;
		}

		// Symbols that the game and the font both draw an em wide go with the Han characters.
		const auto emWideSymbols = MeasureInk(face, CjkGroup::Of(ScriptGroup::Han)->Samples)
			? FontChanger::CodepointRanges::Intersect(GetEmWideSymbols(face, gameFont), codepoints)
			: Ranges{};

		// Groups that the font lacks stay with the rest.
		auto rest = FontChanger::CodepointRanges::Subtract(codepoints, emWideSymbols);
		for (const auto& group : CjkGroups) {
			if (!MeasureInk(face, group.Samples))
				continue;
			auto part = FontChanger::CodepointRanges::Intersect(codepoints, group.Codepoints);
			if (group.Group == ScriptGroup::Han) {
				part.insert(part.end(), emWideSymbols.begin(), emWideSymbols.end());
				part = FontChanger::CodepointRanges::Subtract(part, {});
			}
			rest = FontChanger::CodepointRanges::Subtract(rest, part);
			if (!part.empty())
				parts.emplace_back(group.Group, std::move(part));
		}
		if (!rest.empty())
			parts.insert(parts.begin(), {ScriptGroup::NonCjk, std::move(rest)});
		return parts;
	}

	// Least-squares fits the cap and x-heights to the game's, measured from the capitals' bottom or the baseline if they
	// reach below it, so that letters drawn above the baseline move onto it.
	LatinPlacement FitLatin(const FontProbe& font, const Traits& traits, float size) {
		const auto targetCapHeight = size * traits.ReferenceHeight;
		auto exactSize = size;
		auto latinBottom = 0.f;
		if (const auto caps = MeasureInk(font, traits.ReferenceCharacters)) {
			latinBottom = (std::max)(0.f, caps->Bottom);
			const auto capHeight = caps->Top - latinBottom;
			auto numerator = capHeight * targetCapHeight;
			auto denominator = capHeight * capHeight;

			// Lowercase letters as tall as the capitals are capitals or small capitals, which the game's are not.
			if (const auto x = traits.XHeightPerCapHeight ? MeasureInk(font, U"xzvw") : std::nullopt) {
				if (const auto xHeight = x->Top - latinBottom; xHeight > 0 && xHeight < capHeight * 0.9f) {
					numerator += xHeight * targetCapHeight * *traits.XHeightPerCapHeight;
					denominator += xHeight * xHeight;
				}
			}
			if (capHeight > 0)
				exactSize = numerator / denominator;
		}
		const auto [fontSize, renderMode] = GetSizeAndMode(font, exactSize);
		return {fontSize, renderMode, std::round(latinBottom * fontSize)};
	}

	LatinPlacement ScaleLatin(const FontProbe& font, const LatinPlacement& latin, float by) {
		if (by == 0 || latin.Size <= 0)
			return latin;
		const auto [size, renderMode] = GetSizeAndMode(font, latin.Size * (1 + by));
		return {size, renderMode, std::round(latin.VerticalOffset * size / latin.Size)};
	}

	GroupPlacement ScaleAroundInk(const FontProbe& font, const InkRange& ink, const GroupPlacement& placement, float by) {
		if (by == 0)
			return placement;
		const auto [size, renderMode] = GetSizeAndMode(font, placement.Size * (1 + by));
		const auto center = (ink.Top + ink.Bottom) / 2;
		return {size, renderMode, placement.HorizontalOffset, std::round(placement.VerticalOffset + center * (size - placement.Size))};
	}

	ExclamationMarkPlacement PlaceExclamationMark(const MarkTraits& traits, const FontProbe& digits, const FontProbe& mark, float fontSize, float size, float latinOffset) {
		const auto [markSize, renderMode] = GetSizeAndMode(mark, fontSize * traits.SizePerDigitSize);
		auto markOffset = std::round(traits.OffsetPerSize * size);

		auto digitRight = std::numeric_limits<float>::max();
		for (auto c = U'0'; c <= U'9'; c++) {
			if (const auto glyph = digits.GetGlyph(c))
				digitRight = (std::min)(digitRight, glyph->Right);
		}
		digitRight = digitRight == std::numeric_limits<float>::max() ? 0 : digitRight * fontSize;
		const auto glyph = mark.GetGlyph(U'!').value_or(FontChanger::FontsFromSystemFont::GlyphBox{});

		// A gap too wide moves the digits right, within a third of an em, rather than the mark left, as glyphs may not
		// start left of their pens.
		const auto excess = markOffset + glyph.Left * markSize + digitRight - latinOffset - traits.GapPerSize * size;
		const auto digitOffset = latinOffset + std::round(std::clamp(excess, 0.f, size / 3));
		if (excess < 0)
			markOffset -= std::round(excess);

		const auto inkWidth = (glyph.Advance - glyph.Left - glyph.Right) * markSize;
		return {
			.DigitOffset = digitOffset,
			.Size = markSize,
			.RenderMode = renderMode,
			.Offset = markOffset,
			.VerticalOffset = std::round(traits.VerticalOffsetPerSize * size),
			.LetterSpacing = std::round(traits.PitchPerInk * inkWidth - glyph.Advance * markSize),
		};
	}
}
