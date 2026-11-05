#include "pch.h"
#include "FontChanger.Presets/Structs.h"

#include "FontChanger.Presets/DirectWriteUtil.h"

// The fewest digits that read back as the same float, such as 7.0000005 rather than the 7.000000476837158 of its double.
static nlohmann::json FloatToJson(float value) {
	char buf[32];
	const auto [end, ec] = std::to_chars(buf, buf + sizeof buf, value);
	if (ec != std::errc{})
		return value;
	return std::strtod(std::string(buf, end).c_str(), nullptr);
}

// Earlier versions stored these as integers, so whole numbers stay integers and resaving their files doesn't change them.
static nlohmann::json PixelValueToJson(float value) {
	if (const auto rounded = std::round(value); rounded == value && std::fabs(rounded) < 1e9f)
		return static_cast<int>(rounded);
	return FloatToJson(value);
}

namespace {
	using FontChanger::FixedSizeFont::glyph_merge_fit_mode;
	using FontChanger::FixedSizeFont::glyph_merge_line_alignment;
	using FontChanger::FixedSizeFont::glyph_merge_shape;
	using FontChanger::FixedSizeFont::glyph_merge_text_mode;

	constexpr const auto& GlyphMergeShapeNames = FontChanger::FixedSizeFont::glyph_merge_shape_names;

	constexpr std::pair<glyph_merge_text_mode, const char*> GlyphMergeTextModeNames[]{
		{glyph_merge_text_mode::Subtract, "subtract"},
		{glyph_merge_text_mode::Difference, "difference"},
	};

	constexpr std::pair<glyph_merge_fit_mode, const char*> GlyphMergeFitModeNames[]{
		{glyph_merge_fit_mode::CondenseThenShrink, "condenseThenShrink"},
		{glyph_merge_fit_mode::Shrink, "shrink"},
		{glyph_merge_fit_mode::Overflow, "overflow"},
	};

	constexpr std::pair<FontChanger::FixedSizeFont::image_coverage_mode, const char*> ImageCoverageModeNames[]{
		{FontChanger::FixedSizeFont::image_coverage_mode::Auto, "auto"},
		{FontChanger::FixedSizeFont::image_coverage_mode::Alpha, "alpha"},
		{FontChanger::FixedSizeFont::image_coverage_mode::Darkness, "darkness"},
		{FontChanger::FixedSizeFont::image_coverage_mode::Brightness, "brightness"},
	};

	constexpr std::pair<glyph_merge_line_alignment, const char*> GlyphMergeLineAlignmentNames[]{
		{glyph_merge_line_alignment::Left, "left"},
		{glyph_merge_line_alignment::Center, "center"},
		{glyph_merge_line_alignment::Right, "right"},
	};

	constexpr std::pair<FontChanger::FixedSizeFont::vertical_alignment, const char*> VerticalAlignmentNames[]{
		{FontChanger::FixedSizeFont::vertical_alignment::Baseline, "baseline"},
		{FontChanger::FixedSizeFont::vertical_alignment::Top, "top"},
		{FontChanger::FixedSizeFont::vertical_alignment::Middle, "middle"},
		{FontChanger::FixedSizeFont::vertical_alignment::Bottom, "bottom"},
		{FontChanger::FixedSizeFont::vertical_alignment::RomanBaseline, "romanBaseline"},
		{FontChanger::FixedSizeFont::vertical_alignment::IdeographicCenter, "ideographicCenter"},
	};

	constexpr std::pair<FontChanger::FixedSizeFont::monospacing_unit, const char*> MonospacingUnitNames[]{
		{FontChanger::FixedSizeFont::monospacing_unit::Em, "em"},
		{FontChanger::FixedSizeFont::monospacing_unit::Pixels, "px"},
		{FontChanger::FixedSizeFont::monospacing_unit::ReferenceCharacter, "character"},
		{FontChanger::FixedSizeFont::monospacing_unit::ReferenceCharacter, "glyph"}, // Read from earlier versions only.
	};

	nlohmann::json::const_iterator FindWithLegacy(const nlohmann::json& json, const char* key, const char* legacyKey) {
		if (const auto it = json.find(key); it != json.end())
			return it;
		return json.find(legacyKey);
	}

	constexpr std::pair<FontChanger::FixedSizeFont::monospacing_alignment, const char*> MonospacingAlignmentNames[]{
		{FontChanger::FixedSizeFont::monospacing_alignment::CenterAdvance, "centerAdvance"},
		{FontChanger::FixedSizeFont::monospacing_alignment::Left, "left"},
		{FontChanger::FixedSizeFont::monospacing_alignment::CenterInk, "centerInk"},
		{FontChanger::FixedSizeFont::monospacing_alignment::Right, "right"},
	};

	template<typename T, size_t N>
	const char* NameOf(const std::pair<T, const char*>(&names)[N], T value) {
		for (const auto& [v, name] : names) {
			if (v == value)
				return name;
		}
		return names[0].second;
	}

	template<typename T, size_t N>
	T ValueOf(const std::pair<T, const char*>(&names)[N], const nlohmann::json& json, const char* key, T defaultValue) {
		if (const auto it = json.find(key); it != json.end() && it->is_string()) {
			const auto& s = it->get_ref<const std::string&>();
			for (const auto& [v, name] : names) {
				if (s == name)
					return v;
			}
		}
		return defaultValue;
	}

