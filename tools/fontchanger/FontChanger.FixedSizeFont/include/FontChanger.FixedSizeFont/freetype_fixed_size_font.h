#ifndef XIVRES_FONTGENERATOR_FREETYPEFIXEDSIZEFONT_H_
#define XIVRES_FONTGENERATOR_FREETYPEFIXEDSIZEFONT_H_

#ifndef FT2BUILD_H_
#include <ft2build.h>
#endif

#include FT_FREETYPE_H
#include FT_GLYPH_H

#include <harfbuzz/hb.h>

#include <filesystem>
#include <mutex>

#include "fixed_size_font.h"
#include "opentype_positioning.h"

#include "util.truetype.h"

namespace FontChanger::FixedSizeFont {
	class freetype_fixed_size_font : public default_abstract_fixed_size_font {
		using library_ptr_t = std::unique_ptr<std::remove_pointer_t<FT_Library>, decltype(&FT_Done_FreeType)>;

	public:
		struct create_struct {
			int LoadFlags = FT_LOAD_DEFAULT;
			FT_Render_Mode RenderMode = FT_RENDER_MODE_LIGHT;
			std::vector<hb_feature_t> Features;

			// BCP 47 language tag of the text, such as "ja" or "zh-Hant"; empty if unspecified.
			std::string LanguageCode;

			// Design coordinates of variable fonts, keyed by axis tags in DWRITE_FONT_AXIS_TAG byte order.
			// Axes not listed stay at the instance of the face index; 'opsz' follows the font size unless listed.
			std::map<uint32_t, float> Variations;

			// Emboldening in ems, applied to the outlines before the transformation; negative values make glyphs thinner.
			// As with the bold simulation of DirectWrite, glyphs grow rightwards and upwards by this much, and advance
			// further by as much. Embedded bitmaps are not used while emboldening, as they cannot be emboldened alike.
			float Embolden = 0.f;

			[[nodiscard]] bool requires_shaping() const;

			[[nodiscard]] std::wstring get_load_flags_string() const;

			[[nodiscard]] std::wstring get_render_mode_string() const;
		};

	private:
		class freetype_face_wrapper {
			struct info {
				std::vector<uint8_t> Data;
				std::set<char32_t> Characters;
				std::map<std::pair<char32_t, char32_t>, int> KerningPairs;
				std::unordered_map<uint16_t, glyph_adjustment> GlyphAdjustments;
				std::map<uint32_t, float> Baselines;
				std::vector<uint8_t> GammaTable;
				FT_Matrix Matrix;
				create_struct Params{};
				int LoadFlags = FT_LOAD_DEFAULT;

				// Emboldening in 26.6 fixed point pixels.
				FT_Pos EmboldenStrength = 0;
				int FaceIndex = 0;
				float Size = 0.f;
			};

			library_ptr_t m_library;
			std::shared_ptr<const info> m_info;
			FT_Face m_face{};
			hb_font_t* m_hbFont = nullptr;

		public:
			freetype_face_wrapper();
			freetype_face_wrapper(std::vector<uint8_t> data, int faceIndex, float size, float gamma, const font_render_transformation_matrix& matrix, create_struct createStruct);
			freetype_face_wrapper(freetype_face_wrapper&& r) noexcept;
			freetype_face_wrapper(const freetype_face_wrapper& r);
			freetype_face_wrapper& operator=(freetype_face_wrapper&& r) noexcept;
			freetype_face_wrapper& operator=(const freetype_face_wrapper& r);
			freetype_face_wrapper& operator=(const std::nullptr_t&);
			~freetype_face_wrapper();

			FT_Face operator*() const;

			FT_Face operator->() const;

			[[nodiscard]] int get_char_index(char32_t codepoint) const;

			[[nodiscard]] std::unique_ptr<std::remove_pointer_t<FT_Glyph>, decltype(&FT_Done_Glyph)> load_glyph(uint32_t glyphIndex, bool render) const;

			// Loads the outline of a glyph without hinting, transformed by the matrix.
			[[nodiscard]] std::unique_ptr<std::remove_pointer_t<FT_Glyph>, decltype(&FT_Done_Glyph)> load_unhinted_glyph(uint32_t glyphIndex) const;

			[[nodiscard]] FT_Library library() const;

			[[nodiscard]] float font_size() const;

			[[nodiscard]] const FT_Matrix& matrix() const;

			[[nodiscard]] FT_Render_Mode render_mode() const;

			[[nodiscard]] std::span<const uint8_t> gamma_table() const;

			[[nodiscard]] const std::set<char32_t>& all_characters() const;

			[[nodiscard]] const std::map<std::pair<char32_t, char32_t>, int>& all_kerning_pairs() const;

