#include "pch.h"
#include "FontsFromSystemFont.Internal.h"

#include "FontChanger.Presets/DirectWriteUtil.h"
#include "FontChanger.Presets/GlyphMergingPresets.h"

using namespace FontChanger::FontsFromSystemFont::Internal;
using FontChanger::FontsFromSystemFont::FontInfo;
using FontChanger::FontsFromSystemFont::FontOpener;
using FontChanger::FontsFromSystemFont::FontProbe;
using FontChanger::FontsFromSystemFont::FontScripts;
using FontChanger::FixedSizeFont::glyph_merge_mapping;

namespace {
	const Ranges DigitCodepoints{{U'0', U'9'}};
	const Ranges ExclamationMarkCodepoints{{U'!', U'!'}};

	// Text is drawn a little heavier; the icons and merged glyphs more so.
	constexpr float TextGamma = 1.2f;
	constexpr float SymbolGamma = 1.4f;

	// Draws the game's PUA glyphs as the Lodestone web font (FFXIV_Lodestone_SSF) does.
	constexpr auto LodestoneFontName = "XIV AXIS Std ATK";

	// Fonts of every Windows installation, for the texts of merged glyphs in scripts that the chosen font lacks.
	constexpr std::array MergedTextFallbackFontNames{"Yu Gothic UI", "Malgun Gothic"};

	float GammaOf(FontChanger::FontsFromSystemFont::StrokeWeight weight) {
		using enum FontChanger::FontsFromSystemFont::StrokeWeight;
		switch (weight) {
			case Lighter: return 1.0f;
			case Heavier: return 1.4f;
			case Heaviest: return 1.7f;
			default: return TextGamma;
		}
	}

	LookupStruct WithFeatures(LookupStruct lookup, const Traits& traits, bool monospacedDigits) {
		if (monospacedDigits)
			lookup.Features[DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES] = 1;
		for (const auto feature : traits.Features)
			lookup.Features[static_cast<DWRITE_FONT_FEATURE_TAG>(FontChanger::DirectWriteUtil::StringToTag(feature))] = 1;
		return lookup;
	}

	std::unique_ptr<FontElement> Make(
		LookupStruct lookup,
		float size,
		DWRITE_RENDERING_MODE1 renderMode,
		float gamma,
		Ranges codepoints,
		codepoint_merge_mode mergeMode,
		float horizontalOffset = 0,
		float verticalOffset = 0,
		float letterSpacing = 0) {
		lookup.Synthesis = FontChanger::Structs::SynthesisStruct{};

		auto element = std::make_unique<FontElement>();
		element->Size = size;
		element->Gamma = gamma;
		element->MergeMode = mergeMode;
		element->Source = FontChanger::Structs::ElementSourceEnum::DirectWrite;
		element->Lookup = std::move(lookup);
		element->RendererSpecific.DirectWrite.RenderMode = renderMode;
		element->RendererSpecific.DirectWrite.MeasureMode = DWRITE_MEASURING_MODE_GDI_NATURAL;
		element->RendererSpecific.DirectWrite.GridFitMode = DWRITE_GRID_FIT_MODE_ENABLED;
		element->WrapModifiers.Codepoints = std::move(codepoints);
		element->WrapModifiers.HorizontalOffset = horizontalOffset;
		element->WrapModifiers.VerticalOffset = verticalOffset;
		element->WrapModifiers.LetterSpacing = letterSpacing;
		return element;
	}

	// The game's fonts other than KrnAXIS lack Hangul and the Han characters of Chinese, so those are added rather than only
	// replacing the game's.
	void AddMissingCjk(const FontProbe& font, const std::vector<std::pair<FontElement*, ScriptGroup>>& fitted) {
		const auto isChinese = HasText(font, U"们");
		for (const auto& [pElement, group] : fitted) {
			if (group == ScriptGroup::Hangul || (group == ScriptGroup::Han && isChinese))
				pElement->MergeMode = codepoint_merge_mode::AddAll;
		}
	}