	// Decomposes into rotation * skew * scale.
	FontChanger::Structs::TransformStruct FromScreenMatrix(const FontChanger::Structs::TransformStruct::matrix& screen) {
		constexpr auto RadiansToDegrees = 180.f / std::numbers::pi_v<float>;
		FontChanger::Structs::TransformStruct res;
		res.ScaleX = std::hypot(screen.M11, screen.M21);
		float c = 1.f, s = 0.f;
		if (res.ScaleX > 1e-6f) {
			c = screen.M11 / res.ScaleX;
			s = -screen.M21 / res.ScaleX;
			res.RotationDegrees = std::atan2(s, c) * RadiansToDegrees;
		}

		// The remaining upper triangular part is skew * scale: [[ScaleX, -tan(skew) ScaleY], [0, ScaleY]].
		const auto u12 = c * screen.M12 - s * screen.M22;
		res.ScaleY = s * screen.M12 + c * screen.M22;
		if (std::abs(res.ScaleY) > 1e-6f)
			res.SkewDegrees = std::atan(-u12 / res.ScaleY) * RadiansToDegrees;
		return res;
	}
}

std::map<uint32_t, float> FontChanger::Structs::LookupStruct::GetVariationAxisValues() const {
	std::map<uint32_t, float> res;
	for (const auto& [tag, value] : Variations) {
		if (tag.size() == 4)
			res.emplace(FontChanger::DirectWriteUtil::StringToTag(tag), value);
	}
	return res;
}

FontChanger::Structs::TransformStruct::matrix FontChanger::Structs::TransformStruct::Multiply(const matrix& a, const matrix& b) {
	return {
		a.M11 * b.M11 + a.M12 * b.M21,
		a.M11 * b.M12 + a.M12 * b.M22,
		a.M21 * b.M11 + a.M22 * b.M21,
		a.M21 * b.M12 + a.M22 * b.M22,
	};
}

FontChanger::Structs::TransformStruct::matrix FontChanger::Structs::TransformStruct::GetScreenMatrix() const {
	constexpr auto DegreesToRadians = std::numbers::pi_v<float> / 180.f;
	const auto c = std::cos(RotationDegrees * DegreesToRadians);
	const auto s = std::sin(RotationDegrees * DegreesToRadians);
	const auto t = std::tan(SkewDegrees * DegreesToRadians);

	// Counterclockwise on screen, where y grows downwards.
	const matrix rotation{c, s, -s, c};

	// x' = x - t y, so points above the baseline move right.
	const matrix skew{1.f, -t, 0.f, 1.f};
	const matrix scale{ScaleX, 0.f, 0.f, ScaleY};
	return Multiply(rotation, Multiply(skew, scale));
}

FontChanger::Structs::TransformStruct::matrix FontChanger::Structs::TransformStruct::ScreenToRenderer(ElementSourceEnum renderer, const matrix& screen) {
	// DirectWrite transforms row vectors; FreeType column vectors with y growing upwards.
	if (renderer == ElementSourceEnum::DirectWrite)
		return {screen.M11, screen.M21, screen.M12, screen.M22};
	if (renderer == ElementSourceEnum::GlyphImages)
		return screen;
	return {screen.M11, -screen.M12, -screen.M21, screen.M22};
}

void FontChanger::Structs::to_json(nlohmann::json& json, const TransformStruct& value) {
	json = nlohmann::json::object({
		{"scaleX", value.ScaleX},
		{"scaleY", value.ScaleY},
		{"skew", value.SkewDegrees},
		{"rotation", value.RotationDegrees},
	});
}

void FontChanger::Structs::from_json(const nlohmann::json& json, TransformStruct& value) {
	value = {};
	if (!json.is_object())
		return;
	value.ScaleX = json.value<float>("scaleX", 1.f);
	value.ScaleY = json.value<float>("scaleY", 1.f);
	value.SkewDegrees = json.value<float>("skew", 0.f);
	value.RotationDegrees = json.value<float>("rotation", 0.f);
}

void FontChanger::Structs::FontElement::Scale(float factor) {
	// The fonts round pixel values when they use them, so nothing is rounded here and scaling back and forth keeps them.
	Size *= factor;

	RendererSpecific.Empty.Ascent *= factor;
	RendererSpecific.Empty.LineHeight *= factor;

	WrapModifiers.LetterSpacing *= factor;
	WrapModifiers.HorizontalOffset *= factor;
	WrapModifiers.VerticalOffset *= factor;
	if (auto& monospacing = WrapModifiers.Monospacing; monospacing.Unit == FontChanger::FixedSizeFont::monospacing_unit::Pixels) {
		if (monospacing.MinAdvance)
			*monospacing.MinAdvance *= factor;
		if (monospacing.MaxAdvance)
			*monospacing.MaxAdvance *= factor;
	}

	auto& glyphMerge = GlyphMerging.Params;
	if (glyphMerge.TextSize)
		*glyphMerge.TextSize *= factor;
	glyphMerge.TextOffsetX *= factor;
	glyphMerge.TextOffsetY *= factor;
	glyphMerge.LetterSpacing *= factor;
	glyphMerge.LineSpacing *= factor;

	OnFontCreateParametersChange();
}

