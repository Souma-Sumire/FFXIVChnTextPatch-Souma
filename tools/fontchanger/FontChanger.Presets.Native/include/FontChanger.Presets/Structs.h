#pragma once

#include <exception>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include <nlohmann/json.hpp>

#include "xivres/fontdata.h"
#include "FontChanger.FixedSizeFont/directwrite_fixed_size_font.h"
#include "FontChanger.FixedSizeFont/fontdata_fixed_size_font.h"
#include "FontChanger.FixedSizeFont/freetype_fixed_size_font.h"
#include "FontChanger.FixedSizeFont/glyph_merging_fixed_size_font.h"
#include "FontChanger.FixedSizeFont/image_fixed_size_font.h"
#include "FontChanger.FixedSizeFont/merged_fixed_size_font.h"
#include "FontChanger.FixedSizeFont/wrapping_fixed_size_font.h"

// The C++ twin of FontChanger.Presets/Preset.cs; ElementFonts.cpp makes the fonts that elements draw with.
namespace FontChanger::Structs {
	enum class ElementSourceEnum : uint8_t {
		Empty,
		Game,
		DirectWrite,
		FreeType,
		GlyphImages,
	};

	struct GlyphImagesStruct {
		// Absolute or relative to the project directory.
		std::string Path;

		// Override the files' attributes and font.json when set.
		std::optional<float> UnitsPerEm;
		std::optional<float> BaselineY;

		// In units of UnitsPerEm, overriding font.json when set.
		std::optional<float> Ascent;
		std::optional<float> LineHeight;

		FixedSizeFont::image_coverage_mode BitmapCoverage = FixedSizeFont::image_coverage_mode::Auto;

		// Stored in the configuration instead of the folder.
		struct EmbeddedGlyph {
			std::shared_ptr<const std::string> Svg;
			std::shared_ptr<const std::string> PngBase64;
		};
		std::optional<nlohmann::json> EmbeddedMetadata;
		std::map<char32_t, EmbeddedGlyph> Embedded;

		[[nodiscard]] bool IsEmbedded() const { return !Embedded.empty(); }
	};

	struct SynthesisStruct {
		// Makes the weights, styles and widths that the family lacks from its closest face.
		bool Allow = true;
	};

	struct LookupStruct {
		std::string FamilyName;
		DWRITE_FONT_WEIGHT Weight = DWRITE_FONT_WEIGHT_REGULAR;
		DWRITE_FONT_STRETCH Stretch = DWRITE_FONT_STRETCH_NORMAL;
		DWRITE_FONT_STYLE Style = DWRITE_FONT_STYLE_NORMAL;

		// Unset in files of earlier versions, whose matched font is drawn as is, keeping the simulations DirectWrite matched.
		std::optional<SynthesisStruct> Synthesis;

		// Values above 1 pick an alternate for features such as 'salt' and 'cv01'.
		std::map<DWRITE_FONT_FEATURE_TAG, uint32_t> Features;
		std::string LanguageCode;

		// Override the axis values of the font's instance.
		std::map<std::string, float> Variations;

		// Keyed in DWRITE_FONT_AXIS_TAG byte order.
		std::map<uint32_t, float> GetVariationAxisValues() const;
	};

	struct RendererSpecificStruct {
		FixedSizeFont::empty_fixed_size_font::create_struct Empty;
		FixedSizeFont::freetype_fixed_size_font::create_struct FreeType;
		FixedSizeFont::directwrite_fixed_size_font::create_struct DirectWrite;
		GlyphImagesStruct GlyphImages;
	};

	// On screen, where y grows downwards.
	struct TransformStruct {
		using matrix = FixedSizeFont::font_render_transformation_matrix;

		float ScaleX = 1.f;
		float ScaleY = 1.f;

		// Positive values lean the tops of glyphs right, as italics do.
		float SkewDegrees = 0.f;

		// Positive values rotate counterclockwise.
		float RotationDegrees = 0.f;

		// For column vectors: x' = M11 x + M12 y, y' = M21 x + M22 y.
		[[nodiscard]] matrix GetScreenMatrix() const;

		[[nodiscard]] static matrix Multiply(const matrix& a, const matrix& b);

		// Each conversion is its own inverse, so this also converts a renderer's matrix to the screen's.
		[[nodiscard]] static matrix ScreenToRenderer(ElementSourceEnum renderer, const matrix& screen);
	};

	struct GlyphMergingStruct {
		FixedSizeFont::glyph_merge_params Params;

		// Applied to the texts after the element's transformation.
		TransformStruct TextTransform;

