// ReSharper disable CppClangTidyClangDiagnosticLanguageExtensionToken
#ifndef XIVRES_FONTGENERATOR_DIRECTWRITEFIXEDSIZEFONT_H_
#define XIVRES_FONTGENERATOR_DIRECTWRITEFIXEDSIZEFONT_H_

#include <comdef.h>
#include <dwrite_3.h>
#include <filesystem>
#include "fixed_size_font.h"
#include "opentype_positioning.h"
#include "util.truetype.h"
#include "xivres/util.bitmap_copy.h"

#pragma comment(lib, "dwrite.lib")

_COM_SMARTPTR_TYPEDEF(IDWriteFactory, __uuidof(IDWriteFactory));
_COM_SMARTPTR_TYPEDEF(IDWriteFactory2, __uuidof(IDWriteFactory2));
_COM_SMARTPTR_TYPEDEF(IDWriteFactory3, __uuidof(IDWriteFactory3));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFallback, __uuidof(IDWriteFontFallback));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFallbackBuilder, __uuidof(IDWriteFontFallbackBuilder));
_COM_SMARTPTR_TYPEDEF(IDWriteTextLayout2, __uuidof(IDWriteTextLayout2));
_COM_SMARTPTR_TYPEDEF(IDWriteFont, __uuidof(IDWriteFont));
_COM_SMARTPTR_TYPEDEF(IDWriteFontCollection, __uuidof(IDWriteFontCollection));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFace, __uuidof(IDWriteFontFace));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFace1, __uuidof(IDWriteFontFace1));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFace3, __uuidof(IDWriteFontFace3));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFace5, __uuidof(IDWriteFontFace5));
_COM_SMARTPTR_TYPEDEF(IDWriteFontResource, __uuidof(IDWriteFontResource));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFaceReference, __uuidof(IDWriteFontFaceReference));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFamily, __uuidof(IDWriteFontFamily));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFile, __uuidof(IDWriteFontFile));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFileLoader, __uuidof(IDWriteFontFileLoader));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFileStream, __uuidof(IDWriteFontFileStream));
_COM_SMARTPTR_TYPEDEF(IDWriteTextFormat, __uuidof(IDWriteTextFormat));
_COM_SMARTPTR_TYPEDEF(IDWriteTextFormat3, __uuidof(IDWriteTextFormat3));
_COM_SMARTPTR_TYPEDEF(IDWriteTextLayout, __uuidof(IDWriteTextLayout));
_COM_SMARTPTR_TYPEDEF(IDWriteFontSetBuilder, __uuidof(IDWriteFontSetBuilder));
_COM_SMARTPTR_TYPEDEF(IDWriteGdiInterop, __uuidof(IDWriteGdiInterop));
_COM_SMARTPTR_TYPEDEF(IDWriteGlyphRunAnalysis, __uuidof(IDWriteGlyphRunAnalysis));
_COM_SMARTPTR_TYPEDEF(IDWriteLocalizedStrings, __uuidof(IDWriteLocalizedStrings));
_COM_SMARTPTR_TYPEDEF(IDWriteTextAnalyzer, __uuidof(IDWriteTextAnalyzer));
_COM_SMARTPTR_TYPEDEF(IDWriteTextAnalyzer2, __uuidof(IDWriteTextAnalyzer2));
_COM_SMARTPTR_TYPEDEF(IDWriteTypography, __uuidof(IDWriteTypography));

typedef struct hb_face_t hb_face_t;

namespace FontChanger::FixedSizeFont {
	// Creates a HarfBuzz face that reads the tables of the DirectWrite font face, keeping a reference to it.
	hb_face_t* create_harfbuzz_face(IDWriteFontFace* pFace);

	class directwrite_fixed_size_font : public default_abstract_fixed_size_font {
	public:
		struct create_struct {
			DWRITE_RENDERING_MODE1 RenderMode = DWRITE_RENDERING_MODE1_NATURAL;
			DWRITE_MEASURING_MODE MeasureMode = DWRITE_MEASURING_MODE_GDI_CLASSIC;
			DWRITE_GRID_FIT_MODE GridFitMode = DWRITE_GRID_FIT_MODE_ENABLED;
			std::vector<DWRITE_FONT_FEATURE> Features{};

			// BCP 47 language tag of the text, such as "ja" or "zh-Hant"; empty if unspecified.
			std::string LanguageCode;

			// Design coordinates of variable fonts, keyed by axis tags in DWRITE_FONT_AXIS_TAG byte order.
			// Axes not listed stay at the instance of the font; 'opsz' follows the font size unless listed.
			std::map<uint32_t, float> Variations;

			// Simulations of the face, such as the bold simulation for a font that has no bold face. If unset, those of
			// the font are used, which DirectWrite picks when it matches a font in a family for requested properties.
			std::optional<DWRITE_FONT_SIMULATIONS> Simulations;