bool FontChanger::Structs::FontElement::UsesProjectDirectory() const {
	const auto& glyphImages = RendererSpecific.GlyphImages;
	return Source == ElementSourceEnum::GlyphImages
		&& !glyphImages.IsEmbedded()
		&& std::filesystem::path(xivres::util::unicode::convert<std::wstring>(glyphImages.Path)).is_relative();
}

FontChanger::Structs::Font::Font(const Font& r)
	: MergedFont(r.MergedFont)
	, Name(r.Name)
	, PreviewText(r.PreviewText)
	, VerticalAlignment(r.VerticalAlignment) {
	Elements.reserve(r.Elements.size());
	for (const auto& e : r.Elements)
		Elements.emplace_back(std::make_unique<FontElement>(*e));
}

FontChanger::Structs::Font& FontChanger::Structs::Font::operator=(const Font& r) {
	if (this != &r)
		*this = Font(r);
	return *this;
}

FontChanger::Structs::FontSet FontChanger::Structs::FontSet::NewFromTemplateFont(xivres::font_type fontType) {
	FontSet res{};

	if (const auto pcszFmt = FontChanger::FixedSizeFont::get_font_tex_filename_format(fontType)) {
		std::string_view filename(pcszFmt);
		filename = filename.substr(filename.rfind('/') + 1);
		res.TexFilenameFormat = filename;
	} else
		res.TexFilenameFormat = "font{}.tex";

	for (const auto& def : FontChanger::FixedSizeFont::get_fontdata_definition(fontType)) {
		std::string previewText;
		std::vector<std::pair<char32_t, char32_t>> codepoints;
		if (def.Family == "JupiterN") {
			previewText = "123,456,789.000!!!";
			codepoints = { {U'0', U'9'}, {U'!', U'!'}, {U'.', U'.'}, {U',', U','} };
		} else if (def.Family == "Meidinger") {
			previewText = "0123456789?!%+-./";
			codepoints = { {0x0000, 0x10FFFF} };
		} else {
			previewText = GetDefaultPreviewText();
			codepoints = { {0x0000, 0x10FFFF} };
		}

		auto& font = *res.Fonts.emplace_back(std::make_unique<Font>());
		font.Name = def.Name;
		font.PreviewText = previewText;

		auto& element = *font.Elements.emplace_back(std::make_unique<FontElement>());
		element.Size = def.Size;
		element.WrapModifiers = {
			.Codepoints = std::move(codepoints),
		};
		element.Source = ElementSourceEnum::Game;
		element.Lookup = {
			.FamilyName = def.Family,
		};
	}

	return res;
}

void FontChanger::Structs::from_json(const nlohmann::json& json, FontSet& value) {
	if (!json.is_object()) {
		value = {};
		return;
	}

	value.Fonts.clear();
	if (const auto it = FindWithLegacy(json, "fonts", "faces"); it != json.end() && it->is_array()) {
		for (const auto& v : *it)
			value.Fonts.emplace_back(std::make_unique<Font>(v.get<Font>()));
	}
	value.DiscardStep = json.value<int>("discardStep", 1);
	value.SideLength = json.value<int>("sideLength", 4096);
	value.ExpectedTexCount = json.value<int>("expectedTexCount", 1);
	value.TexFilenameFormat = json.value<std::string>("texFilenameFormat", "");
}

void FontChanger::Structs::to_json(nlohmann::json& json, const FontSet& value) {
	json = nlohmann::json::object();
	auto& fonts = *json.emplace("fonts", nlohmann::json::array()).first;
	for (const auto& e : value.Fonts)
		fonts.emplace_back(*e);
	json.emplace("discardStep", value.DiscardStep);
	json.emplace("sideLength", value.SideLength);
	json.emplace("expectedTexCount", value.ExpectedTexCount);
	json.emplace("texFilenameFormat", value.TexFilenameFormat);
}

void FontChanger::Structs::from_json(const nlohmann::json& json, Font& value) {
	if (!json.is_object())
		throw std::runtime_error(std::format("Expected an object, got {}", json.type_name()));

	value.Name = json.value<std::string>("name", "");
	value.Elements.clear();
	if (const auto it = json.find("elements"); it != json.end() && it->is_array()) {
		for (const auto& v : *it)
			value.Elements.emplace_back(std::make_unique<FontElement>(v.get<FontElement>()));
	}
	value.PreviewText = json.value<std::string>("previewText", "");
	value.VerticalAlignment = ValueOf(VerticalAlignmentNames, json, "verticalAlignment", FontChanger::FixedSizeFont::vertical_alignment::Baseline);
}

void FontChanger::Structs::to_json(nlohmann::json& json, const Font& value) {
	json = nlohmann::json::object();
	json.emplace("name", value.Name);
	auto& elements = *json.emplace("elements", nlohmann::json::array()).first;
	for (const auto& e : value.Elements)
		elements.emplace_back(*e);
	json.emplace("previewText", value.PreviewText);
	if (value.VerticalAlignment != FontChanger::FixedSizeFont::vertical_alignment::Baseline)
		json.emplace("verticalAlignment", NameOf(VerticalAlignmentNames, value.VerticalAlignment));
}