	std::unique_ptr<FontElement> MakeMonospacedDigits(
		const LookupStruct& lookup, float size, DWRITE_RENDERING_MODE1 renderMode, float gamma, float horizontalOffset, float verticalOffset, float letterSpacing) {
		auto digits = Make(lookup, size, renderMode, gamma, DigitCodepoints, codepoint_merge_mode::Replace, horizontalOffset, verticalOffset, letterSpacing);
		digits->WrapModifiers.Monospacing = {
			.MinAdvance = 1.f,
			.MaxAdvance = 1.f,
			.Unit = FontChanger::FixedSizeFont::monospacing_unit::ReferenceCharacter,
			.ReferenceCharacter = U'0',
			.Alignment = FontChanger::FixedSizeFont::monospacing_alignment::CenterAdvance,
		};
		return digits;
	}

	// Merged glyphs aren't moved as the Latin letters are, since their shapes sit where the game's glyphs are; an empty
	// shown lets fallback fonts draw every text.
	std::vector<std::unique_ptr<FontElement>> MakeMergedGlyphElements(
		const LookupStruct& chosen, const FontInfo& font, float size, const FontOpener& open, const std::function<bool(char32_t)>& shown) {
		std::vector<std::pair<LookupStruct, const FontInfo*>> fonts{{{.FamilyName = chosen.FamilyName, .Weight = chosen.Weight, .Stretch = chosen.Stretch, .Style = chosen.Style}, &font}};
		for (const auto name : MergedTextFallbackFontNames) {
			LookupStruct fallback{.FamilyName = name};
			if (const auto info = open(fallback))
				fonts.emplace_back(std::move(fallback), info);
		}

		// Each glyph goes to the first font that has its text; to a fallback font only if the surrounding text shows it.
		std::vector<std::vector<glyph_merge_mapping>> mappingsByFont(fonts.size());
		for (const auto& preset : FontChanger::GlyphMergingPresets::Get()) {
			for (const auto& mapping : preset.Mappings) {
				std::vector<glyph_merge_mapping*> parts(fonts.size());
				for (size_t i = 0; i < mapping.Codepoints.size() && i < mapping.Texts.size(); i++) {
					const auto f = std::ranges::find_if(fonts, [&](const auto& font) { return HasText(*font.second, mapping.Texts[i]); }) - fonts.begin();
					if (f == static_cast<ptrdiff_t>(fonts.size()))
						continue;
					if (f != 0 && shown && !std::ranges::all_of(mapping.Texts[i], [&](char32_t c) { return c == U'_' || c == U'\n' || shown(c); }))
						continue;

					if (!parts[f]) {
						parts[f] = &mappingsByFont[f].emplace_back(mapping);
						parts[f]->Codepoints.clear();
						parts[f]->Texts.clear();
					}
					parts[f]->Codepoints.push_back(mapping.Codepoints[i]);
					parts[f]->Texts.push_back(mapping.Texts[i]);
				}
			}
		}

		std::vector<std::unique_ptr<FontElement>> res;
		for (size_t f = 0; f < fonts.size(); f++) {
			if (mappingsByFont[f].empty())
				continue;

			std::u32string codepoints;
			for (const auto& mapping : mappingsByFont[f])
				codepoints += mapping.Codepoints;

			auto element = Make(fonts[f].first, size, DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC, SymbolGamma, FontChanger::CodepointRanges::FromCodepoints(codepoints), codepoint_merge_mode::Replace);
			element->GlyphMerging.Params.Mappings = std::move(mappingsByFont[f]);
			res.emplace_back(std::move(element));
		}
		return res;
	}

