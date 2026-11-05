#include "../include/FontChanger.FixedSizeFont/opentype_positioning.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <ranges>

#include <harfbuzz/hb.h>
#include <harfbuzz/hb-ot.h>

#include "../include/FontChanger.FixedSizeFont/util.truetype.h"

namespace {
	// Selects GPOS lookups per script, the way shapers do for a run of text in that script.
	class script_lookup_selector {
		const FontChanger::FixedSizeFont::truetype::Gpos::View& m_gpos;
		const std::set<uint32_t>& m_featureTags;
		const hb_language_t m_language;
		const std::span<const float> m_normalizedCoordinates;
		std::map<hb_script_t, std::set<uint16_t>> m_cache;

	public:
		script_lookup_selector(const FontChanger::FixedSizeFont::truetype::Gpos::View& gpos, const std::set<uint32_t>& featureTags, hb_language_t language, std::span<const float> normalizedCoordinates)
			: m_gpos(gpos)
			, m_featureTags(featureTags)
			, m_language(language)
			, m_normalizedCoordinates(normalizedCoordinates) {}

		const std::set<uint16_t>& get(hb_script_t script) {
			if (const auto it = m_cache.find(script); it != m_cache.end())
				return it->second;

			hb_tag_t hbScriptTags[HB_OT_MAX_TAGS_PER_SCRIPT];
			hb_tag_t hbLanguageTags[HB_OT_MAX_TAGS_PER_LANGUAGE];
			unsigned scriptTagCount = HB_OT_MAX_TAGS_PER_SCRIPT;
			unsigned languageTagCount = HB_OT_MAX_TAGS_PER_LANGUAGE;
			hb_ot_tags_from_script_and_language(script, m_language, &scriptTagCount, hbScriptTags, &languageTagCount, hbLanguageTags);

			// HarfBuzz tags are big endian integers; TagStruct::NativeValue keeps the bytes in file order.
			std::vector<uint32_t> scriptTags, languageTags;
			for (unsigned i = 0; i < scriptTagCount; i++)
				scriptTags.push_back(_byteswap_ulong(hbScriptTags[i]));
			for (unsigned i = 0; i < languageTagCount; i++)
				languageTags.push_back(_byteswap_ulong(hbLanguageTags[i]));

			return m_cache.emplace(script, m_gpos.GetFeatureLookupIndices(m_featureTags, scriptTags, languageTags, m_normalizedCoordinates)).first->second;
		}
	};

	// Returns the script of the codepoint, or HB_SCRIPT_INVALID if it takes the script of the surrounding text.
	hb_script_t get_specific_script(char32_t codepoint) {
		switch (const auto script = hb_unicode_script(hb_unicode_funcs_get_default(), codepoint)) {
			case HB_SCRIPT_COMMON:
			case HB_SCRIPT_INHERITED:
			case HB_SCRIPT_UNKNOWN:
				return HB_SCRIPT_INVALID;
			case HB_SCRIPT_KATAKANA:
				// Hiragana and katakana share the OpenType script 'kana', and are shaped as one run.
				return HB_SCRIPT_HIRAGANA;
			default:
				return script;
		}
	}
}