void FontChanger::Structs::from_json(const nlohmann::json& json, FontElement& value) {
	if (!json.is_object())
		throw std::runtime_error(std::format("Expected an object, got {}", json.type_name()));

	value.Size = json.value<float>("size", 0.f);
	value.Gamma = json.value<float>("gamma", 1.f);
	if (const auto it = json.find("mergeMode"); it != json.end() && it->is_number_integer()) {
		if (const auto mode = it->get<int>(); mode >= 0 && mode < static_cast<int>(FontChanger::FixedSizeFont::codepoint_merge_mode::Enum_Count_))
			value.MergeMode = static_cast<FontChanger::FixedSizeFont::codepoint_merge_mode>(mode);
	} else if (const auto it = json.find("overwrite"); it != json.end() && it->is_boolean()) {
		value.MergeMode = it->get<bool>() ? FontChanger::FixedSizeFont::codepoint_merge_mode::AddAll : FontChanger::FixedSizeFont::codepoint_merge_mode::AddNew;
	}
	if (const auto it = json.find("wrapModifiers"); it != json.end())
		from_json(*it, value.WrapModifiers);
	else
		value.WrapModifiers = {};
	value.Source = ElementSourceEnum::Empty;
	if (const auto it = FindWithLegacy(json, "source", "renderer"); it != json.end() && it->is_number_integer())
		value.Source = static_cast<ElementSourceEnum>(it->get<int>());
	if (const auto it = json.find("lookup"); it != json.end())
		from_json(*it, value.Lookup);
	else
		value.Lookup = {};
	if (const auto it = json.find("renderSpecific"); it != json.end())
		from_json(*it, value.RendererSpecific);
	else
		value.RendererSpecific = {};

	value.Transform = {};
	if (const auto it = json.find("transform"); it != json.end()) {
		from_json(*it, value.Transform);
	} else if (const auto it2 = json.find("transformationMatrix"); it2 != json.end() && it2->is_array() && it2->size() == 4
		&& std::ranges::all_of(*it2, [](const nlohmann::json& v) { return v.is_number(); })) {
		// Earlier versions stored the renderer's own matrix; an unusable one is taken as identity.
		const TransformStruct::matrix m{it2->at(0).get<float>(), it2->at(1).get<float>(), it2->at(2).get<float>(), it2->at(3).get<float>()};
		const auto finite = std::isfinite(m.M11) && std::isfinite(m.M12) && std::isfinite(m.M21) && std::isfinite(m.M22);
		if (finite && std::abs(m.M11 * m.M22 - m.M12 * m.M21) > 1e-6f)
			value.Transform = FromScreenMatrix(TransformStruct::ScreenToRenderer(value.Source, m));
	}

	if (const auto it = json.find("glyphMerging"); it != json.end())
		from_json(*it, value.GlyphMerging);
	else
		value.GlyphMerging = {};

	value.Auto = {};
	if (const auto it = json.find("auto"); it != json.end() && it->is_array()) {
		for (const auto& field : *it) {
			if (!field.is_string())
				continue;
			const auto name = field.get<std::string>();
			if (name == "size")
				value.Auto.Size = true;
			else if (name == "horizontalOffset")
				value.Auto.HorizontalOffset = true;
			else if (name == "verticalOffset")
				value.Auto.VerticalOffset = true;
			else if (name == "renderMode")
				value.Auto.RenderMode = true;
		}
	}
}

void FontChanger::Structs::to_json(nlohmann::json& json, const FontElement& value) {
	json = nlohmann::json::object();
	json.emplace("size", FloatToJson(value.Size));
	json.emplace("gamma", value.Gamma);
	json.emplace("mergeMode", value.MergeMode);
	json.emplace("wrapModifiers", value.WrapModifiers);
	json.emplace("transform", value.Transform);
	json.emplace("source", static_cast<int>(value.Source));
	json.emplace("lookup", value.Lookup);
	json.emplace("renderSpecific", value.RendererSpecific);
	if (value.GlyphMerging.IsEnabled())
		json.emplace("glyphMerging", value.GlyphMerging);
	if (value.Auto.Any()) {
		auto& fields = *json.emplace("auto", nlohmann::json::array()).first;
		for (const auto& [on, name] : {
			     std::pair{value.Auto.Size, "size"},
			     std::pair{value.Auto.HorizontalOffset, "horizontalOffset"},
			     std::pair{value.Auto.VerticalOffset, "verticalOffset"},
			     std::pair{value.Auto.RenderMode, "renderMode"},
		     }) {
			if (on)
				fields.emplace_back(name);
		}
	}
}

void FontChanger::Structs::to_json(nlohmann::json& json, const GlyphMergingStruct& value) {
	const auto& params = value.Params;
	json = nlohmann::json::object();
	json.emplace("textSize", params.TextSize ? nlohmann::json(*params.TextSize) : nlohmann::json(nullptr));
	json.emplace("fitMode", NameOf(GlyphMergeFitModeNames, params.FitMode));
	json.emplace("textOffset", nlohmann::json::array({params.TextOffsetX, params.TextOffsetY}));
	json.emplace("textTransform", value.TextTransform);
	json.emplace("letterSpacing", params.LetterSpacing);
	json.emplace("lineSpacing", params.LineSpacing);
	json.emplace("lineAlignment", NameOf(GlyphMergeLineAlignmentNames, params.LineAlignment));

	auto mappings = nlohmann::json::array();
	for (const auto& mapping : params.Mappings) {
		auto texts = nlohmann::json::array();
		for (const auto& text : mapping.Texts)
			texts.emplace_back(xivres::util::unicode::convert<std::string>(text));

		auto m = nlohmann::json::object();
		m.emplace("codepoints", xivres::util::unicode::convert<std::string>(mapping.Codepoints));
		m.emplace("texts", std::move(texts));
		m.emplace("shape", NameOf(GlyphMergeShapeNames, mapping.Shape));
		m.emplace("textMode", NameOf(GlyphMergeTextModeNames, mapping.TextMode));
		if (mapping.Shape == glyph_merge_shape::Custom) {
			m.emplace("customPath", mapping.CustomPath);
			if (!mapping.CustomSvg.empty())
				m.emplace("customSvg", mapping.CustomSvg);
			m.emplace("customAdvance", mapping.CustomAdvance);
			if (mapping.CustomTextArea)
				m.emplace("customTextArea", *mapping.CustomTextArea);
		} else if (mapping.Shape == glyph_merge_shape::Glyph && mapping.CustomTextArea) {
			m.emplace("customTextArea", *mapping.CustomTextArea);
		}
		mappings.emplace_back(std::move(m));
	}
	json.emplace("mappings", std::move(mappings));
}