	void AddFamilyMarks(
		std::vector<std::unique_ptr<FontElement>>& elements, const std::vector<FontElement*>& nonCjkElements, const Traits& traits,
		const Placement& placement, const LookupStruct& chosen, const FontInfo& font, const FontOpener& open, float size,
		const std::function<bool(char32_t)>& shown, FontScripts scripts, float raise, float textGamma) {
		// JupiterN's "CriticalDirectHit!!": the digits move right, toward the italic exclamation mark.
		if (traits.Mark && HasScripts(scripts, FontScripts::NonCjk)) {
			auto italic = chosen;
			italic.Style = DWRITE_FONT_STYLE_ITALIC;
			const auto italicFont = open(italic);
			const auto fontSize = nonCjkElements.empty() ? size : nonCjkElements.front()->Size;
			const auto mark = PlaceExclamationMark(*traits.Mark, font, italicFont ? *italicFont : font, fontSize, size, placement.LatinOffset);
			for (const auto pElement : nonCjkElements) {
				// Placed by the mark, which keeping the offset fitted would undo.
				pElement->WrapModifiers.HorizontalOffset = mark.DigitOffset;
				pElement->Auto.HorizontalOffset = false;
			}
			elements.emplace_back(Make(italic, mark.Size, mark.RenderMode, textGamma, ExclamationMarkCodepoints, codepoint_merge_mode::Replace, mark.Offset, mark.VerticalOffset - raise, mark.LetterSpacing));
		}

		if (traits.IsAxis && HasScripts(scripts, FontScripts::GameSymbols)) {
			auto merged = MakeMergedGlyphElements(chosen, font, size, open, shown);

			// Vector PUA glyphs scale to any size; they follow the font's elements, which decide its metrics, and unmerged
			// mergeable ones keep the game's, as the Lodestone font places them apart.
			std::set<char32_t> mergeable;
			for (const auto& preset : FontChanger::GlyphMergingPresets::Get())
				mergeable.insert(preset.Codepoints.begin(), preset.Codepoints.end());
			for (const auto& pElement : merged) {
				for (const auto& mapping : pElement->GlyphMerging.Params.Mappings) {
					for (const auto c : mapping.Codepoints)
						mergeable.erase(c);
				}
			}
			const auto codepoints = FontChanger::CodepointRanges::Subtract(PrivateUseCodepoints, FontChanger::CodepointRanges::FromCodepoints(mergeable));

			elements.emplace_back(Make({.FamilyName = LodestoneFontName}, size, DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC, SymbolGamma, codepoints, codepoint_merge_mode::Replace, placement.PuaOffset, placement.CjkShift));
			elements.insert(elements.end(), std::make_move_iterator(merged.begin()), std::make_move_iterator(merged.end()));
		}
	}

	class DrawnFontProbe final : public FontProbe {
		std::vector<std::pair<const FontElement*, const FontInfo*>> m_elements;
		float m_size = 1;

	public:
		DrawnFontProbe(const FontChanger::Structs::Font& font, const FontOpener& open) {
			if (!font.Elements.empty() && font.Elements.front()->Size > 0)
				m_size = font.Elements.front()->Size;
			for (const auto& element : font.Elements) {
				using enum FontChanger::Structs::ElementSourceEnum;
				const auto drawsFont = (element->Source == DirectWrite || element->Source == FreeType) && !element->GlyphMerging.IsEnabled();
				m_elements.emplace_back(element.get(), drawsFont ? open(element->Lookup) : nullptr);
			}
		}

		[[nodiscard]] std::optional<FontChanger::FontsFromSystemFont::GlyphBox> GetGlyph(char32_t codepoint) const override {
			// Elements take codepoints by their merge modes, the game's having all of theirs and empty ones none.
			std::optional<size_t> drawer;
			for (size_t i = 0; i < m_elements.size(); i++) {
				const auto& [element, info] = m_elements[i];
				const auto has = FontChanger::CodepointRanges::Contains(element->WrapModifiers.Codepoints, codepoint)
					&& (element->Source == FontChanger::Structs::ElementSourceEnum::Game || (info && info->GetGlyph(codepoint)));
				if (!has || (element->MergeMode == codepoint_merge_mode::AddNew && drawer) || (element->MergeMode == codepoint_merge_mode::Replace && !drawer))
					continue;
				drawer = i;
			}
			if (!drawer || !m_elements[*drawer].second)
				return std::nullopt;

			const auto& element = *m_elements[*drawer].first;
			const auto glyph = m_elements[*drawer].second->GetGlyph(codepoint);
			if (!glyph)
				return std::nullopt;

			const auto scale = element.Size / m_size;
			const auto h = std::round(element.WrapModifiers.HorizontalOffset) / m_size;
			const auto v = std::round(element.WrapModifiers.VerticalOffset) / m_size;
			const auto spacing = std::round(element.WrapModifiers.LetterSpacing) / m_size;
			return FontChanger::FontsFromSystemFont::GlyphBox{
				.Left = glyph->Left * scale + h,
				.Right = glyph->Right * scale - h + spacing,
				.Advance = glyph->Advance * scale + spacing,
				.Top = glyph->Top * scale - v,
				.Bottom = glyph->Bottom * scale - v,
			};
		}
	};
}