namespace {
	// Shapes pairs of glyphs that contextual lookups may position, and returns the resulting pair kerning in font units,
	// excluding single adjustments which are applied to glyphs separately.
	std::map<std::pair<char32_t, char32_t>, double> shape_contextual_kerning(
		const FontChanger::FixedSizeFont::opentype_positioning_params& params,
		hb_script_t script,
		hb_language_t language,
		const std::vector<uint16_t>& lookupIndices,
		const std::vector<std::set<char32_t>>& glyphToCharMap,
		const std::map<uint16_t, FontChanger::FixedSizeFont::truetype::Gpos::View::SingleAdjustment>& singleAdjustments
	) {
		std::map<std::pair<char32_t, char32_t>, double> result;
		const auto face = params.HarfBuzzFace;

		// Only glyphs that the contextual lookups refer to, and that are drawn for a codepoint, can be affected.
		// Marks are left out, as shapers zero their advances instead of kerning them.
		const auto glyphSet = std::unique_ptr<hb_set_t, decltype(&hb_set_destroy)>(hb_set_create(), &hb_set_destroy);
		for (const auto lookupIndex : lookupIndices)
			hb_ot_layout_lookup_collect_glyphs(face, HB_OT_TAG_GPOS, lookupIndex, glyphSet.get(), glyphSet.get(), glyphSet.get(), nullptr);

		std::vector<uint16_t> candidates;
		for (hb_codepoint_t glyph = HB_SET_VALUE_INVALID; hb_set_next(glyphSet.get(), &glyph);) {
			if (glyph < glyphToCharMap.size() && !glyphToCharMap[glyph].empty() && hb_ot_layout_get_glyph_class(face, glyph) != HB_OT_LAYOUT_GLYPH_CLASS_MARK)
				candidates.push_back(static_cast<uint16_t>(glyph));
		}
		if (candidates.empty())
			return result;

		const auto font = std::unique_ptr<hb_font_t, decltype(&hb_font_destroy)>(hb_font_create(face), &hb_font_destroy);
		const auto unitsPerEm = static_cast<int>(params.UnitsPerEm);
		hb_font_set_scale(font.get(), unitsPerEm, unitsPerEm);
		hb_font_set_ppem(font.get(),
			static_cast<unsigned>(std::lround(std::abs(params.Size * params.ScaleX))),
			static_cast<unsigned>(std::lround(std::abs(params.Size * params.ScaleY))));
		std::vector<hb_variation_t> variations;
		for (const auto& [tag, value] : params.DesignCoordinates)
			variations.push_back({ _byteswap_ulong(tag), value });
		hb_font_set_variations(font.get(), variations.data(), static_cast<unsigned>(variations.size()));

		// Leave out positioning that is not kerning, and substitutions that would change the glyphs of the pair.
		std::vector<hb_feature_t> features;
		for (const auto* disabled : { "-mark", "-mkmk", "-curs", "-dist", "-abvm", "-blwm", "-liga", "-clig", "-calt", "-rclt" }) {
			hb_feature_t feature;
			hb_feature_from_string(disabled, -1, &feature);
			features.push_back(feature);
		}
		for (const auto tag : params.FeatureTags)
			features.push_back({ _byteswap_ulong(tag), 1, HB_FEATURE_GLOBAL_START, HB_FEATURE_GLOBAL_END });
		for (const auto tag : params.DisabledFeatureTags)
			features.push_back({ _byteswap_ulong(tag), 0, HB_FEATURE_GLOBAL_START, HB_FEATURE_GLOBAL_END });

		const auto buffer = std::unique_ptr<hb_buffer_t, decltype(&hb_buffer_destroy)>(hb_buffer_create(), &hb_buffer_destroy);
		const auto getSingle = [&singleAdjustments](uint16_t glyph) {
			const auto it = singleAdjustments.find(glyph);
			return it == singleAdjustments.end() ? FontChanger::FixedSizeFont::truetype::Gpos::View::SingleAdjustment{} : it->second;
		};

		for (const auto glyph1 : candidates) {
			const auto nominalAdvance = hb_font_get_glyph_h_advance(font.get(), glyph1);
			const auto single1 = getSingle(glyph1);
			for (const auto glyph2 : candidates) {
				const uint32_t text[2]{ static_cast<uint32_t>(*glyphToCharMap[glyph1].begin()), static_cast<uint32_t>(*glyphToCharMap[glyph2].begin()) };
				hb_buffer_clear_contents(buffer.get());
				hb_buffer_add_utf32(buffer.get(), text, 2, 0, 2);
				hb_buffer_set_direction(buffer.get(), HB_DIRECTION_LTR);
				hb_buffer_set_script(buffer.get(), script);
				if (language != HB_LANGUAGE_INVALID)
					hb_buffer_set_language(buffer.get(), language);
				hb_shape(font.get(), buffer.get(), features.data(), static_cast<unsigned>(features.size()));

				unsigned count;
				const auto* infos = hb_buffer_get_glyph_infos(buffer.get(), &count);
				const auto* positions = hb_buffer_get_glyph_positions(buffer.get(), &count);
				if (count != 2 || infos[0].codepoint != glyph1 || infos[1].codepoint != glyph2)
					continue;

				const auto value = (positions[0].x_advance - nominalAdvance - single1.AdvanceX) + (positions[1].x_offset - getSingle(glyph2).PlacementX);
				for (const auto c1 : glyphToCharMap[glyph1])
					for (const auto c2 : glyphToCharMap[glyph2])
						result.insert_or_assign(std::make_pair(c1, c2), value);
			}
		}

		return result;
	}
}
FontChanger::FixedSizeFont::opentype_positioning FontChanger::FixedSizeFont::extract_opentype_positioning(const opentype_positioning_params& params, const std::vector<std::set<char32_t>>& glyphToCharMap) {
	using namespace truetype;

	opentype_positioning result;
	if (!params.UnitsPerEm)
		return result;

	const Kern::View kern(params.Kern);
	const Gpos::View gpos(params.Gpos);

	// Values of variable fonts vary with the instance.
	const auto normalizedCoordinates = Fvar::NormalizeCoordinates(Fvar::ReadAxes(params.Fvar), params.DesignCoordinates, params.Avar);
	// Device tables adjust values in whole pixels at specific sizes, which the fixed pixel size of FDT fonts lets us apply.
	const Gpos::View::ValueContext valueContext{
		.VariationStore = Gdef::GetItemVariationStore(params.Gdef),
		.NormalizedCoordinates = normalizedCoordinates,
		.PpemX = static_cast<unsigned>(std::lround(std::abs(params.Size * params.ScaleX))),
		.PpemY = static_cast<unsigned>(std::lround(std::abs(params.Size * params.ScaleY))),
		.UnitsPerEm = params.UnitsPerEm,
	};

	// Positioning moves glyphs, so it is subject to the scale of the transformation.
	const auto scaleX = static_cast<double>(params.Size) * params.ScaleX / params.UnitsPerEm;
	const auto scaleY = static_cast<double>(params.Size) * params.ScaleY / params.UnitsPerEm;
	const auto toPixels = [](double value, double scale) { return static_cast<int>(std::lround(value * scale)); };

	// Characters such as punctuation belong to no script in particular, and take the script of the surrounding text.
	// As the FDT format is context-free, assume that they are used in text of the script of the language.
	const auto language = params.LanguageCode.empty() ? HB_LANGUAGE_INVALID : hb_language_from_string(params.LanguageCode.c_str(), static_cast<int>(params.LanguageCode.size()));
	auto defaultScript = hb_script_from_string(get_default_script_for_language(params.LanguageCode).c_str(), -1);
	if (defaultScript == HB_SCRIPT_INVALID || defaultScript == HB_SCRIPT_UNKNOWN)
		defaultScript = HB_SCRIPT_LATIN;

	std::vector<uint16_t> glyphs;
	std::map<hb_script_t, std::vector<char32_t>> charsByScript;
	std::map<hb_script_t, std::vector<uint16_t>> glyphsByScript;
	for (size_t i = 0, i_ = (std::min<size_t>)(glyphToCharMap.size(), 65536); i < i_; i++) {
		if (glyphToCharMap[i].empty())
			continue;

		const auto glyph = static_cast<uint16_t>(i);
		glyphs.push_back(glyph);
		for (const auto c : glyphToCharMap[i]) {
			if (const auto script = get_specific_script(c); script != HB_SCRIPT_INVALID)
				charsByScript[script].push_back(c);
		}

		const auto glyphScript = get_specific_script(*glyphToCharMap[i].begin());
		glyphsByScript[glyphScript == HB_SCRIPT_INVALID ? defaultScript : glyphScript].push_back(glyph);
	}

	// Single adjustments, in font units.
	const auto kernDisabled = params.DisabledFeatureTags.contains(Gpos::KerningFeatureTag.NativeValue);
	std::map<uint16_t, Gpos::View::SingleAdjustment> singleAdjustments;
	if (gpos) {
		auto featureTags = params.FeatureTags;
		if (!kernDisabled)
			featureTags.insert(Gpos::KerningFeatureTag.NativeValue);
		script_lookup_selector lookups(gpos, featureTags, language, normalizedCoordinates);

		for (const auto& [script, scriptGlyphs] : glyphsByScript)
			singleAdjustments.merge(gpos.ExtractSingleAdjustments(lookups.get(script), scriptGlyphs, valueContext));
	}

	if ((gpos || kern) && !kernDisabled) {
		const std::set featureTags{ Gpos::KerningFeatureTag.NativeValue };
		script_lookup_selector lookups(gpos, featureTags, language, normalizedCoordinates);

		// A pair of characters of different scripts is never positioned together, as shapers split runs by script.
		const auto getPairScript = [defaultScript](char32_t c1, char32_t c2) {
			const auto s1 = get_specific_script(c1);
			const auto s2 = get_specific_script(c2);
			if (s1 != HB_SCRIPT_INVALID && s2 != HB_SCRIPT_INVALID)
				return s1 == s2 ? s1 : HB_SCRIPT_INVALID;
			if (s1 != HB_SCRIPT_INVALID)
				return s1;
			if (s2 != HB_SCRIPT_INVALID)
				return s2;
			return defaultScript;
		};

		auto scripts = std::set{ defaultScript };
		for (const auto& script : charsByScript | std::views::keys)
			scripts.insert(script);

		std::optional<std::map<std::pair<char32_t, char32_t>, int>> kernTablePairs;
		std::map<std::pair<char32_t, char32_t>, double> kerning;
		for (const auto script : scripts) {
			// As shapers do, the legacy kern table is used where GPOS has no 'kern' feature for the script and language.
			const auto& lookupIndices = gpos ? lookups.get(script) : std::set<uint16_t>{};
			if (lookupIndices.empty()) {
				if (!kern)
					continue;
				if (!kernTablePairs)
					kernTablePairs = kern.Parse(glyphToCharMap);
				for (const auto& [pair, value] : *kernTablePairs) {
					if (getPairScript(pair.first, pair.second) == script)
						kerning.emplace(pair, value);
				}
				continue;
			}

			// Only characters of this script, and characters that take the script of the surrounding text, can be in a pair of this script.
			std::vector<std::set<char32_t>> scriptGlyphToCharMap(glyphToCharMap.size());
			for (const auto glyph : glyphs) {
				for (const auto c : glyphToCharMap[glyph]) {
					if (const auto s = get_specific_script(c); s == script || s == HB_SCRIPT_INVALID)
						scriptGlyphToCharMap[glyph].insert(c);
				}
			}

			for (const auto& [pair, value] : gpos.ExtractAdvanceX(scriptGlyphToCharMap, lookupIndices, valueContext)) {
				if (getPairScript(pair.first, pair.second) == script)
					kerning.emplace(pair, value);
			}

			// Kerning that contextual lookups apply to a pair of glyphs can only be found by shaping the pair.
			if (params.HarfBuzzFace) {
				std::vector<uint16_t> contextualLookups;
				for (const auto lookupIndex : lookupIndices) {
					const auto lookupType = gpos.GetLookupType(lookupIndex);
					if (lookupType == LookupType::ContextPositioning || lookupType == LookupType::ChainedContextPositioning)
						contextualLookups.push_back(lookupIndex);
				}
				if (!contextualLookups.empty()) {
					for (const auto& [pair, value] : shape_contextual_kerning(params, script, language, contextualLookups, scriptGlyphToCharMap, singleAdjustments)) {
						if (getPairScript(pair.first, pair.second) == script)
							kerning.insert_or_assign(pair, value);
					}
				}
			}
		}

		for (const auto& [pair, value] : kerning) {
			if (const auto pixels = toPixels(value, scaleX))
				result.KerningPairs.emplace(pair, pixels);
		}
	}

	for (const auto& [glyph, adjustment] : singleAdjustments) {
		const glyph_adjustment pixels{
			.PlacementX = toPixels(adjustment.PlacementX, scaleX),
			.PlacementY = -toPixels(adjustment.PlacementY, scaleY),
			.AdvanceX = toPixels(adjustment.AdvanceX, scaleX),
		};
		if (pixels.PlacementX || pixels.PlacementY || pixels.AdvanceX)
			result.GlyphAdjustments.emplace(glyph, pixels);
	}
	return result;
}

