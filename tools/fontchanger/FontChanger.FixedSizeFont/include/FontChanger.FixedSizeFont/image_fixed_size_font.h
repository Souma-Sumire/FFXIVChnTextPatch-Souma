#ifndef XIVRES_FONTGENERATOR_IMAGEFIXEDSIZEFONT_H_
#define XIVRES_FONTGENERATOR_IMAGEFIXEDSIZEFONT_H_

#include <array>
#include <memory>
#include <mutex>
#include <span>

#include "fixed_size_font.h"

namespace FontChanger::FixedSizeFont {
	// How the pixels of a bitmap glyph become coverage.
	enum class image_coverage_mode : uint8_t {
		// Alpha, if any pixel is not opaque; darkness otherwise.
		Auto,
		Alpha,
		Darkness,
		Brightness,
	};

	// A glyph drawn from an SVG document, or from the pixels of a bitmap.
	struct image_glyph_source {
		// The whole SVG file, if the glyph is drawn from one.
		std::shared_ptr<const std::string> Svg;

		// 32-bit BGRA pixels with straight alpha, if the glyph is drawn from a bitmap.
		std::shared_ptr<const std::vector<uint32_t>> Pixels;
		int Width = 0;
		int Height = 0;

		// For bitmaps: the advance in pixels of the bitmap, if not its width, and where the origin on the top of the line
		// is in the bitmap.
		std::optional<float> Advance;
		int OriginX = 0;
		int OriginY = 0;
	};

	// Draws glyphs from SVG documents and bitmaps, with Direct2D, without hinting.
	//
	// SVG documents are in units of UnitsPerEm per em, with y growing downwards and the baseline at y = BaselineY, unless
	// their root has data-xivfont-units-per-em and data-xivfont-baseline attributes. The origin is at x = 0, and the
	// advance is data-xivfont-advance, the width of the viewBox, or the width, in this order. A group with the id
	// xivfont-reference is not drawn. Pixels become coverage by their opacity, less their luminance, so that white
	// shapes over black ones cut holes as they look.
	//
	// Bitmaps are in pixels, with BitmapUnitsPerEm pixels per em, and the baseline at BitmapBaselineY pixels below the
	// top of the line.
	class image_fixed_size_font : public default_abstract_fixed_size_font {
	public:
		struct create_struct {
			std::map<char32_t, image_glyph_source> Glyphs;

			// Units of SVG documents, which their attributes override unless told otherwise.
			float UnitsPerEm = 1000.f;
			float BaselineY = 880.f;
			bool UnitsPerEmOverridesAttribute = false;
			bool BaselineYOverridesAttribute = false;

			// Units of bitmaps.
			float BitmapUnitsPerEm = 1000.f;
			float BitmapBaselineY = 880.f;

			// Ascent and line height of the font in units of UnitsPerEm.
			float Ascent = 880.f;
			float LineHeight = 1000.f;

			// Kerning in units of UnitsPerEm.
			std::map<std::pair<char32_t, char32_t>, float> KerningPairs;

			image_coverage_mode BitmapCoverage = image_coverage_mode::Auto;

			std::string FamilyName;
		};

	private:
		struct rendered_glyph {
			glyph_metrics Metrics;
			std::vector<uint8_t> Alpha;
		};

		struct info {
			create_struct Params;
			std::set<char32_t> Codepoints;
			std::map<std::pair<char32_t, char32_t>, int> KerningPairs;
			std::vector<uint8_t> GammaTable;

			// Transformation on screen, for column vectors.
			font_render_transformation_matrix Matrix{};
			float Size = 0.f;
			int Ascent = 0;
			int LineHeight = 0;

			// Shared by the views of the font for other threads, as drawing a glyph takes a while.
			mutable std::mutex GlyphsMtx;
			mutable std::map<char32_t, std::shared_ptr<const rendered_glyph>> Glyphs;
		};

		std::shared_ptr<const info> m_info;

	public:
		// matrix is the transformation on screen, for column vectors: x' = M11 x + M12 y, y' = M21 x + M22 y.
		image_fixed_size_font(create_struct params, float size, float gamma, const font_render_transformation_matrix& matrix);

		image_fixed_size_font();
		image_fixed_size_font(const image_fixed_size_font& r);
		image_fixed_size_font(image_fixed_size_font&& r) noexcept;
		image_fixed_size_font& operator=(const image_fixed_size_font& r);
		image_fixed_size_font& operator=(image_fixed_size_font&& r) noexcept;
		~image_fixed_size_font() override = default;

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

		// Reads the root attributes of an SVG document that decide its metrics; returns false if it cannot be read.
		struct svg_metrics {
			std::optional<float> UnitsPerEm;
			std::optional<float> BaselineY;
			std::optional<float> Advance;
		};
		[[nodiscard]] static bool try_read_svg_metrics(std::string_view svg, svg_metrics& metrics);

		// Draws an SVG document into a buffer of coverage of width by height pixels, with user units mapped to pixels by
		// x' = m[0] x + m[1] y + m[2] and y' = m[3] x + m[4] y + m[5]. Coverage is taken as the class describes.
		static void draw_svg_coverage(std::string_view svg, const std::array<float, 6>& m, int width, int height, std::span<uint8_t> coverage);

	private:
		[[nodiscard]] std::shared_ptr<const rendered_glyph> get_rendered_glyph(char32_t codepoint) const;

		[[nodiscard]] rendered_glyph render(const image_glyph_source& source) const;
	};
}

#endif