FontInfo::FontInfo(IDWriteFont* font) {
	SuccessOrThrow(font->CreateFontFace(&m_face));
	DWRITE_FONT_METRICS metrics;
	m_face->GetMetrics(&metrics);
	m_unitsPerEm = metrics.designUnitsPerEm;

	// The ppemY of each BitmapSize record, of 48 bytes after the 8-byte EBLC or CBLC header.
	for (const auto tag : {DWRITE_MAKE_OPENTYPE_TAG('E', 'B', 'L', 'C'), DWRITE_MAKE_OPENTYPE_TAG('C', 'B', 'L', 'C')}) {
		const void* data;
		UINT32 size;
		void* context;
		BOOL exists;
		if (FAILED(m_face->TryGetFontTable(tag, &data, &size, &context, &exists)) || !exists)
			continue;

		const auto p = static_cast<const uint8_t*>(data);
		if (size >= 8) {
			const auto count = static_cast<uint32_t>(p[4]) << 24 | static_cast<uint32_t>(p[5]) << 16 | static_cast<uint32_t>(p[6]) << 8 | p[7];
			for (uint32_t i = 0; i < count && 8 + 48 * (i + 1) <= size; i++)
				m_bitmapSizes.insert(p[8 + 48 * i + 45]);
		}
		m_face->ReleaseFontTable(context);
	}
}

std::optional<FontInfo> FontInfo::Open(const std::string& name, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch, DWRITE_FONT_STYLE style) {
	try {
		static const auto collection = [] {
			IDWriteFactoryPtr factory;
			SuccessOrThrow(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&factory)));
			IDWriteFontCollectionPtr res;
			SuccessOrThrow(factory->GetSystemFontCollection(&res));
			return res;
		}();

		UINT32 index;
		BOOL exists;
		if (FAILED(collection->FindFamilyName(xivres::util::unicode::convert<std::wstring>(name).c_str(), &index, &exists)) || !exists)
			return std::nullopt;

		IDWriteFontFamilyPtr family;
		IDWriteFontPtr font;
		SuccessOrThrow(collection->GetFontFamily(index, &family));
		SuccessOrThrow(family->GetFirstMatchingFont(weight, stretch, style, &font));
		return FontInfo(font);
	} catch (const std::exception&) {
		return std::nullopt;
	}
}

std::optional<FontChanger::FontsFromSystemFont::GlyphBox> FontInfo::GetGlyph(char32_t codepoint) const {
	const auto c = static_cast<UINT32>(codepoint);
	UINT16 glyph = 0;
	DWRITE_GLYPH_METRICS gm;
	if (FAILED(m_face->GetGlyphIndices(&c, 1, &glyph)) || !glyph || FAILED(m_face->GetDesignGlyphMetrics(&glyph, 1, &gm, FALSE)))
		return std::nullopt;

	const auto top = gm.verticalOriginY - gm.topSideBearing;
	const auto bottom = top - (static_cast<int>(gm.advanceHeight) - gm.topSideBearing - gm.bottomSideBearing);
	return GlyphBox{
		.Left = gm.leftSideBearing / m_unitsPerEm,
		.Right = gm.rightSideBearing / m_unitsPerEm,
		.Advance = gm.advanceWidth / m_unitsPerEm,
		.Top = top / m_unitsPerEm,
		.Bottom = bottom / m_unitsPerEm,
	};
}

bool FontInfo::HasBitmapOf(int ppem) const {
	return m_bitmapSizes.contains(ppem);
}

