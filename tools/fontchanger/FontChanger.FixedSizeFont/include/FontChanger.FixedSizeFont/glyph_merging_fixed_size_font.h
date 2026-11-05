#ifndef XIVRES_FONTGENERATOR_GLYPHMERGINGFIXEDSIZEFONT_H_
#define XIVRES_FONTGENERATOR_GLYPHMERGINGFIXEDSIZEFONT_H_

#include <array>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fixed_size_font.h"

namespace FontChanger::FixedSizeFont {
	enum class glyph_merge_shape : uint8_t {
		None,
		AmPm,
		Ime,
		Box,

		NumberBox,

		HollowBox,
		Hexagon,
		Rhombus,
		Bozja,
		Time,
		Star,
		Custom,

		Glyph,
	};

	enum class glyph_merge_text_mode : uint8_t {
		Subtract,
		Difference,
	};

	enum class glyph_merge_fit_mode : uint8_t {
		CondenseThenShrink,
		Shrink,
		Overflow,
	};

	enum class glyph_merge_line_alignment : uint8_t {
		Left,
		Center,
		Right,
	};

	// The shapes by their names in presets and in data/glyph_merge_shapes.json.
	inline constexpr std::pair<glyph_merge_shape, const char*> glyph_merge_shape_names[]{
		{glyph_merge_shape::None, "none"},
		{glyph_merge_shape::AmPm, "amPm"},
		{glyph_merge_shape::Ime, "ime"},
		{glyph_merge_shape::Box, "box"},
		{glyph_merge_shape::NumberBox, "numberBox"},
		{glyph_merge_shape::HollowBox, "hollowBox"},
		{glyph_merge_shape::Hexagon, "hexagon"},
		{glyph_merge_shape::Rhombus, "rhombus"},
		{glyph_merge_shape::Bozja, "bozja"},
		{glyph_merge_shape::Time, "time"},
		{glyph_merge_shape::Star, "star"},
		{glyph_merge_shape::Custom, "custom"},
		{glyph_merge_shape::Glyph, "glyph"},
	};

	struct glyph_merge_mapping {
		std::u32string Codepoints;
		std::vector<std::u32string> Texts;
		glyph_merge_shape Shape = glyph_merge_shape::Box;
		glyph_merge_text_mode TextMode = glyph_merge_text_mode::Subtract;
		std::string CustomPath;
		float CustomAdvance = 1000.f;
		std::string CustomSvg;
		std::optional<std::array<float, 4>> CustomTextArea;
	};

	struct glyph_merge_params {
		std::optional<float> TextSize;
		glyph_merge_fit_mode FitMode = glyph_merge_fit_mode::CondenseThenShrink;
		float TextOffsetX = 0.f;
		float TextOffsetY = 0.f;
		float LetterSpacing = 0.f;
		float LineSpacing = 0.f;
		glyph_merge_line_alignment LineAlignment = glyph_merge_line_alignment::Center;
		std::vector<glyph_merge_mapping> Mappings;
	};

	class glyph_merging_fixed_size_font : public default_abstract_fixed_size_font {
	public:
		using text_font_factory = std::function<std::shared_ptr<fixed_size_font>(float size, float condense)>;

	private:
		struct info {
			std::shared_ptr<const fixed_size_font> BaseFont;
			glyph_merge_params Params;
			text_font_factory TextFontFactory;
			std::set<char32_t> Codepoints;
			std::map<char32_t, std::pair<size_t, size_t>> MappingIndices;
		};

		struct rendered_glyph {
			glyph_metrics Metrics;
			std::vector<uint8_t> Alpha;
		};

		struct glyph_cache {
			std::mutex Mutex;
			std::map<char32_t, std::shared_ptr<const rendered_glyph>> Glyphs;
		};

		std::shared_ptr<const info> m_info;
		std::shared_ptr<glyph_cache> m_glyphs = std::make_shared<glyph_cache>();
		mutable std::map<std::pair<int, int>, std::shared_ptr<fixed_size_font>> m_textFonts;

	public:
		glyph_merging_fixed_size_font(std::shared_ptr<const fixed_size_font> baseFont, glyph_merge_params params, text_font_factory textFontFactory);

		glyph_merging_fixed_size_font();
		glyph_merging_fixed_size_font(const glyph_merging_fixed_size_font& r);
		glyph_merging_fixed_size_font(glyph_merging_fixed_size_font&& r) noexcept;
		glyph_merging_fixed_size_font& operator=(const glyph_merging_fixed_size_font& r);
		glyph_merging_fixed_size_font& operator=(glyph_merging_fixed_size_font&& r) noexcept;
		~glyph_merging_fixed_size_font() override = default;

		[[nodiscard]] std::string family_name() const override;

		[[nodiscard]] std::string subfamily_name() const override;

		[[nodiscard]] float font_size() const override;

		[[nodiscard]] int ascent() const override;

		[[nodiscard]] int line_height() const override;

		[[nodiscard]] const std::set<char32_t>& all_codepoints() const override;

		[[nodiscard]] bool try_get_glyph_metrics(char32_t codepoint, glyph_metrics& gm) const override;

		[[nodiscard]] const std::map<std::pair<char32_t, char32_t>, int>& all_kerning_pairs() const override;

		bool draw(char32_t codepoint, xivres::util::b8g8r8a8* pBuf, int drawX, int drawY, int destWidth, int destHeight, xivres::util::b8g8r8a8 fgColor, xivres::util::b8g8r8a8 bgColor) const override;

		bool draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override;

		[[nodiscard]] std::shared_ptr<fixed_size_font> get_threadsafe_view() const override;

		[[nodiscard]] const fixed_size_font* get_base_font(char32_t codepoint) const override;

		[[nodiscard]] std::optional<float> get_baseline(uint32_t baselineTag) const override;

		[[nodiscard]] bool try_get_glyph_outline(char32_t codepoint, glyph_outline& outline) const override;

		[[nodiscard]] static float get_shape_advance(glyph_merge_shape shape);

		[[nodiscard]] std::optional<std::string> get_shape_path_data(char32_t codepoint) const;

	private:
		struct arrangement;

		[[nodiscard]] std::shared_ptr<const rendered_glyph> get_rendered_glyph(char32_t codepoint) const;

		[[nodiscard]] arrangement arrange(char32_t codepoint, const glyph_merge_mapping& mapping, const std::u32string& text) const;

		[[nodiscard]] rendered_glyph render(char32_t codepoint, const glyph_merge_mapping& mapping, const std::u32string& text) const;

		[[nodiscard]] const fixed_size_font& get_text_font(float size, float condense) const;
	};

	// The glyphs of the game's private use area drawn by glyph merging, in groups as the font editor offers them
	// (data/glyph_merge_shapes.json): the AM/PM marks, the digits and levels, the IME indicators (texts beginning with "_"
	// are of the underlined boxes), and so on.
	[[nodiscard]] const std::vector<std::vector<glyph_merge_mapping>>& get_glyph_merge_presets();
}

#endif