void FontChanger::Structs::from_json(const nlohmann::json& json, GlyphMergingStruct& value) {
	value = {};
	if (!json.is_object())
		return;

	auto& params = value.Params;
	if (const auto it = json.find("textSize"); it != json.end() && it->is_number())
		params.TextSize = it->get<float>();
	params.FitMode = ValueOf(GlyphMergeFitModeNames, json, "fitMode", glyph_merge_fit_mode::CondenseThenShrink);
	if (const auto it = json.find("textOffset"); it != json.end() && it->is_array() && it->size() == 2) {
		params.TextOffsetX = it->at(0).get<float>();
		params.TextOffsetY = it->at(1).get<float>();
	}
	if (const auto it = json.find("textTransform"); it != json.end())
		from_json(*it, value.TextTransform);
	params.LetterSpacing = json.value<float>("letterSpacing", 0.f);
	params.LineSpacing = json.value<float>("lineSpacing", 0.f);
	params.LineAlignment = ValueOf(GlyphMergeLineAlignmentNames, json, "lineAlignment", glyph_merge_line_alignment::Center);

	if (const auto it = json.find("mappings"); it != json.end() && it->is_array()) {
		for (const auto& m : *it) {
			if (!m.is_object())
				continue;
			auto& mapping = params.Mappings.emplace_back();
			mapping.Codepoints = xivres::util::unicode::convert<std::u32string>(m.value<std::string>("codepoints", ""));
			if (const auto texts = m.find("texts"); texts != m.end() && texts->is_array()) {
				for (const auto& text : *texts)
					mapping.Texts.emplace_back(text.is_string() ? xivres::util::unicode::convert<std::u32string>(text.get<std::string>()) : std::u32string());
			}
			mapping.Shape = ValueOf(GlyphMergeShapeNames, m, "shape", glyph_merge_shape::Box);
			mapping.TextMode = ValueOf(GlyphMergeTextModeNames, m, "textMode", glyph_merge_text_mode::Subtract);
			mapping.CustomPath = m.value<std::string>("customPath", "");
			mapping.CustomSvg = m.value<std::string>("customSvg", "");
			mapping.CustomAdvance = m.value<float>("customAdvance", 1000.f);

			if (const auto it = m.find("customTextArea"); it != m.end() && it->is_array() && it->size() == 4
				&& std::ranges::all_of(*it, [](const nlohmann::json& v) { return v.is_number() && std::isfinite(v.get<float>()); })) {
				const auto area = it->get<std::array<float, 4>>();
				if (area[0] < area[2] && area[1] < area[3])
					mapping.CustomTextArea = area;
			}
		}
	}
}