bool FontInfo::PrefersNatural(float ppem) const {
	// At 96 dpi, where pixels are DIPs, measured naturally, with the system's rendering settings.
	IDWriteFontFace3* face3 = nullptr;
	if (FAILED(m_face->QueryInterface(__uuidof(IDWriteFontFace3), reinterpret_cast<void**>(&face3))))
		return false;
	DWRITE_RENDERING_MODE1 mode;
	DWRITE_GRID_FIT_MODE gridFit;
	const auto hr = face3->GetRecommendedRenderingMode(ppem, 96, 96, nullptr, FALSE, DWRITE_OUTLINE_THRESHOLD_ANTIALIASED, DWRITE_MEASURING_MODE_NATURAL, nullptr, &mode, &gridFit);
	face3->Release();
	return SUCCEEDED(hr) && mode == DWRITE_RENDERING_MODE1_NATURAL;
}

std::optional<std::pair<std::string_view, float>> FontChanger::FontsFromSystemFont::GetGameFontFamilyAndSize(std::string_view fontName) {
	if (const auto def = FontChanger::FixedSizeFont::find_fontdata_definition(fontName))
		return std::make_pair(std::string_view(def->Family), def->Size);
	return std::nullopt;
}

std::optional<float> FontChanger::FontsFromSystemFont::GetGameFontSize(const Structs::Font& font) {
	if (!font.Elements.empty())
		return font.Elements.front()->Size;
	if (const auto familyAndSize = GetGameFontFamilyAndSize(font.Name))
		return familyAndSize->second;
	return std::nullopt;
}

LookupStruct FontChanger::FontsFromSystemFont::GetLookupFromLogFont(const LOGFONTW& logFont) {
	IDWriteFactoryPtr factory;
	SuccessOrThrow(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&factory)));
	IDWriteGdiInteropPtr interop;
	SuccessOrThrow(factory->GetGdiInterop(&interop));
	IDWriteFontPtr font;
	SuccessOrThrow(interop->CreateFontFromLOGFONT(&logFont, &font));
	IDWriteFontFamilyPtr family;
	SuccessOrThrow(font->GetFontFamily(&family));

	return {
		.FamilyName = xivres::util::unicode::convert<std::string>(DirectWriteUtil::GetEnglishFamilyName(family)),
		.Weight = font->GetWeight(),
		.Stretch = font->GetStretch(),
		.Style = font->GetStyle(),
		.Synthesis = Structs::SynthesisStruct{},
	};
}

std::vector<FontChanger::Structs::FontElement*> FontChanger::FontsFromSystemFont::AddElement(Structs::Font& font, const LookupStruct& chosen, size_t index) {
	FontCache open;
	const auto pFontInfo = open(chosen);
	if (!pFontInfo)
		throw std::runtime_error(std::format("Font not found: {}", chosen.FamilyName));

	const auto familyAndSize = GetGameFontFamilyAndSize(font.Name);
	const auto size = GetGameFontSize(font);
	const auto lookup = familyAndSize ? WithFeatures(chosen, Traits::Of(familyAndSize->first), false) : chosen;

	// The glyphs that the other fonts draw stay theirs; the font replaces the game's, other than its symbols.
	std::set<char32_t> drawnByOthers;
	if (!font.Elements.empty()) {
		const auto merged = font.GetMergedFont();
		const auto gameElement = std::ranges::find(font.Elements, Structs::ElementSourceEnum::Game, [](const auto& e) { return e->Source; });
		const auto gameBase = gameElement != font.Elements.end() ? (*gameElement)->GetBaseFont().get() : nullptr;
		for (const auto c : merged->all_codepoints()) {
			if (const auto drawer = merged->get_base_font(c); drawer && drawer != gameBase)
				drawnByOthers.insert(c);
		}
	}
	auto codepoints = CodepointRanges::Subtract(CodepointRanges::Subtract(AllCodepoints, PrivateUseCodepoints), CodepointRanges::FromCodepoints(drawnByOthers));

	index = (std::min)(index, font.Elements.size());
	const auto pElement = font.Elements.emplace(
		font.Elements.begin() + static_cast<ptrdiff_t>(index),
		Make(lookup, size.value_or(12.f), DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC, TextGamma, std::move(codepoints), codepoint_merge_mode::Replace))->get();
	font.OnElementChange();
	if (!familyAndSize || !size)
		return {pElement};

	const auto fitted = AutoFit(font, familyAndSize->first, *size, {pElement});
	AddMissingCjk(*pFontInfo, fitted.Fitted);
	font.OnElementChange();

	const auto fittedElements = fitted.Fitted | std::views::keys;
	return {fittedElements.begin(), fittedElements.end()};
}

