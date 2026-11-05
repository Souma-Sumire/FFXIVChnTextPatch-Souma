#ifndef XIVRES_FONTGENERATOR_CODEPOINTLIMITINGFIXEDSIZEFONT_H_
#define XIVRES_FONTGENERATOR_CODEPOINTLIMITINGFIXEDSIZEFONT_H_

#include <functional>
#include <limits>
#include <mutex>

#include "fixed_size_font.h"

namespace FontChanger::FixedSizeFont {
	enum class monospacing_unit : uint8_t {
		Pixels,

		// Fraction of the font size.
		Em,

		// Multiple of the advance width of the reference character in the font.
		ReferenceCharacter,
	};

	enum class monospacing_alignment : uint8_t {
		// Keeps the left side bearing.
		Left,

		// Splits the width added to or removed from the advance evenly between both sides.
		CenterAdvance,

		// Centers the bounding box of the glyph in the cell.
		CenterInk,

		// Keeps the right side bearing.
		Right,
	};

	// Limits the advance widths of the glyphs into a range, placing each glyph in a cell of the resulting width.
	struct monospacing {
		std::optional<float> MinAdvance;
		std::optional<float> MaxAdvance;
		monospacing_unit Unit = monospacing_unit::Em;
		char32_t ReferenceCharacter = U'0';
		monospacing_alignment Alignment = monospacing_alignment::CenterAdvance;

		// Drops the kerning pairs that involve any glyph that the monospacing applies to.
		bool DropKerning = true;

		[[nodiscard]] bool is_enabled() const { return MinAdvance || MaxAdvance; }

		bool operator==(const monospacing&) const = default;
	};

	struct wrap_modifiers {
		std::vector<std::pair<char32_t, char32_t>> Codepoints;
		std::map<char32_t, char32_t> CodepointReplacements;

		// In pixels, rounded when the font is made, so that scaling them back and forth keeps them; VerticalOffset moves glyphs down.
		float LetterSpacing = 0.f;
		float HorizontalOffset = 0.f;
		float VerticalOffset = 0.f;
		monospacing Monospacing;
	};

	class wrapping_fixed_size_font : public default_abstract_fixed_size_font {
		struct info {
			std::set<char32_t> Codepoints;
			std::map<char32_t, char32_t> MappedCodepoints;
			int LetterSpacing = 0;
			int HorizontalOffset = 0;
			int VerticalOffset = 0;

			// Limits of the advance widths in pixels, resolved from wrap_modifiers::Monospacing.
			bool Monospaced = false;
			int MinAdvance = 0;
			int MaxAdvance = (std::numeric_limits<int>::max)();
			bool HasMaxAdvance = false;
			monospacing_alignment Alignment = monospacing_alignment::CenterAdvance;
			bool DropKerning = true;
		};

		// Where a glyph of the base font goes in this font.
		struct placement {
			// Horizontal distance by which the bounding box of the glyph moves.
			int ShiftX = 0;

			int AdvanceX = 0;

			// If not zero, the glyph is squeezed horizontally to fit in this width.
			int SqueezedWidth = 0;

			// Font that draws the squeezed glyph narrower, and the metrics of the glyph in it, which ShiftX moves; if null,
			// the bounding box of the glyph of the base font is squeezed by resampling, starting at X1 + ShiftX.
			const fixed_size_font* ScaledFont = nullptr;
			glyph_metrics ScaledMetrics;

			// Negative distance that the advance has grown by to keep a glyph with negative left side bearing from
			// starting left of the pen; kerning pairs compensate for it.
			int NegativeLsbCompensation = 0;

			bool Monospaced = false;
		};

	public:
		// Creates the base font again with its glyphs scaled horizontally by scaleX, to draw squeezed glyphs with.
		using scaled_font_factory = std::function<std::shared_ptr<fixed_size_font>(float scaleX)>;

	private:
		// Fonts made by the factory and the scales chosen for glyphs, shared by the thread safe views so that they all choose
		// alike and the factory is not called from many threads at once. The fonts here are only used under the mutex;
		// each view draws with thread safe views of them.
		struct scaled_font_cache {
			std::mutex Mutex;

			// Keyed by the horizontal scale in units of 1 / ScaleSteps; null where the factory failed.
			std::map<int, std::shared_ptr<fixed_size_font>> Fonts;

			// Scale chosen for each squeezed glyph of the base font, or 0 if the glyph is resampled instead.
			std::map<char32_t, int> GlyphScales;
		};

