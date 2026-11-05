#ifndef XIVRES_FONTGENERATOR_TEXTMEASURER_H_
#define XIVRES_FONTGENERATOR_TEXTMEASURER_H_

#include "fixed_size_font.h"
#include "xivres/util.unicode.h"

namespace FontChanger::FixedSizeFont {
	struct text_measure_result {
		struct character_info {
			char32_t Codepoint;
			char32_t Displayed;
			int X;
			int Y;
			glyph_metrics Metrics;
		};

		glyph_metrics Occupied;
		std::vector<character_info> Characters;

		void draw_to(xivres::texture::memory_mipmap_stream& mipmapStream, const fixed_size_font& fontFace, int x, int y, xivres::util::b8g8r8a8 fgColor, xivres::util::b8g8r8a8 bgColor) const;

		[[nodiscard]] std::shared_ptr<xivres::texture::memory_mipmap_stream> create_mipmap(const fixed_size_font& fontFace, xivres::util::b8g8r8a8 fgColor, xivres::util::b8g8r8a8 bgColor, int pad = 0) const;
	};

	struct text_measurer {
		const fixed_size_font& FontFace;
		int MaxWidth = (std::numeric_limits<int>::max)();
		int MaxHeight = (std::numeric_limits<int>::max)();
		bool UseKerning = true;
		std::optional<int> LineHeight = std::nullopt;
		const char32_t* FallbackCharacters = U"\u3013-?";
		std::vector<bool> IsCharacterWhiteSpace;
		std::vector<bool> IsCharacterWordBreakPoint;
		std::vector<bool> IsCharacterControlCharacter;

		text_measurer(const fixed_size_font& fontFace);

		text_measurer& use_kerning(bool use);

		text_measurer& max_width(int width = (std::numeric_limits<int>::max)());

		text_measurer& max_height(int height = (std::numeric_limits<int>::max)());

		text_measurer& line_height(std::optional<int> lineHeight = std::nullopt);

		text_measurer& fallback_characters(char32_t* fallbackCharacters);

		template <class TStringElem, class TStringTraits = std::char_traits<TStringElem>, class TStringAlloc = std::allocator<TStringElem>>
		[[nodiscard]] text_measure_result measure(const std::basic_string<TStringElem, TStringTraits, TStringAlloc>& pcszString) const {
			return measure(&pcszString[0], pcszString.size());
		}

		template <class TStringElem, class TStringTraits = std::char_traits<TStringElem>>
		[[nodiscard]] text_measure_result measure(const std::basic_string_view<TStringElem, TStringTraits>& pcszString) const {
			return measure(&pcszString[0], pcszString.size());
		}

		template<typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
		[[nodiscard]] text_measure_result measure(const T* pcszString, size_t nLength = (std::numeric_limits<size_t>::max)()) const {
			if (nLength == (std::numeric_limits<size_t>::max)())
				nLength = std::char_traits<T>::length(pcszString);

			text_measure_result res{};
			res.Characters.reserve(nLength);
			for (auto pc = pcszString, pc_ = pc + nLength; pc < pc_; pc++) {
				char32_t c = *pc;
				if (c == '\r') {
					if (pc + 1 < pc_ && *(pc + 1) == '\n')
						continue;
					c = '\n';
				}

				pc += xivres::util::unicode::decode(c, pc, pc_ - pc) - 1;
				res.Characters.emplace_back(c, c, 0, 0, glyph_metrics{});
			}

			return measure(res);
		}

	private:
		[[nodiscard]] text_measure_result& measure(text_measure_result& res) const;
	};
}

#endif