			[[nodiscard]] const wchar_t* get_measuring_mode_string() const;

			[[nodiscard]] const wchar_t* get_rendering_mode_string() const;

			[[nodiscard]] const wchar_t* get_grid_fit_mode_string() const;
		};

	private:
		struct info {
			IDWriteFactoryPtr Factory;
			IDWriteFontPtr Font;
			std::shared_ptr<xivres::stream> Stream;
			std::set<char32_t> Characters;
			std::map<std::pair<char32_t, char32_t>, int> KerningPairs;
			std::unordered_map<uint16_t, glyph_adjustment> GlyphAdjustments;
			std::map<uint32_t, float> Baselines;
			std::vector<uint8_t> GammaTable;
			DWRITE_FONT_METRICS1 Metrics;
			DWRITE_MATRIX Matrix;
			create_struct Params;
			int FontIndex = 0;
			float Size = 0.f;

			template<decltype(std::roundf) TIntCastFn = std::roundf, typename T>
			[[nodiscard]] int scale_from_font_unit(T fontUnitValue) const {
				return static_cast<int>(TIntCastFn(static_cast<float>(fontUnitValue) * Size / static_cast<float>(Metrics.designUnitsPerEm)));
			}
		};

		struct dwrite_interfaces {
			IDWriteFactoryPtr Factory;
			IDWriteFactory3Ptr Factory3;
			IDWriteFontCollectionPtr Collection;
			IDWriteFontFamilyPtr Family;
			IDWriteFontPtr Font;
			IDWriteFontFacePtr Face;
			IDWriteFontFace1Ptr Face1;
			IDWriteTextFormatPtr Format;
			IDWriteTypographyPtr Typography;
		};

		dwrite_interfaces m_dwrite;
		std::shared_ptr<const info> m_info;
		mutable std::vector<uint8_t> m_drawBuffer;

	public:
		directwrite_fixed_size_font();
		directwrite_fixed_size_font(std::filesystem::path path, int fontIndex, float size, float gamma, const font_render_transformation_matrix& matrix, const create_struct& params);
		directwrite_fixed_size_font(IDWriteFactoryPtr factory, IDWriteFontPtr font, float size, float gamma, const font_render_transformation_matrix& matrix, const create_struct& params);
		directwrite_fixed_size_font(std::shared_ptr<xivres::stream> strm, int fontIndex, float size, float gamma, const font_render_transformation_matrix& matrix, const create_struct& params);
		directwrite_fixed_size_font(directwrite_fixed_size_font&&) noexcept;
		directwrite_fixed_size_font& operator=(directwrite_fixed_size_font&&) noexcept;
		directwrite_fixed_size_font(const directwrite_fixed_size_font& r);
		directwrite_fixed_size_font& operator=(const directwrite_fixed_size_font& r);
		~directwrite_fixed_size_font() override = default;

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

		[[nodiscard]] std::optional<shaped_line> shape_line(std::u32string_view text, int letterSpacing) const override;

		[[nodiscard]] bool try_get_glyph_index_metrics(uint32_t glyphIndex, float originX, float originY, glyph_metrics& gm) const override;

		[[nodiscard]] bool try_get_glyph_index_ink_extent(uint32_t glyphIndex, float& x1, float& x2) const override;

		bool draw_glyph_index(uint32_t glyphIndex, uint8_t* pBuf, size_t stride, float drawX, float drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override;

		[[nodiscard]] bool try_get_glyph_outline(char32_t codepoint, glyph_outline& outline) const override;

		[[nodiscard]] bool try_get_glyph_index_outline(uint32_t glyphIndex, float originX, float originY, glyph_outline& outline) const override;

	private:
		// Gets the pixels that a glyph covers with its origin on the baseline at (originX, originY), without the
		// adjustments of the features.
		[[nodiscard]] bool try_get_glyph_index_metrics(uint16_t glyphIndex, glyph_metrics& gm, IDWriteGlyphRunAnalysisPtr& analysis, float originX = 0, float originY = 0) const;

		[[nodiscard]] static dwrite_interfaces face_from_info_t(const info& info);

		static void load_font_data(info& info, const dwrite_interfaces& dwrite);

		// Returns the glyph that DirectWrite lays out for the codepoint with the selected features, or 0 if none.
		[[nodiscard]] static uint16_t resolve_glyph_index(const dwrite_interfaces& dwrite, char32_t codepoint);

		[[nodiscard]] bool try_get_glyph_metrics(char32_t codepoint, glyph_metrics& gm, IDWriteGlyphRunAnalysisPtr& analysis, glyph_adjustment& adjustment) const;
	};
}

#endif