void FontChanger::Structs::from_json(const nlohmann::json& json, RendererSpecificStruct& value) {
	if (!json.is_object())
		throw std::runtime_error(std::format("Expected an object, got {}", json.type_name()));

	if (const auto obj = json.find("empty"); obj != json.end() && obj->is_object()) {
		value.Empty.Ascent = obj->value<float>("ascent", 0.f);
		value.Empty.LineHeight = obj->value<float>("lineHeight", 0.f);
	} else
		value.Empty = {};
	if (const auto obj = json.find("freetype"); obj != json.end() && obj->is_object()) {
		value.FreeType.LoadFlags = 0;
		value.FreeType.LoadFlags |= obj->value<bool>("noHinting", false) ? FT_LOAD_NO_HINTING : 0;
		value.FreeType.LoadFlags |= obj->value<bool>("noBitmap", false) ? FT_LOAD_NO_BITMAP : 0;
		value.FreeType.LoadFlags |= obj->value<bool>("forceAutohint", false) ? FT_LOAD_FORCE_AUTOHINT : 0;
		value.FreeType.LoadFlags |= obj->value<bool>("noAutohint", false) ? FT_LOAD_NO_AUTOHINT : 0;
		value.FreeType.RenderMode = static_cast<FT_Render_Mode>(obj->value<int>("renderMode", FT_RENDER_MODE_LIGHT));
	} else
		value.FreeType = {};
	if (const auto obj = json.find("directwrite"); obj != json.end() && obj->is_object()) {
		value.DirectWrite.RenderMode = static_cast<DWRITE_RENDERING_MODE1>(obj->value<int>("renderMode", DWRITE_RENDERING_MODE1_DEFAULT));
		value.DirectWrite.MeasureMode = static_cast<DWRITE_MEASURING_MODE>(obj->value<int>("measureMode", DWRITE_MEASURING_MODE_GDI_NATURAL));
		value.DirectWrite.GridFitMode = static_cast<DWRITE_GRID_FIT_MODE>(obj->value<int>("gridFitMode", DWRITE_GRID_FIT_MODE_DEFAULT));
	} else
		value.DirectWrite = {};

	value.GlyphImages = {};
	if (const auto obj = json.find("glyphImages"); obj != json.end() && obj->is_object()) {
		auto& glyphImages = value.GlyphImages;
		glyphImages.Path = obj->value<std::string>("path", "");
		for (const auto& [key, v] : {
			     std::pair{"unitsPerEm", &glyphImages.UnitsPerEm},
			     std::pair{"baselineY", &glyphImages.BaselineY},
			     std::pair{"ascent", &glyphImages.Ascent},
			     std::pair{"lineHeight", &glyphImages.LineHeight},
		     }) {
			if (const auto it = obj->find(key); it != obj->end() && it->is_number())
				*v = it->get<float>();
		}
		glyphImages.BitmapCoverage = ValueOf(ImageCoverageModeNames, *obj, "bitmapCoverage", FontChanger::FixedSizeFont::image_coverage_mode::Auto);
		if (const auto embedded = obj->find("embedded"); embedded != obj->end() && embedded->is_object()) {
			if (const auto metadata = embedded->find("metadata"); metadata != embedded->end() && !metadata->is_null())
				glyphImages.EmbeddedMetadata = *metadata;
			if (const auto glyphs = embedded->find("glyphs"); glyphs != embedded->end() && glyphs->is_object()) {
				for (const auto& [key, entry] : glyphs->items()) {
					if (!key.starts_with("U+") || !entry.is_object())
						continue;
					const auto codepoint = static_cast<char32_t>(std::strtoul(key.c_str() + 2, nullptr, 16));
					if (const auto svg = entry.find("svg"); svg != entry.end() && svg->is_string())
						glyphImages.Embedded[codepoint].Svg = std::make_shared<const std::string>(svg->get<std::string>());
					else if (const auto png = entry.find("png"); png != entry.end() && png->is_string())
						glyphImages.Embedded[codepoint].PngBase64 = std::make_shared<const std::string>(png->get<std::string>());
				}
			}
		}
	}
}

void FontChanger::Structs::to_json(nlohmann::json& json, const RendererSpecificStruct& value) {
	json = nlohmann::json::object();
	json.emplace("empty", nlohmann::json::object({
		{"ascent", PixelValueToJson(value.Empty.Ascent)},
		{"lineHeight", PixelValueToJson(value.Empty.LineHeight)},
		}));
	json.emplace("freetype", nlohmann::json::object({
		{"noHinting", !!(value.FreeType.LoadFlags & FT_LOAD_NO_HINTING)},
		{"noBitmap", !!(value.FreeType.LoadFlags & FT_LOAD_NO_BITMAP)},
		{"forceAutohint", !!(value.FreeType.LoadFlags & FT_LOAD_FORCE_AUTOHINT)},
		{"noAutohint", !!(value.FreeType.LoadFlags & FT_LOAD_NO_AUTOHINT)},
		{"renderMode", static_cast<int>(value.FreeType.RenderMode)},
		}));
	json.emplace("directwrite", nlohmann::json::object({
		{"renderMode", static_cast<int>(value.DirectWrite.RenderMode)},
		{"measureMode", static_cast<int>(value.DirectWrite.MeasureMode)},
		{"gridFitMode", static_cast<int>(value.DirectWrite.GridFitMode)},
		}));

	const auto& glyphImages = value.GlyphImages;
	if (!glyphImages.Path.empty() || glyphImages.IsEmbedded()) {
		auto obj = nlohmann::json::object();
		obj.emplace("path", glyphImages.Path);
		for (const auto& [key, v] : {
			     std::pair{"unitsPerEm", glyphImages.UnitsPerEm},
			     std::pair{"baselineY", glyphImages.BaselineY},
			     std::pair{"ascent", glyphImages.Ascent},
			     std::pair{"lineHeight", glyphImages.LineHeight},
		     }) {
			if (v)
				obj.emplace(key, *v);
		}
		obj.emplace("bitmapCoverage", NameOf(ImageCoverageModeNames, glyphImages.BitmapCoverage));
		if (glyphImages.IsEmbedded()) {
			auto glyphs = nlohmann::json::object();
			for (const auto& [codepoint, glyph] : glyphImages.Embedded) {
				auto& entry = glyphs[std::format("U+{:04X}", static_cast<uint32_t>(codepoint))];
				if (glyph.Svg)
					entry["svg"] = *glyph.Svg;
				else if (glyph.PngBase64)
					entry["png"] = *glyph.PngBase64;
			}
			obj.emplace("embedded", nlohmann::json::object({
				{"metadata", glyphImages.EmbeddedMetadata.value_or(nlohmann::json())},
				{"glyphs", std::move(glyphs)},
			}));
		}
		json.emplace("glyphImages", std::move(obj));
	}
}