std::vector<FontChanger::Structs::Font*> FontChanger::FontsFromSystemFont::AddFamilyElements(
	const Structs::FontSet& fontSet, std::string_view family, const LookupStruct& chosen, bool monospacedDigits) {
	FontCache open;
	const auto pFontInfo = open(chosen);
	if (!pFontInfo)
		throw std::runtime_error(std::format("Font not found: {}", chosen.FamilyName));
	const auto& font = *pFontInfo;

	const auto traits = Traits::Of(family);
	const auto lookup = WithFeatures(chosen, traits, monospacedDigits);
	const auto needsMonospacedDigits = monospacedDigits && !HasTabularDigits(font);

	std::vector<Structs::Font*> res;
	for (const auto& pFont : fontSet.Fonts) {
		const auto other = GetGameFontFamilyAndSize(pFont->Name);
		const auto size = GetGameFontSize(*pFont);
		if (!other || !size || other->first != family)
			continue;

		auto& elements = pFont->Elements;
		std::vector<FontElement*> targets{elements.emplace_back(Make(lookup, *size, DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC, TextGamma, AllCodepoints, codepoint_merge_mode::Replace)).get()};
		if (needsMonospacedDigits)
			targets.push_back(elements.emplace_back(MakeMonospacedDigits(lookup, *size, DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC, TextGamma, 0, 0, 0)).get());
		const auto fitted = AutoFit(*pFont, family, *size, targets);
		AddMissingCjk(font, fitted.Fitted);

		std::vector<FontElement*> nonCjkElements;
		for (const auto& [pElement, group] : fitted.Fitted) {
			if (group == ScriptGroup::NonCjk)
				nonCjkElements.push_back(pElement);
		}
		AddFamilyMarks(elements, nonCjkElements, traits, Placement::Of(traits, *size, other->second), chosen, font, std::ref(open), *size, {}, FontScripts::All, 0, TextGamma);

		pFont->OnElementChange();
		res.push_back(pFont.get());
	}
	return res;
}

