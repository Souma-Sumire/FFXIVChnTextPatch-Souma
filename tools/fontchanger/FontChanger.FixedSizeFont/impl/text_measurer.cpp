#include "../include/FontChanger.FixedSizeFont/text_measurer.h"

using namespace xivres;

std::shared_ptr<xivres::texture::memory_mipmap_stream> FontChanger::FixedSizeFont::text_measure_result::create_mipmap(const fixed_size_font& fontFace, util::b8g8r8a8 fgColor, util::b8g8r8a8 bgColor, int pad) const {
	auto res = std::make_shared<texture::memory_mipmap_stream>(
		pad * 2 + Occupied.X2 - (std::min)(0, Occupied.X1),
		pad * 2 + Occupied.Y2 - (std::min)(0, Occupied.Y1),
		1,
		texture::formats::B8G8R8A8);
	std::ranges::fill(res->as_span<util::b8g8r8a8>(), bgColor);
	draw_to(
		*res,
		fontFace,
		pad - (std::min)(0, Occupied.X1),
		pad - (std::min)(0, Occupied.Y1),
		fgColor,
		bgColor);
	return res;
}

void FontChanger::FixedSizeFont::text_measure_result::draw_to(texture::memory_mipmap_stream& mipmapStream, const fixed_size_font& fontFace, int x, int y, util::b8g8r8a8 fgColor, util::b8g8r8a8 bgColor) const {
	const auto buf = mipmapStream.as_span<util::b8g8r8a8>();
	for (const auto& c : Characters) {
		if (c.Metrics.is_effectively_empty())
			continue;

		fontFace.draw(
			c.Displayed,
			&buf[0],
			x + c.X, y + c.Y,
			mipmapStream.Width, mipmapStream.Height,
			fgColor,
			{});
	}
}

FontChanger::FixedSizeFont::text_measure_result& FontChanger::FixedSizeFont::text_measurer::measure(text_measure_result& res) const {
	std::vector<size_t> lineBreakIndices;

	if (res.Characters.empty())
		return res;

	for (auto& curr : res.Characters) {
		if (curr.Codepoint < IsCharacterControlCharacter.size() && IsCharacterControlCharacter[curr.Codepoint])
			continue;

		if (!FontFace.try_get_glyph_metrics(curr.Displayed, curr.Metrics)) {
			for (auto pfc = FallbackCharacters; (curr.Displayed = *pfc); pfc++) {
				if (FontFace.try_get_glyph_metrics(curr.Displayed, curr.Metrics))
					break;
			}
		}
	}

	size_t lastBreakIndex = 0;
	for (size_t i = 1; i < res.Characters.size(); i++) {
		auto& prev = res.Characters[i - 1];
		auto& curr = res.Characters[i];

		if (prev.Codepoint == '\n') {
			lineBreakIndices.push_back(i);
			curr.X = 0;
			curr.Y = prev.Y + LineHeight.value_or(FontFace.line_height());
		} else {
			curr.X = prev.X + (UseKerning ? FontFace.get_adjusted_advance_width(prev.Displayed, curr.Displayed) : prev.Metrics.AdvanceX);
			curr.Y = prev.Y;
		}

		if (prev.Codepoint < IsCharacterWordBreakPoint.size() && IsCharacterWordBreakPoint[prev.Codepoint])
			lastBreakIndex = i;

		if (curr.Codepoint < IsCharacterWhiteSpace.size() && IsCharacterWhiteSpace[curr.Codepoint])
			continue;

		if (curr.X + curr.Metrics.X2 < MaxWidth)
			continue;

		if (!(prev.Codepoint < IsCharacterWhiteSpace.size() && IsCharacterWhiteSpace[prev.Codepoint]) && res.Characters[lastBreakIndex].X > 0)
			i = lastBreakIndex;
		else
			lastBreakIndex = i;
		res.Characters[i].X = 0;
		res.Characters[i].Y = res.Characters[i - 1].Y + LineHeight.value_or(FontFace.line_height());
		lineBreakIndices.push_back(i);
	}

	for (auto& elem : res.Characters) {
		elem.Metrics.translate(elem.X, elem.Y);
		res.Occupied.expand_to_fit(elem.Metrics);
	}

	return res;
}

FontChanger::FixedSizeFont::text_measurer& FontChanger::FixedSizeFont::text_measurer::fallback_characters(char32_t* fallbackCharacters) {
	FallbackCharacters = fallbackCharacters;
	return *this;
}

FontChanger::FixedSizeFont::text_measurer& FontChanger::FixedSizeFont::text_measurer::line_height(std::optional<int> lineHeight) {
	LineHeight = lineHeight;
	return *this;
}

FontChanger::FixedSizeFont::text_measurer& FontChanger::FixedSizeFont::text_measurer::max_height(int height) {
	MaxHeight = height;
	return *this;
}

FontChanger::FixedSizeFont::text_measurer& FontChanger::FixedSizeFont::text_measurer::max_width(int width) {
	MaxWidth = width;
	return *this;
}

FontChanger::FixedSizeFont::text_measurer& FontChanger::FixedSizeFont::text_measurer::use_kerning(bool use) {
	UseKerning = use;
	return *this;
}

FontChanger::FixedSizeFont::text_measurer::text_measurer(const fixed_size_font& fontFace) : FontFace(fontFace) {
	IsCharacterWhiteSpace.resize(256);
	IsCharacterWhiteSpace[U' '] = true;
	IsCharacterWhiteSpace[U'\r'] = true;
	IsCharacterWhiteSpace[U'\n'] = true;
	IsCharacterWhiteSpace[U'\t'] = true;

	IsCharacterWordBreakPoint.resize(256);
	IsCharacterWordBreakPoint[U' '] = true;
	IsCharacterWordBreakPoint[U'\r'] = true;
	IsCharacterWordBreakPoint[U'\n'] = true;
	IsCharacterWordBreakPoint[U'\t'] = true;

	IsCharacterControlCharacter.resize(0x200d);
	IsCharacterControlCharacter[U'\r'] = true;
	IsCharacterControlCharacter[U'\n'] = true;
	IsCharacterControlCharacter[U'\t'] = true;
	IsCharacterControlCharacter[U'\x200c'] = true;  // Zero-width non joiner
}