void FontChanger::FixedSizeFont::from_json(const nlohmann::json& json, wrap_modifiers& value) {
	if (!json.is_object())
		throw std::runtime_error(std::format("Expected an object, got {}", json.type_name()));

	value.Codepoints.clear();
	if (const auto it = json.find("codepoints"); it != json.end() && it->is_array()) {
		for (const auto& v : *it) {
			if (!v.is_array())
				continue;
			switch (v.size()) {
				case 0:
					break;
				case 1:
					value.Codepoints.emplace_back(static_cast<char32_t>(v[0].get<uint32_t>()), static_cast<char32_t>(v[0].get<uint32_t>()));
					break;
				default:
					value.Codepoints.emplace_back(static_cast<char32_t>(v[0].get<uint32_t>()), static_cast<char32_t>(v[1].get<uint32_t>()));
					break;
			}
		}
	}

	value.LetterSpacing = json.value<float>("letterSpacing", 0.f);
	value.HorizontalOffset = json.value<float>("horizontalOffset", 0.f);
	value.VerticalOffset = 0.f;
	if (const auto it = FindWithLegacy(json, "verticalOffset", "baselineShift"); it != json.end() && it->is_number())
		value.VerticalOffset = it->get<float>();

	value.Monospacing = {};
	if (const auto it = json.find("monospacing"); it != json.end() && it->is_object()) {
		auto& m = value.Monospacing;
		if (const auto v = it->find("min"); v != it->end() && v->is_number())
			m.MinAdvance = v->get<float>();
		if (const auto v = it->find("max"); v != it->end() && v->is_number())
			m.MaxAdvance = v->get<float>();

		m.Unit = ValueOf(MonospacingUnitNames, *it, "unit", monospacing_unit::Em);

		if (const auto s = it->value<std::string>("referenceChar", ""); !s.empty()) {
			char32_t c = xivres::util::unicode::UReplacement;
			xivres::util::unicode::decode(c, s.c_str(), s.size());
			if (c != xivres::util::unicode::UReplacement)
				m.ReferenceCharacter = c;
		}

		m.Alignment = ValueOf(MonospacingAlignmentNames, *it, "alignment", monospacing_alignment::CenterAdvance);

		m.DropKerning = it->value<bool>("dropKerning", true);
	}

	value.CodepointReplacements.clear();
	if (const auto it = json.find("codepointReplacements"); it != json.end() && it->is_object()) {
		for (const auto& item : it->items()) {
			auto vs = item.value().get<std::string>();
			char32_t l = xivres::util::unicode::UReplacement;
			char32_t r = xivres::util::unicode::UReplacement;
			xivres::util::unicode::decode(l, item.key().c_str(), item.key().size());
			xivres::util::unicode::decode(r, vs.c_str(), vs.size());
			if (l == xivres::util::unicode::UReplacement || r == xivres::util::unicode::UReplacement)
				continue;

			value.CodepointReplacements.emplace(l, r);
		}
	}
}

void FontChanger::FixedSizeFont::to_json(nlohmann::json& json, const wrap_modifiers& value) {
	json = nlohmann::json::object();
	auto& codepoints = *json.emplace("codepoints", nlohmann::json::array()).first;
	for (const auto& c : value.Codepoints)
		codepoints.emplace_back(nlohmann::json::array({ static_cast<uint32_t>(c.first), static_cast<uint32_t>(c.second) }));
	json.emplace("letterSpacing", PixelValueToJson(value.LetterSpacing));
	json.emplace("horizontalOffset", PixelValueToJson(value.HorizontalOffset));
	json.emplace("verticalOffset", PixelValueToJson(value.VerticalOffset));
	if (const auto& m = value.Monospacing; m.is_enabled()) {
		auto& obj = *json.emplace("monospacing", nlohmann::json::object()).first;
		if (m.MinAdvance)
			obj.emplace("min", *m.MinAdvance);
		if (m.MaxAdvance)
			obj.emplace("max", *m.MaxAdvance);
		obj.emplace("unit", NameOf(MonospacingUnitNames, m.Unit));
		obj.emplace("referenceChar", xivres::util::unicode::convert_from_codepoint<std::string>(m.ReferenceCharacter));
		obj.emplace("alignment", NameOf(MonospacingAlignmentNames, m.Alignment));
		obj.emplace("dropKerning", m.DropKerning);
	}
	auto& codepointReplacements = *json.emplace("codepointReplacements", nlohmann::json::object()).first;
	for (const auto& [from, to] : value.CodepointReplacements)
		codepointReplacements[xivres::util::unicode::convert_from_codepoint<std::string>(from)] = xivres::util::unicode::convert_from_codepoint<std::string>(to);
}