std::unique_ptr<FontChanger::Structs::Font> FontChanger::FontsFromSystemFont::MakeFont(
	std::string_view fontName, float size, const LookupStruct& chosen, const FontOpener& open, bool monospacedDigits, const FixedSizeFont::fixed_size_font* gameFont,
	const std::vector<const FontProbe*>& fontsAbove, const FontAdjustments& adjustments) {
	const auto pFontInfo = open(chosen);
	if (!pFontInfo)
		return nullptr;
	const auto& font = *pFontInfo;

	// Presets name fonts of families that the game lacks as the family up to the last underscore.
	const auto familyAndSize = GetGameFontFamilyAndSize(fontName);
	auto family = fontName;
	if (familyAndSize)
		family = familyAndSize->first;
	else if (const auto underscore = fontName.rfind('_'); underscore != std::string_view::npos && underscore > 0)
		family = fontName.substr(0, underscore);

	const auto traits = Traits::Of(family);
	const auto placement = Placement::Of(traits, size, familyAndSize ? familyAndSize->second : size);
	const auto lookup = WithFeatures(chosen, traits, monospacedDigits);

	if (gameFont && gameFont->all_codepoints().empty())
		gameFont = nullptr;
	std::optional<LineMetrics> line;
	if (gameFont)
		line = LineMetrics::Of(*gameFont, gameFont->font_size() > 0 ? size / gameFont->font_size() : 1.f);

	// The base font, whose CJK scripts this font's are sized as, is the first of the fonts above that has any of the
	// capitals, or this one, unless it doesn't draw its Latin letters.
	const auto hasCapitals = [&traits](const FontProbe& f) {
		return std::ranges::any_of(traits.ReferenceCharacters, [&f](char32_t c) { return f.GetGlyph(c).has_value(); });
	};
	const auto above = std::ranges::find_if(fontsAbove, [&](const FontProbe* f) { return f && hasCapitals(*f); });
	auto baseFace = above != fontsAbove.end() ? *above : nullptr;
	if (!baseFace && HasScripts(adjustments.Scripts, FontScripts::NonCjk) && hasCapitals(font))
		baseFace = &font;

	// CJK scripts are fitted next to the Latin letters as fitted, before the user's scaling.
	const auto latin = FitLatin(font, traits, size);
	const auto targetCapHeight = size * traits.ReferenceHeight;
	const auto drawsNonCjk = HasScripts(adjustments.Scripts, FontScripts::NonCjk);
	const auto raise = std::round(adjustments.Raise * size);
	const auto nonCjkSpacing = std::round(adjustments.NonCjkSpacing * size);
	const auto cjkSpacing = std::round(adjustments.CjkSpacing * size);
	const auto textGamma = GammaOf(adjustments.StrokeWeight);
	const auto scaledLatin = ScaleLatin(font, latin, adjustments.NonCjkSize);

	// Groups that the font isn't to draw are left out, also from the rest where too few of them to fit as a group are, but
	// for the symbols that go with Han.
	auto parts = SplitByScriptGroup(font, AllCodepoints, gameFont, traits.HasText);
	const auto findPart = [&parts](ScriptGroup group) { return std::ranges::find(parts, group, [](const auto& p) { return p.first; }); };
	Ranges symbols;
	for (const auto& group : CjkGroups) {
		if (HasScripts(adjustments.Scripts, group.Scripts))
			continue;
		if (const auto part = findPart(group.Group); group.Group == ScriptGroup::Han && part != parts.end())
			symbols = CodepointRanges::Subtract(part->second, group.Codepoints);
		if (const auto rest = findPart(ScriptGroup::NonCjk); rest != parts.end())
			rest->second = CodepointRanges::Subtract(rest->second, group.Codepoints);
	}
	if (!symbols.empty()) {
		if (const auto rest = findPart(ScriptGroup::NonCjk); rest != parts.end()) {
			rest->second.insert(rest->second.end(), symbols.begin(), symbols.end());
			rest->second = CodepointRanges::Subtract(rest->second, {});
		} else {
			parts.insert(parts.begin(), {ScriptGroup::NonCjk, std::move(symbols)});
		}
	}
	std::erase_if(parts, [&adjustments](const auto& p) {
		const auto group = CjkGroup::Of(p.first);
		return !HasScripts(adjustments.Scripts, group ? group->Scripts : FontScripts::NonCjk);
	});

	std::vector<std::unique_ptr<FontElement>> elements;
	std::vector<std::pair<FontElement*, ScriptGroup>> fitted;
	std::vector<FontElement*> nonCjkElements;
	for (auto& [groupId, codepoints] : parts) {
		const auto group = CjkGroup::Of(groupId);
		GroupPlacement placed{scaledLatin.Size, scaledLatin.RenderMode, placement.LatinOffset, scaledLatin.VerticalOffset};
		if (const auto ink = group ? MeasureInk(font, group->Samples) : std::nullopt) {
			// Drawn as the base font draws them, unless it lacks them or the game's glyphs of them stay, which are drawn as the
			// game draws them next to its own Latin letters.
			const auto baseInk = baseFace == &font ? ink : baseFace ? MeasureInk(*baseFace, group->Samples) : std::nullopt;
			if (!baseInk || IsLeftToGame(font, baseFace, gameFont, *group)) {
				placed = FitInk(font, *ink, group->TopPerCapHeight * targetCapHeight, group->BottomPerCapHeight * targetCapHeight, latin.Size, placement.LatinOffset);
			} else if (baseFace == &font) {
				placed = KeepWithinLine(font, *ink, {latin.Size, latin.RenderMode, placement.LatinOffset, latin.VerticalOffset + placement.CjkShift}, line, latin.Size);
			} else {
				placed = FitInk(font, *ink, baseInk->Top * size, baseInk->Bottom * size, latin.Size, placement.LatinOffset);
			}
			placed = ScaleAroundInk(font, *ink, placed, adjustments.CjkSize);
		}

		const auto pElement = elements.emplace_back(Make(
			lookup, placed.Size, placed.RenderMode, textGamma, std::move(codepoints), codepoint_merge_mode::Replace, placed.HorizontalOffset, placed.VerticalOffset - raise,
			group ? cjkSpacing : nonCjkSpacing)).get();
		fitted.emplace_back(pElement, groupId);
		if (!group)
			nonCjkElements.push_back(pElement);
	}
	AddMissingCjk(font, fitted);

	if (drawsNonCjk && monospacedDigits && !HasTabularDigits(font))
		nonCjkElements.push_back(elements.emplace_back(MakeMonospacedDigits(lookup, scaledLatin.Size, scaledLatin.RenderMode, textGamma, placement.LatinOffset, scaledLatin.VerticalOffset - raise, nonCjkSpacing)).get());

	// Text is shown in this font, the fonts drawn over it, and the game's glyphs that stay.
	const auto shown = [&](char32_t c) {
		return font.GetGlyph(c) || std::ranges::any_of(fontsAbove, [c](const FontProbe* f) { return f && f->GetGlyph(c); }) || (gameFont && gameFont->all_codepoints().contains(c));
	};
	AddFamilyMarks(elements, nonCjkElements, traits, placement, chosen, font, open, size, shown, adjustments.Scripts, raise, textGamma);
	if (elements.empty())
		return nullptr;

	// The game's glyphs go first, as Replace elements draw nothing without an earlier one, and give the font the game
	// font's line, as in a preset.
	auto res = std::make_unique<Structs::Font>();
	res->Name = fontName;
	auto& game = *res->Elements.emplace_back(std::make_unique<FontElement>());
	game.Source = Structs::ElementSourceEnum::Game;
	game.Lookup.FamilyName = family;
	game.Size = size;
	game.WrapModifiers.Codepoints = {{0, 0x10FFFF}};
	res->Elements.insert(res->Elements.end(), std::make_move_iterator(elements.begin()), std::make_move_iterator(elements.end()));
	return res;
}