		// Each scale is a font of its own, which may hold a copy of the font file, so scales are kept coarse.
		static constexpr int ScaleSteps = 32;

		std::shared_ptr<const fixed_size_font> m_font;
		std::shared_ptr<const info> m_info;
		scaled_font_factory m_scaledFontFactory;
		std::shared_ptr<scaled_font_cache> m_scaledFontCache;

		// Thread safe views of the fonts of m_scaledFontCache that this font draws with.
		mutable std::map<int, std::shared_ptr<fixed_size_font>> m_scaledFonts;

		mutable std::optional<std::map<std::pair<char32_t, char32_t>, int>> m_kerningPairs;

	public:
		wrapping_fixed_size_font(std::shared_ptr<const fixed_size_font> font, const wrap_modifiers& wrapModifiers, scaled_font_factory scaledFontFactory = {});

		wrapping_fixed_size_font();
		wrapping_fixed_size_font(const wrapping_fixed_size_font& r);
		wrapping_fixed_size_font(wrapping_fixed_size_font&& r) noexcept;
		wrapping_fixed_size_font& operator=(const wrapping_fixed_size_font& r);
		wrapping_fixed_size_font& operator=(wrapping_fixed_size_font&& r) noexcept;
		~wrapping_fixed_size_font() override = default;

		[[nodiscard]] std::string family_name() const override;

		[[nodiscard]] std::string subfamily_name() const override;

		[[nodiscard]] float font_size() const override;

		[[nodiscard]] int ascent() const override;

		[[nodiscard]] int line_height() const override;

		[[nodiscard]] const std::set<char32_t>& all_codepoints() const override;

		[[nodiscard]] bool try_get_glyph_metrics(char32_t codepoint, glyph_metrics& gm) const override;

		[[nodiscard]] const void* get_base_font_glyph_uniqid(char32_t codepoint) const override;

		[[nodiscard]] char32_t uniqid_to_glyph(const void* pc) const override;

		[[nodiscard]] const std::map<std::pair<char32_t, char32_t>, int>& all_kerning_pairs() const override;

		bool draw(char32_t codepoint, xivres::util::b8g8r8a8* pBuf, int drawX, int drawY, int destWidth, int destHeight, xivres::util::b8g8r8a8 fgColor, xivres::util::b8g8r8a8 bgColor) const override;

		bool draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override;

		[[nodiscard]] std::shared_ptr<fixed_size_font> get_threadsafe_view() const override;

		[[nodiscard]] const fixed_size_font* get_base_font(char32_t codepoint) const override;

		[[nodiscard]] std::optional<float> get_baseline(uint32_t baselineTag) const override;

		[[nodiscard]] bool try_get_glyph_outline(char32_t codepoint, glyph_outline& outline) const override;

		[[nodiscard]] std::vector<std::pair<char32_t, int>> get_negative_lsb_codepoints() const;

		// Returns the number of pixels in one unit of monospacing widths for the font, or nothing if the font lacks the
		// reference character.
		[[nodiscard]] static std::optional<float> get_monospacing_unit_pixels(const fixed_size_font& font, monospacing_unit unit, char32_t referenceCharacter);

		// Largest advance width that monospacing may set, as the font data stores the difference between the advance
		// width and the bounding width of a glyph in a signed byte.
		static constexpr int MaxMonospacingAdvance = 127;

	private:
		[[nodiscard]] char32_t translate_codepoint(char32_t codepoint) const;

		// Places the glyph of the base font for a translated codepoint, of which gm holds the metrics.
		[[nodiscard]] placement place(char32_t codepoint, const glyph_metrics& gm) const;

		// Finds the font that draws a glyph of the base font no wider than width, and the metrics of the glyph in it.
		[[nodiscard]] const fixed_size_font* find_scaled_glyph(char32_t codepoint, const glyph_metrics& gm, int width, glyph_metrics& scaledGm) const;

		// Applies a placement to the metrics of a glyph of the base font.
		static void apply_placement(glyph_metrics& gm, const placement& p, int verticalOffset);

		// Draws a squeezed glyph through a buffer of coverage; draw is called with the buffer and returns its results.
		template<typename TDraw>
		bool draw_squeezed(char32_t codepoint, const glyph_metrics& gm, const placement& p, int drawX, int drawY, TDraw&& draw) const;
	};
}

#endif