			[[nodiscard]] glyph_adjustment get_glyph_adjustment(uint32_t glyphIndex) const;

			[[nodiscard]] std::optional<float> get_baseline(uint32_t baselineTag) const;

			// Shapes with the OpenType functions of HarfBuzz, which leave the glyph slot of the face alone.
			[[nodiscard]] shaped_line shape_line(std::u32string_view text, int letterSpacing) const;

		private:
			static FT_Face create_face(FT_Library library, const info& info);

			static int get_load_flags(FT_Face face, const create_struct& params);

			static int resolve_glyph_index(FT_Face face, hb_font_t* hbFont, char32_t codepoint, const create_struct& params);

			static std::map<uint32_t, float> get_design_coordinates(FT_Library library, FT_Face face);
		};

		freetype_face_wrapper m_face;

	public:
		freetype_fixed_size_font();
		freetype_fixed_size_font(const std::filesystem::path& path, int faceIndex, float size, float gamma, const font_render_transformation_matrix& matrix, create_struct createStruct);
		freetype_fixed_size_font(xivres::stream& strm, int faceIndex, float fSize, float gamma, const font_render_transformation_matrix& matrix, create_struct createStruct);
		freetype_fixed_size_font(std::vector<uint8_t> data, int faceIndex, float fSize, float gamma, const font_render_transformation_matrix& matrix, create_struct createStruct);
		freetype_fixed_size_font(freetype_fixed_size_font&& r) noexcept;
		freetype_fixed_size_font(const freetype_fixed_size_font& r);
		freetype_fixed_size_font& operator=(freetype_fixed_size_font&& r) noexcept;
		freetype_fixed_size_font& operator=(const freetype_fixed_size_font& r);
		~freetype_fixed_size_font() override = default;

		[[nodiscard]] std::string family_name() const override;

		[[nodiscard]] std::string subfamily_name() const override;

		[[nodiscard]] float font_size() const override;

		[[nodiscard]] int ascent() const override;

		[[nodiscard]] int line_height() const override;

		[[nodiscard]] const std::set<char32_t>& all_codepoints() const override;

		[[nodiscard]] bool try_get_glyph_metrics(char32_t codepoint, glyph_metrics& gm) const override;

		[[nodiscard]] const std::map<std::pair<char32_t, char32_t>, int>& all_kerning_pairs() const override;

		[[nodiscard]] int get_adjusted_advance_width(char32_t left, char32_t right) const override;

		bool draw(char32_t codepoint, xivres::util::b8g8r8a8* pBuf, int drawX, int drawY, int destWidth, int destHeight, xivres::util::b8g8r8a8 fgColor, xivres::util::b8g8r8a8 bgColor) const override;

		bool draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override;

		[[nodiscard]] std::shared_ptr<fixed_size_font> get_threadsafe_view() const override;

		[[nodiscard]] const fixed_size_font* get_base_font(char32_t codepoint) const override;

		[[nodiscard]] std::optional<float> get_baseline(uint32_t baselineTag) const override;

		[[nodiscard]] std::optional<shaped_line> shape_line(std::u32string_view text, int letterSpacing) const override;

		[[nodiscard]] bool try_get_glyph_index_metrics(uint32_t glyphIndex, float originX, float originY, glyph_metrics& gm) const override;

		[[nodiscard]] bool try_get_glyph_index_ink_extent(uint32_t glyphIndex, float& x1, float& x2) const override;

		bool draw_glyph_index(uint32_t glyphIndex, uint8_t* pBuf, size_t stride, float drawX, float drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override;

		[[nodiscard]] bool try_get_glyph_outline(char32_t codepoint, glyph_outline& outline) const override;

		[[nodiscard]] bool try_get_glyph_index_outline(uint32_t glyphIndex, float originX, float originY, glyph_outline& outline) const override;

	private:
		using glyph_ptr_t = std::unique_ptr<std::remove_pointer_t<FT_Glyph>, decltype(&FT_Done_Glyph)>;

		[[nodiscard]] glyph_metrics freetype_glyph_to_metrics(uint32_t glyphIndex, FT_Glyph glyph, int x = 0, int y = 0) const;

		// Loads a glyph moved by the fractional part of its origin, and returns the integral part of the origin.
		[[nodiscard]] glyph_ptr_t load_positioned_glyph(uint32_t glyphIndex, float originX, float originY, bool render, int& x, int& y) const;

		// Metrics relative to the origin on the baseline at (x, y), without the adjustments of the features.
		[[nodiscard]] glyph_metrics freetype_glyph_to_shaped_metrics(uint32_t glyphIndex, FT_Glyph glyph, int x = 0, int y = 0) const;
	};
}

#endif