std::unique_ptr<FontProbe> FontChanger::FontsFromSystemFont::ProbeFont(const Structs::Font& font, const FontOpener& open) {
	return std::make_unique<DrawnFontProbe>(font, open);
}

std::vector<std::unique_ptr<FontElement>> FontChanger::FontsFromSystemFont::MakeFallbackElements(
	const Structs::Font& font, float size, const LookupStruct& fallback, const FontOpener& open, const FontProbe& drawn,
	const FixedSizeFont::fixed_size_font* gameFont) {
	// The game's symbols stay the game's, which fallback fonts don't draw.
	static constexpr FontAdjustments Adjustments{
		.Scripts = static_cast<FontScripts>(static_cast<uint32_t>(FontScripts::All) & ~static_cast<uint32_t>(FontScripts::GameSymbols)),
	};

	if (font.Elements.empty() || font.Elements.front()->Size <= 0)
		return {};
	const auto made = MakeFont(font.Name, size, fallback, open, false, gameFont, {&drawn}, Adjustments);
	if (!made)
		return {};

	const auto factor = font.Elements.front()->Size / size;
	std::vector<std::unique_ptr<FontElement>> res;
	for (auto& e : made->Elements) {
		if (e->Source == Structs::ElementSourceEnum::Game)
			continue;

		// Private-use characters keep the system's icon font at its own size.
		e->WrapModifiers.Codepoints = CodepointRanges::Subtract(e->WrapModifiers.Codepoints, PrivateUseCodepoints);
		e->Scale(factor);
		res.push_back(std::move(e));
	}
	return res;
}