std::string FontChanger::FixedSizeFont::get_default_script_for_language(const std::string& languageCode) {
	// Usual scripts of languages whose text is not written in the Latin script.
	static const std::map<std::string, std::string> LanguageScripts{
		{"ja", "Hira"},
		{"ko", "Hang"},
		{"zh", "Hani"}, {"yue", "Hani"}, {"cmn", "Hani"}, {"wuu", "Hani"}, {"hak", "Hani"}, {"nan", "Hani"}, {"lzh", "Hani"},
		{"ru", "Cyrl"}, {"uk", "Cyrl"}, {"be", "Cyrl"}, {"bg", "Cyrl"}, {"sr", "Cyrl"}, {"mk", "Cyrl"}, {"kk", "Cyrl"}, {"ky", "Cyrl"}, {"tg", "Cyrl"}, {"mn", "Cyrl"}, {"tt", "Cyrl"}, {"ba", "Cyrl"},
		{"el", "Grek"},
		{"hy", "Armn"},
		{"ka", "Geor"},
		{"he", "Hebr"}, {"yi", "Hebr"},
		{"ar", "Arab"}, {"fa", "Arab"}, {"ur", "Arab"}, {"ps", "Arab"}, {"ug", "Arab"},
		{"hi", "Deva"}, {"mr", "Deva"}, {"ne", "Deva"}, {"sa", "Deva"},
		{"bn", "Beng"}, {"as", "Beng"},
		{"pa", "Guru"},
		{"gu", "Gujr"},
		{"or", "Orya"},
		{"ta", "Taml"},
		{"te", "Telu"},
		{"kn", "Knda"},
		{"ml", "Mlym"},
		{"si", "Sinh"},
		{"th", "Thai"},
		{"lo", "Laoo"},
		{"km", "Khmr"},
		{"my", "Mymr"},
		{"bo", "Tibt"}, {"dz", "Tibt"},
		{"am", "Ethi"}, {"ti", "Ethi"},
	};

	std::vector<std::string> subtags;
	for (size_t pos = 0; pos <= languageCode.size();) {
		auto next = languageCode.find_first_of("-_", pos);
		if (next == std::string::npos)
			next = languageCode.size();
		std::string subtag = languageCode.substr(pos, next - pos);
		for (auto& ch : subtag)
			ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		subtags.emplace_back(std::move(subtag));
		pos = next + 1;
	}

	// A script subtag, such as "Hant" in "zh-Hant", is four letters long and follows the language subtag.
	for (size_t i = 1; i < subtags.size(); i++) {
		auto& subtag = subtags[i];
		if (subtag.size() == 4 && std::ranges::all_of(subtag, [](char ch) { return std::isalpha(static_cast<unsigned char>(ch)) != 0; })) {
			// Codes for combinations or variants of scripts, which shaping does not distinguish.
			if (subtag == "hans" || subtag == "hant")
				return "Hani";
			if (subtag == "jpan" || subtag == "hrkt")
				return "Hira";
			if (subtag == "kore")
				return "Hang";

			subtag[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(subtag[0])));
			return subtag;
		}
	}

	if (!subtags.empty()) {
		if (const auto it = LanguageScripts.find(subtags.front()); it != LanguageScripts.end())
			return it->second;
	}

	return "Latn";
}
std::map<uint32_t, int> FontChanger::FixedSizeFont::read_baselines(std::span<const char> base) {
	using truetype::Base;
	using truetype::TagStruct;

	const auto tag = [](const char (&s)[5]) { return TagStruct{ { s[0], s[1], s[2], s[3] } }.NativeValue; };
	const std::vector romanScripts{ tag("latn"), tag("DFLT") };
	const std::vector ideographicScripts{ tag("hani"), tag("kana"), tag("hang"), tag("DFLT") };

	std::map<uint32_t, int> result;
	for (const auto& [baselineTag, value] : Base::ReadHorizontalBaselines(base, romanScripts)) {
		if (baselineTag == Base::RomanBaselineTag.NativeValue)
			result.emplace(baselineTag, value);
	}
	for (const auto& [baselineTag, value] : Base::ReadHorizontalBaselines(base, ideographicScripts)) {
		if (baselineTag == Base::IdeographicEmBoxBottomTag.NativeValue
			|| baselineTag == Base::IdeographicEmBoxTopTag.NativeValue
			|| baselineTag == Base::IdeographicFaceBottomTag.NativeValue
			|| baselineTag == Base::IdeographicFaceTopTag.NativeValue)
			result.emplace(baselineTag, value);
	}
	return result;
}