		// With mappings, the element draws only the mapped codepoints.
		[[nodiscard]] bool IsEnabled() const { return !Params.Mappings.empty(); }
	};

	// Fields that the editor keeps fitted (FontsFromSystemFont::RefitAuto); their values are still stored, for readers that
	// don't fit fonts.
	struct AutoFieldsStruct {
		bool Size = false;
		bool HorizontalOffset = false;
		bool VerticalOffset = false;
		bool RenderMode = false;

		[[nodiscard]] bool Any() const { return Size || HorizontalOffset || VerticalOffset || RenderMode; }
	};

	class FontElement {
		mutable std::shared_ptr<FixedSizeFont::fixed_size_font> m_baseFont;
		mutable std::shared_ptr<FixedSizeFont::wrapping_fixed_size_font> m_wrappedFont;
		friend struct FontSet;

	public:
		float Size = 0.f;
		float Gamma = 1.f;
		FixedSizeFont::codepoint_merge_mode MergeMode = FixedSizeFont::codepoint_merge_mode::AddNew;
		TransformStruct Transform;
		FixedSizeFont::wrap_modifiers WrapModifiers;
		ElementSourceEnum Source = ElementSourceEnum::Empty;
		LookupStruct Lookup;
		RendererSpecificStruct RendererSpecific;
		GlyphMergingStruct GlyphMerging;
		AutoFieldsStruct Auto;

		const std::shared_ptr<FixedSizeFont::fixed_size_font>& GetBaseFont() const;
		const std::shared_ptr<FixedSizeFont::wrapping_fixed_size_font>& GetWrappedFont() const;
		void OnFontWrappingParametersChange();
		void OnFontCreateParametersChange();

		void Scale(float factor);

		// Equal for elements that can share a base font.
		std::string GetBaseFontKey() const;

		[[nodiscard]] bool UsesProjectDirectory() const;
	};

	class Font {
		mutable std::shared_ptr<FixedSizeFont::fixed_size_font> MergedFont;

	public:
		std::string Name;
		std::string PreviewText;
		std::vector<std::unique_ptr<FontElement>> Elements;
		FixedSizeFont::vertical_alignment VerticalAlignment = FixedSizeFont::vertical_alignment::Baseline;

		Font() noexcept = default;
		Font(Font&& r) noexcept = default;
		Font(const Font& r);
		Font& operator=(Font&& r) noexcept = default;
		Font& operator=(const Font& r);

		const std::shared_ptr<FixedSizeFont::fixed_size_font>& GetMergedFont() const;
		void FlushCache();
		void OnElementChange();
	};

	struct FontSet {
		std::string TexFilenameFormat;
		int DiscardStep = 1;
		int SideLength = 4096;
		int ExpectedTexCount = 1;
		std::vector<std::unique_ptr<Font>> Fonts;

		void FlushCache();

		void ConsolidateFonts() const;

		static FontSet NewFromTemplateFont(xivres::font_type fontType);
	};

	struct MultiFontSet {
		std::vector<std::unique_ptr<FontSet>> FontSets;
		bool ExportMapFontLobbyToFont = true;
		bool ExportMapChnAxisToFont = true;
		bool ExportMapKrnAxisToFont = true;
		bool ExportMapTcAxisToFont = true;

		void FlushCache();
	};

	void to_json(nlohmann::json& json, const LookupStruct& value);

	void from_json(const nlohmann::json& json, LookupStruct& value);

	void to_json(nlohmann::json& json, const RendererSpecificStruct& value);

	void from_json(const nlohmann::json& json, RendererSpecificStruct& value);

	void to_json(nlohmann::json& json, const FontElement& value);

	void from_json(const nlohmann::json& json, FontElement& value);

	void to_json(nlohmann::json& json, const TransformStruct& value);

	void from_json(const nlohmann::json& json, TransformStruct& value);

	void to_json(nlohmann::json& json, const GlyphMergingStruct& value);

	void from_json(const nlohmann::json& json, GlyphMergingStruct& value);

	void to_json(nlohmann::json& json, const Font& value);

	void from_json(const nlohmann::json& json, Font& value);

	void to_json(nlohmann::json& json, const FontSet& value);

	void from_json(const nlohmann::json& json, FontSet& value);

	void to_json(nlohmann::json& json, const MultiFontSet& value);

	void from_json(const nlohmann::json& json, MultiFontSet& value);

	const char* GetDefaultPreviewText();
}

namespace FontChanger::FixedSizeFont {
	void to_json(nlohmann::json& json, const wrap_modifiers& value);

	void from_json(const nlohmann::json& json, wrap_modifiers& value);
}