void FontChanger::Structs::from_json(const nlohmann::json& json, LookupStruct& value) {
	if (!json.is_object())
		throw std::runtime_error(std::format("Expected an object, got {}", json.type_name()));

	value.FamilyName.clear();
	if (const auto it = FindWithLegacy(json, "familyName", "name"); it != json.end() && it->is_string())
		value.FamilyName = it->get<std::string>();
	value.Weight = static_cast<DWRITE_FONT_WEIGHT>(json.value<int>("weight", DWRITE_FONT_WEIGHT_NORMAL));
	value.Stretch = static_cast<DWRITE_FONT_STRETCH>(json.value<int>("stretch", DWRITE_FONT_STRETCH_NORMAL));
	value.Style = static_cast<DWRITE_FONT_STYLE>(json.value<int>("style", DWRITE_FONT_STYLE_NORMAL));
	value.Features.clear();
	if (const auto it = json.find("features"); it != json.end() && it->is_array()) {
		for (const auto& [_, v] : it->items()) {
			value.Features.emplace(static_cast<DWRITE_FONT_FEATURE_TAG>(FontChanger::DirectWriteUtil::StringToTag(v.get<std::string>())), 1);
		}
	}

	// Values other than 1 are stored apart, so that older versions read the list and leave features turned off (0) as is.
	if (const auto it = json.find("featureValues"); it != json.end() && it->is_object()) {
		for (const auto& [name, v] : it->items()) {
			if (!v.is_number_unsigned())
				continue;
			value.Features[static_cast<DWRITE_FONT_FEATURE_TAG>(FontChanger::DirectWriteUtil::StringToTag(name))] = v.get<uint32_t>();
		}
	}
	value.LanguageCode = json.value<std::string>("language", "");
	value.Variations.clear();
	if (const auto it = json.find("variations"); it != json.end() && it->is_object()) {
		for (const auto& [tag, v] : it->items()) {
			if (v.is_number())
				value.Variations[tag] = v.get<float>();
		}
	}
	value.Synthesis.reset();
	if (const auto it = json.find("synthesis"); it != json.end() && it->is_object())
		value.Synthesis = SynthesisStruct{.Allow = it->value<bool>("allow", true)};
}

void FontChanger::Structs::to_json(nlohmann::json& json, const LookupStruct& value) {
	json = nlohmann::json::object();
	json.emplace("familyName", value.FamilyName);
	json.emplace("weight", static_cast<int>(value.Weight));
	json.emplace("stretch", static_cast<int>(value.Stretch));
	json.emplace("style", static_cast<int>(value.Style));

	auto features = nlohmann::json::array();
	auto featureValues = nlohmann::json::object();
	for (const auto& [tag, v] : value.Features) {
		const auto name = FontChanger::DirectWriteUtil::TagToString(tag);
		if (v != 0)
			features.emplace_back(name);
		if (v != 1)
			featureValues[name] = v;
	}
	json.emplace("features", features);
	if (!featureValues.empty())
		json.emplace("featureValues", std::move(featureValues));
	if (!value.LanguageCode.empty())
		json.emplace("language", value.LanguageCode);
	if (!value.Variations.empty()) {
		auto variations = nlohmann::json::object();
		for (const auto& [tag, v] : value.Variations)
			variations[tag] = v;
		json.emplace("variations", std::move(variations));
	}
	if (value.Synthesis)
		json.emplace("synthesis", nlohmann::json::object({{"allow", value.Synthesis->Allow}}));
}

void FontChanger::Structs::from_json(const nlohmann::json& json, MultiFontSet& value) {
	if (!json.is_object())
		throw std::runtime_error(std::format("Expected an object, got {}", json.type_name()));

	value.FontSets.clear();
	if (json.contains("fonts") || json.contains("faces")) {
		value.FontSets.emplace_back(std::make_unique<FontSet>(json.get<FontSet>()));
		return;
	}

	if (const auto it = json.find("fontSets"); it != json.end() && it->is_array()) {
		for (const auto& o : *it) {
			value.FontSets.emplace_back(std::make_unique<FontSet>(o.get<FontSet>()));
		}
	}

	value.ExportMapFontLobbyToFont = json.value("exportMapFontLobbyToFont", false);
	value.ExportMapChnAxisToFont = json.value("exportMapChnAxisToFont", false);
	value.ExportMapKrnAxisToFont = json.value("exportMapKrnAxisToFont", false);
	value.ExportMapTcAxisToFont = json.value("exportMapTcAxisToFont", false);
}

void FontChanger::Structs::to_json(nlohmann::json& json, const MultiFontSet& value) {
	json = nlohmann::json::object();
	auto& fontSets = json["fontSets"] = nlohmann::json::array();
	for (const auto& set : value.FontSets) {
		to_json(fontSets.emplace_back(), *set);
	}

	json["exportMapFontLobbyToFont"] = value.ExportMapFontLobbyToFont;
	json["exportMapChnAxisToFont"] = value.ExportMapChnAxisToFont;
	json["exportMapKrnAxisToFont"] = value.ExportMapKrnAxisToFont;
	json["exportMapTcAxisToFont"] = value.ExportMapTcAxisToFont;
}

const char* FontChanger::Structs::GetDefaultPreviewText() {
	return reinterpret_cast<const char*>(
		u8"!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~\r\n"
		u8"0英en-US: _The_ (89) quick brown foxes jump over the [01] lazy dogs.\r\n"
		u8"1日ja-JP: _パングラム_(pangram)で[23]つの字体（フォント）をテストします。\r\n"
		u8"2中zh-CN: _(天)地玄黃_，宇[宙]洪荒。蓋此身髮，4大5常。\r\n"
		u8"3韓ko-KR: 45 _다(람)쥐_ 67 헌 쳇바퀴에 타[고]파.\r\n"
		u8"4露ru-RU: Съешь (ж)е ещё этих мягких 23 [ф]ранцузских булок да 45 выпей чаю.\r\n"
		u8"5泰th-TH: เป็นมนุษย์สุดประเสริฐเลิศคุณค่า\r\n"
		u8"6\r\n"
		u8"7\r\n"
		u8"8\r\n"
		u8"9\r\n"
		u8"\r\n"
		u8"\r\n"
		// Nirmala UI's scripts are left out, as most have too many three-glyph GPOS entries to be usable in the game.
		);
}
