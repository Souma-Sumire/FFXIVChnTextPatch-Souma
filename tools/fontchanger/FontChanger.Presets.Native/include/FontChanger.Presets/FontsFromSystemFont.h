#pragma once

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "FontChanger.Presets/CodepointRanges.h"
#include "FontChanger.Presets/Structs.h"

// The C# twin is FontChanger.Presets/FontsFromSystemFont.cs.
namespace FontChanger::FontsFromSystemFont {
	enum class FontScripts : uint32_t {
		None = 0,
		NonCjk = 1,
		Han = 2,
		Kana = 4,
		Hangul = 8,

		// The private-use icons and merged glyphs of AXIS fonts.
		GameSymbols = 16,

		All = NonCjk | Han | Kana | Hangul | GameSymbols,
	};

	[[nodiscard]] constexpr bool HasScripts(FontScripts scripts, FontScripts flag) {
		return (static_cast<uint32_t>(scripts) & static_cast<uint32_t>(flag)) == static_cast<uint32_t>(flag);
	}

	enum class StrokeWeight {
		Lighter,
		Normal,
		Heavier,
		Heaviest,
	};

	// Sizes are fractions larger than fitted and the rest fractions of the font's size, so they look alike at all sizes of
	// a family; the C# twin is FontChanger.Presets.Records.FontAdjustments.
	struct FontAdjustments {
		FontScripts Scripts = FontScripts::All;
		float NonCjkSize = 0;
		float CjkSize = 0;

		// Positive moves the glyphs up.
		float Raise = 0;

		float NonCjkSpacing = 0;
		float CjkSpacing = 0;
		FontsFromSystemFont::StrokeWeight StrokeWeight = FontsFromSystemFont::StrokeWeight::Normal;
	};

	// In ems; Top and Bottom are above the baseline.
	struct GlyphBox {
		float Left;
		float Right;
		float Advance;
		float Top;
		float Bottom;
	};

	// Metrics of a font in ems.
	class FontProbe {
	public:
		virtual ~FontProbe() = default;

		[[nodiscard]] virtual std::optional<GlyphBox> GetGlyph(char32_t codepoint) const = 0;

		[[nodiscard]] virtual bool HasBitmapOf(int ppem) const { return false; }

		// Whether DirectWrite recommends natural over natural symmetric rendering at a size, as the gasp table asks.
		[[nodiscard]] virtual bool PrefersNatural(float ppem) const { return false; }
	};

	class FontInfo final : public FontProbe {
		IDWriteFontFacePtr m_face;
		float m_unitsPerEm = 1;
		std::set<int> m_bitmapSizes;

	public:
		explicit FontInfo(IDWriteFont* font);

		static std::optional<FontInfo> Open(const std::string& name, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch, DWRITE_FONT_STYLE style);

		[[nodiscard]] std::optional<GlyphBox> GetGlyph(char32_t codepoint) const override;
		[[nodiscard]] bool HasBitmapOf(int ppem) const override;
		[[nodiscard]] bool PrefersNatural(float ppem) const override;
	};

	// Returns nullptr for fonts that aren't installed.
	using FontOpener = std::function<const FontInfo*(const Structs::LookupStruct& lookup)>;

	// The DWRITE_RENDERING_MODE1 that fonts made here use at a size, for fonts drawn at sizes not fitted here, such as the
	// system's fallback fonts.
	DWRITE_RENDERING_MODE1 GetRenderMode(const FontProbe& font, float size);

	// Jupiter_45 and Jupiter_90, which only have digits and a few symbols, are of the family JupiterN.
	std::optional<std::pair<std::string_view, float>> GetGameFontFamilyAndSize(std::string_view fontName);

	// The first element's size, which presets may make larger than the game font's (96 px for 36 px fonts), else the game
	// font's.
	std::optional<float> GetGameFontSize(const Structs::Font& font);

	// By the family's name in DirectWrite, as GDI names some faces as families of their own ("Franklin Gothic Medium").
	Structs::LookupStruct GetLookupFromLogFont(const LOGFONTW& logFont);

	[[nodiscard]] bool CanAutoFit(const Structs::FontElement& element);

	struct AutoFitResult {
		// Elements that drew more than one script group, now gone from the font, where copies drawing one group each took
		// their place.
		std::set<const Structs::FontElement*> Replaced;

		std::vector<std::pair<Structs::FontElement*, CodepointRanges::ScriptGroup>> Fitted;
	};

	// Elements that CanAutoFit rejects are left as they are, those that draw more than one script group are split, and the
	// fields fitted are marked Auto for RefitAuto to keep fitted.
	AutoFitResult AutoFit(Structs::Font& font, std::string_view family, float size, const std::vector<Structs::FontElement*>& targets);

	// Nullopt if the font is none of the game's.
	std::optional<AutoFitResult> AutoFit(Structs::Font& font, const std::vector<Structs::FontElement*>& targets);

	// Unlike AutoFit, elements aren't split, and an element whose size isn't Auto has its offset and render mode fitted to
	// its own size; returns whether any field changed.
	bool RefitAuto(Structs::Font& font);

	// The element draws what the other elements don't, over the game's glyphs but its symbols, and is fitted (and so split)
	// for fonts of game font families.
	std::vector<Structs::FontElement*> AddElement(Structs::Font& font, const Structs::LookupStruct& chosen, size_t index);

	// With monospacedDigits, the digits are tabular figures (tnum) where text is shaped, and unless all are as wide by
	// default, they are put in cells as wide as the 0, as glyphs drawn one by one get no features.
	std::vector<Structs::Font*> AddFamilyElements(
		const Structs::FontSet& fontSet, std::string_view family, const Structs::LookupStruct& chosen, bool monospacedDigits = true);

	// gameFont, whose glyphs need no textures, gives the line and the glyphs that stay the game's; fontsAbove are the fonts
	// of the same name drawn over this one, top first, as ProbeFont measures them. Null if the font isn't installed or draws
	// nothing.
	std::unique_ptr<Structs::Font> MakeFont(
		std::string_view fontName, float size, const Structs::LookupStruct& chosen, const FontOpener& open, bool monospacedDigits,
		const FixedSizeFont::fixed_size_font* gameFont, const std::vector<const FontProbe*>& fontsAbove, const FontAdjustments& adjustments);

	// In ems of the first element's size, from the system font elements only; the font and the fonts that open returns must
	// outlive the probe.
	std::unique_ptr<FontProbe> ProbeFont(const Structs::Font& font, const FontOpener& open);

	// Fits a system fallback font as an item under the font's own with no adjustments, rather than at the size DirectWrite
	// would draw it; size is the one MakeFont is given for the game font, drawn is ProbeFont of the font, and of the
	// elements, the last that has a glyph draws it. The C# twin is FontsFromSystemFont.MakeFallbackElements.
	std::vector<std::unique_ptr<Structs::FontElement>> MakeFallbackElements(
		const Structs::Font& font, float size, const Structs::LookupStruct& fallback, const FontOpener& open, const FontProbe& drawn,
		const FixedSizeFont::fixed_size_font* gameFont);
}
