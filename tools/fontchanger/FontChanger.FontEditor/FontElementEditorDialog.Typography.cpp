#include "pch.h"
#include "FontElementEditorDialog.Internal.h"
#include "FontChanger.Presets/DirectWriteUtil.h"

#include <harfbuzz/hb-ot.h>

#include "FontChanger.FixedSizeFont/opentype_positioning.h"

using namespace App::FontElementEditorDialogInternal;

std::shared_ptr<hb_face_t> App::FontElementEditorDialogInternal::CreateHarfBuzzFace(const Structs::LookupStruct& lookup) {
	// The tables are read from the font as DirectWrite has it, instead of from a copy of the file.
	IDWriteFontFacePtr face;
	SuccessOrThrow(ElementFonts::ResolveFont(lookup).second->CreateFontFace(&face));
	return {FontChanger::FixedSizeFont::create_harfbuzz_face(face), &hb_face_destroy};
}

std::map<DWRITE_FONT_FEATURE_TAG, uint32_t> App::FontElementEditorDialogInternal::GetFeatureAlternateCounts(hb_face_t* face) {
	std::vector<hb_tag_t> featureTags(hb_ot_layout_table_get_feature_tags(face, HB_OT_TAG_GSUB, 0, nullptr, nullptr));
	auto featureCount = static_cast<unsigned>(featureTags.size());
	hb_ot_layout_table_get_feature_tags(face, HB_OT_TAG_GSUB, 0, &featureCount, featureTags.data());

	const auto glyphs = std::unique_ptr<hb_set_t, decltype(&hb_set_destroy)>(hb_set_create(), &hb_set_destroy);
	std::map<unsigned, uint32_t> lookupAlternateCounts;
	std::map<DWRITE_FONT_FEATURE_TAG, uint32_t> result;
	for (unsigned featureIndex = 0; featureIndex < featureCount; featureIndex++) {
		std::vector<unsigned> lookupIndices(hb_ot_layout_feature_get_lookups(face, HB_OT_TAG_GSUB, featureIndex, 0, nullptr, nullptr));
		auto lookupCount = static_cast<unsigned>(lookupIndices.size());
		hb_ot_layout_feature_get_lookups(face, HB_OT_TAG_GSUB, featureIndex, 0, &lookupCount, lookupIndices.data());

		auto& count = result[static_cast<DWRITE_FONT_FEATURE_TAG>(_byteswap_ulong(featureTags[featureIndex]))];
		for (const auto lookupIndex : lookupIndices) {
			auto [it, inserted] = lookupAlternateCounts.emplace(lookupIndex, 0);
			if (inserted) {
				hb_set_clear(glyphs.get());
				hb_ot_layout_lookup_collect_glyphs(face, HB_OT_TAG_GSUB, lookupIndex, nullptr, glyphs.get(), nullptr, nullptr);
				for (hb_codepoint_t glyph = HB_SET_VALUE_INVALID; hb_set_next(glyphs.get(), &glyph);)
					it->second = (std::max)(it->second, hb_ot_layout_lookup_get_glyph_alternates(face, lookupIndex, glyph, 0, nullptr, nullptr));
			}
			count = (std::max)(count, it->second);
		}
	}
	return result;
}

// A feature is taken as on by default if turning it off changes the lookups that a HarfBuzz shape plan, made without
// any features given, applies to text of a script that the font supports, in the language of the element. This asks
// the shaper itself instead of relying on a list of features that are on by default, and so follows the features that
// HarfBuzz turns on per script, such as 'init' and 'medi' for Arabic. Its limits:
// - A feature whose lookups the font selects for no script and language system in use, such as 'locl' without a
//   matching language, is reported as off, as turning it on or off does not change anything.
// - A feature whose lookups are all also applied by another feature that is on is reported as off, for the same reason.
// - Shape plans also hold 'frac', 'numr', and 'dnom', which shapers apply only to digits around a fraction slash; they
//   are reported as off, as they do not apply to text in general, and cannot to the separate characters of FDT fonts.
// - Feature variations of variable fonts are evaluated at the default instance.
// - DirectWrite decides on its own, and may differ from HarfBuzz for some features or scripts.
// The legacy 'kern' table is applied by default by both shapers where GPOS has no kerning, without being a lookup.
std::set<DWRITE_FONT_FEATURE_TAG> App::FontElementEditorDialogInternal::GetDefaultOnFeatures(hb_face_t* face, const std::string& languageCode) {
	std::set<hb_tag_t> featureTags;
	std::set<hb_script_t> scripts;
	if (const auto script = hb_script_from_string(FontChanger::FixedSizeFont::get_default_script_for_language(languageCode).c_str(), -1); script != HB_SCRIPT_INVALID && script != HB_SCRIPT_UNKNOWN)
		scripts.insert(script);
	for (const auto table : { HB_OT_TAG_GSUB, HB_OT_TAG_GPOS }) {
		std::vector<hb_tag_t> tags(hb_ot_layout_table_get_feature_tags(face, table, 0, nullptr, nullptr));
		auto count = static_cast<unsigned>(tags.size());
		hb_ot_layout_table_get_feature_tags(face, table, 0, &count, tags.data());
		featureTags.insert(tags.begin(), tags.begin() + count);
		for (const auto tag : { HB_TAG('f', 'r', 'a', 'c'), HB_TAG('n', 'u', 'm', 'r'), HB_TAG('d', 'n', 'o', 'm') })
			featureTags.erase(tag);

		tags.resize(hb_ot_layout_table_get_script_tags(face, table, 0, nullptr, nullptr));
		count = static_cast<unsigned>(tags.size());
		hb_ot_layout_table_get_script_tags(face, table, 0, &count, tags.data());
		for (unsigned i = 0; i < count; i++) {
			// 'DFLT' maps to no script.
			if (const auto script = hb_ot_tag_to_script(tags[i]); script != HB_SCRIPT_INVALID && script != HB_SCRIPT_UNKNOWN)
				scripts.insert(script);
		}
	}

	std::set<DWRITE_FONT_FEATURE_TAG> result;
	if (const auto kern = std::unique_ptr<hb_blob_t, decltype(&hb_blob_destroy)>(hb_face_reference_table(face, HB_TAG('k', 'e', 'r', 'n')), &hb_blob_destroy); hb_blob_get_length(kern.get()))
		result.insert(DWRITE_FONT_FEATURE_TAG_KERNING);

	const auto hbLanguage = languageCode.empty() ? HB_LANGUAGE_INVALID : hb_language_from_string(languageCode.c_str(), static_cast<int>(languageCode.size()));
	const char* const shapers[]{ "ot", nullptr };
	using set_ptr = std::unique_ptr<hb_set_t, decltype(&hb_set_destroy)>;
	for (const auto script : scripts) {
		hb_segment_properties_t props = HB_SEGMENT_PROPERTIES_DEFAULT;
		props.direction = hb_script_get_horizontal_direction(script);
		if (props.direction == HB_DIRECTION_INVALID)
			props.direction = HB_DIRECTION_LTR;
		props.script = script;
		props.language = hbLanguage;

		const auto collect = [&](const hb_feature_t* features, unsigned featureCount) {
			std::pair res{ set_ptr(hb_set_create(), &hb_set_destroy), set_ptr(hb_set_create(), &hb_set_destroy) };
			const auto plan = std::unique_ptr<hb_shape_plan_t, decltype(&hb_shape_plan_destroy)>(
				hb_shape_plan_create(face, &props, features, featureCount, shapers),
				&hb_shape_plan_destroy);
			hb_ot_shape_plan_collect_lookups(plan.get(), HB_OT_TAG_GSUB, res.first.get());
			hb_ot_shape_plan_collect_lookups(plan.get(), HB_OT_TAG_GPOS, res.second.get());
			return res;
		};

		const auto [defaultGsub, defaultGpos] = collect(nullptr, 0);
		if (hb_set_is_empty(defaultGsub.get()) && hb_set_is_empty(defaultGpos.get()))
			continue;

		for (const auto tag : featureTags) {
			const auto dwriteTag = static_cast<DWRITE_FONT_FEATURE_TAG>(_byteswap_ulong(tag));
			if (result.contains(dwriteTag))
				continue;

			const hb_feature_t off{ tag, 0, HB_FEATURE_GLOBAL_START, HB_FEATURE_GLOBAL_END };
			const auto [gsub, gpos] = collect(&off, 1);
			if (!hb_set_is_equal(gsub.get(), defaultGsub.get()) || !hb_set_is_equal(gpos.get(), defaultGpos.get()))
				result.insert(dwriteTag);
		}
	}
	return result;
}

INT_PTR App::FontElementEditorDialog::FontFeaturesList_OnItemChanged(const NMLISTVIEW& nmlv) {
	if ((nmlv.uChanged & LVIF_STATE) && ((nmlv.uNewState ^ nmlv.uOldState) & LVIS_SELECTED))
		RefreshFontFeatureValueCombo();
	return 0;
}

INT_PTR App::FontElementEditorDialog::FontFeaturesList_OnKeyDown(const NMLVKEYDOWN& nmkd) {
	const auto index = GetSelectedFontFeature();
	if (index < 0)
		return 0;

	switch (nmkd.wVKey) {
		case VK_SPACE:
			CycleFontFeatureValue(index);
			return 0;
		case VK_DELETE:
		case VK_BACK:
			SetFontFeatureValue(index, std::nullopt);
			return 0;
	}
	return 0;
}

INT_PTR App::FontElementEditorDialog::FontFeaturesList_OnDblClick(const NMITEMACTIVATE& nmia) {
	if (nmia.iItem < 0 || nmia.iItem >= static_cast<int>(m_features.size()))
		return 0;

	CycleFontFeatureValue(nmia.iItem);
	return 0;
}

void App::FontElementEditorDialog::CycleFontFeatureValue(int index) {
	// Cycles through all the choices, from the default back to the default.
	const auto choices = GetFontFeatureChoices(index);
	const auto it = std::ranges::find(choices, GetFontFeatureValue(index));
	SetFontFeatureValue(index, it == choices.end() || it + 1 == choices.end() ? choices.front() : *(it + 1));
}

INT_PTR App::FontElementEditorDialog::FontFeaturesList_OnCustomDraw(const NMLVCUSTOMDRAW& nmcd) {
	switch (nmcd.nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;
		case CDDS_ITEMPREPAINT:
			return CDRF_NOTIFYSUBITEMDRAW;
		case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
			// Values left at the default are dimmed, unless the row is highlighted.
			auto& mutableNmcd = const_cast<NMLVCUSTOMDRAW&>(nmcd);
			const auto index = static_cast<int>(nmcd.nmcd.dwItemSpec);
			mutableNmcd.clrText = CLR_DEFAULT;
			if (nmcd.iSubItem == 1
				&& index >= 0 && index < static_cast<int>(m_features.size())
				&& !(ListView_GetItemState(m_controls->FontFeaturesList, index, LVIS_SELECTED) & LVIS_SELECTED)
				&& !GetFontFeatureValue(index))
				mutableNmcd.clrText = GetSysColor(COLOR_GRAYTEXT);
			return CDRF_DODEFAULT;
		}
	}
	return CDRF_DODEFAULT;
}

INT_PTR App::FontElementEditorDialog::FontFeaturesList_OnContextMenu(POINT screenPos) {
	auto index = GetSelectedFontFeature();
	if (screenPos.x == -1 && screenPos.y == -1) {
		// Opened from the keyboard: place the menu at the selected row.
		if (index < 0)
			return 0;
		RECT rc;
		ListView_GetItemRect(m_controls->FontFeaturesList, index, &rc, LVIR_LABEL);
		screenPos = { rc.left, rc.bottom };
		ClientToScreen(m_controls->FontFeaturesList, &screenPos);
	} else {
		LVHITTESTINFO hti{ .pt = screenPos };
		ScreenToClient(m_controls->FontFeaturesList, &hti.pt);
		index = ListView_HitTest(m_controls->FontFeaturesList, &hti);
		if (index < 0 || index >= static_cast<int>(m_features.size()))
			return 0;
		ListView_SetItemState(m_controls->FontFeaturesList, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	}
	if (!IsWindowEnabled(m_controls->FontFeaturesList))
		return 0;

	const auto choices = GetFontFeatureChoices(index);
	const auto current = GetFontFeatureValue(index);
	const auto menu = std::unique_ptr<std::remove_pointer_t<HMENU>, decltype(&DestroyMenu)>(CreatePopupMenu(), &DestroyMenu);
	for (size_t i = 0; i < choices.size(); i++) {
		// Menu commands are returned directly, so they need no IDs of their own; 0 means that nothing was chosen.
		const auto text = GetFontFeatureChoiceText(index, choices[i]);
		AppendMenuW(menu.get(), MF_STRING | (choices[i] == current ? MF_CHECKED : 0), i + 1, text.c_str());

		// Alternates other than the first follow the choices that every feature has.
		if (i == 2 && choices.size() > 3)
			AppendMenuW(menu.get(), MF_SEPARATOR, 0, nullptr);
	}

	if (const auto chosen = TrackPopupMenu(menu.get(), TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, screenPos.x, screenPos.y, 0, m_controls->Window, nullptr); chosen > 0)
		SetFontFeatureValue(index, choices[chosen - 1]);
	return 0;
}

INT_PTR App::FontElementEditorDialog::FontFeatureValueCombo_OnCommand(uint16_t notiCode) {
	if (m_bSettingFeatureValueCombo)
		return 0;

	const auto index = GetSelectedFontFeature();
	if (index < 0)
		return 0;

	switch (notiCode) {
		case CBN_SELCHANGE: {
			const auto sel = ComboBox_GetCurSel(m_controls->FontFeatureValueCombo);
			if (const auto choices = GetFontFeatureChoices(index); sel >= 0 && sel < static_cast<int>(choices.size()))
				SetFontFeatureValue(index, choices[sel], false);
			return 0;
		}

		case CBN_EDITCHANGE: {
			// Keep the last valid value while the text is being typed.
			const auto str = GetWindowString(m_controls->FontFeatureValueCombo, true);
			const auto choices = GetFontFeatureChoices(index);
			for (const auto& choice : choices) {
				if (CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE, str.c_str(), -1, GetFontFeatureChoiceText(index, choice).c_str(), -1, nullptr, nullptr, 0) == CSTR_EQUAL) {
					SetFontFeatureValue(index, choice, false);
					return 0;
				}
			}

			wchar_t* end;
			const auto parsed = std::wcstoul(str.c_str(), &end, 10);
			if (end == str.c_str() || *end || parsed > (std::max)(1u, m_features[index].Alternates))
				return 0;
			SetFontFeatureValue(index, static_cast<uint32_t>(parsed), false);
			return 0;
		}

		case CBN_KILLFOCUS:
			// Show the value as one of the choices once typing is done.
			RefreshFontFeatureValueCombo();
			return 0;
	}
	return 0;
}

INT_PTR App::FontElementEditorDialog::FontLanguageCombo_OnCommand(uint16_t notiCode) {
	if (notiCode != CBN_SELCHANGE)
		return 0;

	const auto index = ComboBox_GetCurSel(m_controls->FontLanguageCombo);
	if (index < 0 || index >= static_cast<int>(m_languageTags.size()))
		return 0;

	if (m_languageTags[index] != m_element.Lookup.LanguageCode) {
		m_element.Lookup.LanguageCode = m_languageTags[index];
		RefreshFontFeatureDefaults();
		OnBaseFontChanged();
	}
	return 0;
}

INT_PTR App::FontElementEditorDialog::FontVariationsList_OnItemChanged(const NMLISTVIEW& nmlv) {
	if ((nmlv.uChanged & LVIF_STATE) && ((nmlv.uNewState ^ nmlv.uOldState) & LVIS_SELECTED))
		RefreshFontVariationValueEdit();
	return 0;
}

INT_PTR App::FontElementEditorDialog::FontVariationValueEdit_OnCommand(uint16_t notiCode) {
	if (notiCode != EN_CHANGE || m_bSettingVariationValueEdit)
		return 0;

	const auto axisIndex = GetSelectedFontVariationAxis();
	if (axisIndex < 0)
		return 0;

	const auto tag = FontChanger::DirectWriteUtil::TagToString(m_variationAxes[axisIndex].Tag);

	auto changed = false;
	if (const auto str = GetWindowString(m_controls->FontVariationValueEdit, true); str.empty()) {
		// An empty value leaves the axis at the value of the selected instance.
		changed = m_element.Lookup.Variations.erase(tag) != 0;
	} else {
		// Keep the last valid value while the text is being typed.
		wchar_t* end;
		const auto value = std::wcstof(str.c_str(), &end);
		if (end == str.c_str() || *end)
			return 0;

		if (const auto it = m_element.Lookup.Variations.find(tag); it == m_element.Lookup.Variations.end() || it->second != value) {  // NOLINT(clang-diagnostic-float-equal)
			m_element.Lookup.Variations[tag] = value;
			changed = true;
		}
	}

	if (changed) {
		UpdateFontVariationsListItem(axisIndex);
		OnBaseFontChanged();
	}
	return 0;
}

void App::FontElementEditorDialog::RefreshFontFeatureDefaults() {
	std::set<DWRITE_FONT_FEATURE_TAG> defaultOn;
	try {
		defaultOn = GetDefaultOnFeatures(CreateHarfBuzzFace(m_element.Lookup).get(), m_element.Lookup.LanguageCode);
	} catch (...) {
		// Show every feature as off by default if the font cannot be read.
	}

	for (int i = 0; i < static_cast<int>(m_features.size()); i++) {
		m_features[i].DefaultOn = defaultOn.contains(m_features[i].Tag);
		UpdateFontFeaturesListItem(i);
	}
	RefreshFontFeatureValueCombo();
}

void App::FontElementEditorDialog::UpdateFontFeaturesListItem(int index) {
	const auto& feature = m_features[index];
	const auto value = GetFontFeatureValue(index);

	std::wstring text;
	if (!value)
		text = GetStringResource(feature.DefaultOn ? IDS_FONTFEATURES_VALUE_DEFAULTON : IDS_FONTFEATURES_VALUE_DEFAULTOFF);
	else if (*value == 0)
		text = GetStringResource(IDS_FONTFEATURES_VALUE_OFF);
	else if (feature.Alternates <= 1)
		text = GetStringResource(IDS_FONTFEATURES_VALUE_ON);
	else
		text = std::format(L"{}", *value);
	if (feature.Alternates > 1)
		text += std::format(L" [1\u2013{}]", feature.Alternates);
	ListView_SetItemText(m_controls->FontFeaturesList, index, 1, text.data());
}

int App::FontElementEditorDialog::GetSelectedFontFeature() const {
	const auto index = ListView_GetNextItem(m_controls->FontFeaturesList, -1, LVNI_SELECTED);
	return index >= 0 && index < static_cast<int>(m_features.size()) ? index : -1;
}

std::vector<std::optional<uint32_t>> App::FontElementEditorDialog::GetFontFeatureChoices(int index) const {
	std::vector<std::optional<uint32_t>> choices{ std::nullopt, 1u, 0u };
	for (uint32_t i = 2; i <= m_features[index].Alternates; i++)
		choices.emplace_back(i);
	return choices;
}

std::wstring App::FontElementEditorDialog::GetFontFeatureChoiceText(int index, std::optional<uint32_t> value) const {
	if (!value)
		return std::wstring(GetStringResource(m_features[index].DefaultOn ? IDS_FONTFEATURES_CHOICE_DEFAULTON : IDS_FONTFEATURES_CHOICE_DEFAULTOFF));
	if (*value == 0)
		return std::wstring(GetStringResource(IDS_FONTFEATURES_VALUE_OFF));
	if (*value == 1) {
		// Turning on a feature with alternates picks the first one.
		std::wstring text(GetStringResource(IDS_FONTFEATURES_VALUE_ON));
		return m_features[index].Alternates > 1 ? std::format(L"{} (1)", text) : text;
	}
	return std::format(L"{}", *value);
}

std::optional<uint32_t> App::FontElementEditorDialog::GetFontFeatureValue(int index) const {
	if (const auto it = m_element.Lookup.Features.find(m_features[index].Tag); it != m_element.Lookup.Features.end())
		return it->second;
	return std::nullopt;
}

void App::FontElementEditorDialog::SetFontFeatureValue(int index, std::optional<uint32_t> value, bool refreshCombo) {
	if (GetFontFeatureValue(index) == value)
		return;

	if (value)
		m_element.Lookup.Features[m_features[index].Tag] = *value;
	else
		m_element.Lookup.Features.erase(m_features[index].Tag);
	UpdateFontFeaturesListItem(index);
	if (refreshCombo && index == GetSelectedFontFeature())
		RefreshFontFeatureValueCombo();
	OnBaseFontChanged();
}

void App::FontElementEditorDialog::RefreshFontFeatureValueCombo() {
	const auto index = GetSelectedFontFeature();

	m_bSettingFeatureValueCombo = true;
	ComboBox_ResetContent(m_controls->FontFeatureValueCombo);
	if (index >= 0) {
		const auto current = GetFontFeatureValue(index);
		const auto choices = GetFontFeatureChoices(index);
		for (const auto& choice : choices) {
			const auto item = ComboBox_AddString(m_controls->FontFeatureValueCombo, GetFontFeatureChoiceText(index, choice).c_str());
			if (choice == current)
				ComboBox_SetCurSel(m_controls->FontFeatureValueCombo, item);
		}

		// Values beyond the alternates that the font has are kept as they are.
		if (std::ranges::find(choices, current) == choices.end())
			ComboBox_SetText(m_controls->FontFeatureValueCombo, std::format(L"{}", current.value_or(0)).c_str());
	}
	m_bSettingFeatureValueCombo = false;

	EnableWindow(m_controls->FontFeatureValueCombo, index >= 0 && DrawsFontFiles(m_element.Source));
}

void App::FontElementEditorDialog::RepopulateFontLanguageCombobox() {
	// Languages whose text is commonly shaped differently by fonts, such as with the 'locl' feature.
	static constexpr const char* Languages[]{
		"ja", "ko", "zh-Hans", "zh-Hant", "zh-HK",
		"az", "bg", "ca", "cs", "de", "el", "en", "es", "fr", "hr", "hu", "it", "mk", "nl", "pl", "pt", "ro", "ru", "sk", "sl", "sr", "tr", "uk", "vi",
	};

	m_languageTags.clear();
	m_languageTags.emplace_back();
	m_languageTags.insert(m_languageTags.end(), std::begin(Languages), std::end(Languages));
	if (!m_element.Lookup.LanguageCode.empty() && std::ranges::find(m_languageTags, m_element.Lookup.LanguageCode) == m_languageTags.end())
		m_languageTags.emplace_back(m_element.Lookup.LanguageCode);

	ComboBox_ResetContent(m_controls->FontLanguageCombo);
	for (const auto& tag : m_languageTags) {
		std::wstring text;
		if (tag.empty()) {
			text = GetStringResource(IDS_FONTLANGUAGE_UNSPECIFIED);
		} else {
			const auto tagW = xivres::util::unicode::convert<std::wstring>(tag);
			wchar_t displayName[LOCALE_NAME_MAX_LENGTH * 4]{};
			if (GetLocaleInfoEx(tagW.c_str(), LOCALE_SLOCALIZEDDISPLAYNAME, displayName, static_cast<int>(std::size(displayName))))
				text = std::format(L"{}: {}", tagW, displayName);
			else
				text = tagW;
		}
		ComboBox_AddString(m_controls->FontLanguageCombo, text.c_str());
		if (tag == m_element.Lookup.LanguageCode)
			ComboBox_SetCurSel(m_controls->FontLanguageCombo, ComboBox_GetCount(m_controls->FontLanguageCombo) - 1);
	}
}

void App::FontElementEditorDialog::RepopulateFontVariationsList() {
	ListView_DeleteAllItems(m_controls->FontVariationsList);
	m_variationAxes.clear();

	if (DrawsFontFiles(m_element.Source)) {
		try {
			const auto [factory, font] = ElementFonts::ResolveFont(m_element.Lookup);

			IDWriteFontFacePtr face;
			SuccessOrThrow(font->CreateFontFace(&face));

			if (IDWriteFontFace5Ptr face5; SUCCEEDED(face.QueryInterface(decltype(face5)::GetIID(), &face5)) && face5->HasVariations()) {
				IDWriteFontResourcePtr resource;
				SuccessOrThrow(face5->GetFontResource(&resource));

				std::vector<DWRITE_FONT_AXIS_RANGE> ranges(resource->GetFontAxisCount());
				SuccessOrThrow(resource->GetFontAxisRanges(ranges.data(), static_cast<UINT32>(ranges.size())));

				std::vector<DWRITE_FONT_AXIS_VALUE> instanceValues(face5->GetFontAxisValueCount());
				SuccessOrThrow(face5->GetFontAxisValues(instanceValues.data(), static_cast<UINT32>(instanceValues.size())));

				for (UINT32 i = 0; i < ranges.size(); i++) {
					// Axes such as 'ital' of Bahnschrift are listed with a single value, and cannot be changed.
					if (!(resource->GetFontAxisAttributes(i) & DWRITE_FONT_AXIS_ATTRIBUTES_VARIABLE) || ranges[i].minValue >= ranges[i].maxValue)
						continue;

					auto& axis = m_variationAxes.emplace_back(VariationAxis{
						.Tag = static_cast<uint32_t>(ranges[i].axisTag),
						.Minimum = ranges[i].minValue,
						.Maximum = ranges[i].maxValue,
						.InstanceValue = ranges[i].minValue,
					});

					for (const auto& v : instanceValues) {
						if (v.axisTag == ranges[i].axisTag)
							axis.InstanceValue = v.value;
					}

					if (IDWriteLocalizedStringsPtr names; SUCCEEDED(resource->GetAxisNames(i, &names)))
						axis.Name = FontChanger::DirectWriteUtil::GetLocalizedString(names, {g_localeName.c_str(), L"en-us"});
				}
			}

			// Values of axes that the font does not have, or cannot vary, would be ignored, and cannot be seen or edited from here.
			std::erase_if(m_element.Lookup.Variations, [this](const auto& kv) {
				return kv.first.size() != 4 || std::ranges::none_of(m_variationAxes, [&kv](const VariationAxis& axis) {
					return FontChanger::DirectWriteUtil::TagToString(axis.Tag) == kv.first;
				});
			});
		} catch (...) {
			// Leave the list empty if the font cannot be resolved.
		}
	}

	for (int i = 0; i < static_cast<int>(m_variationAxes.size()); i++) {
		const auto& axis = m_variationAxes[i];
		const auto tag = xivres::util::unicode::convert<std::wstring>(FontChanger::DirectWriteUtil::TagToString(axis.Tag));
		auto name = axis.Name.empty() ? tag : std::format(L"{} ({})", axis.Name, tag);
		auto range = std::format(L"{:g}\u2013{:g}", axis.Minimum, axis.Maximum);

		LVITEMW lvi{
			.mask = LVIF_TEXT,
			.iItem = i,
			.pszText = name.data(),
		};
		ListView_InsertItem(m_controls->FontVariationsList, &lvi);
		ListView_SetItemText(m_controls->FontVariationsList, i, 1, range.data());
		UpdateFontVariationsListItem(i);
	}

	if (DrawsFontFiles(m_element.Source))
		EnableWindow(m_controls->FontVariationsList, !m_variationAxes.empty());
	RefreshFontVariationValueEdit();
}

void App::FontElementEditorDialog::UpdateFontVariationsListItem(int index) {
	const auto& axis = m_variationAxes[index];

	const auto tag = FontChanger::DirectWriteUtil::TagToString(axis.Tag);

	std::wstring text;
	if (const auto it = m_element.Lookup.Variations.find(tag); it != m_element.Lookup.Variations.end())
		text = std::format(L"{:g}", it->second);
	else if (axis.Tag == DWRITE_FONT_AXIS_TAG_OPTICAL_SIZE)
		text = GetStringResource(IDS_FONTVARIATIONS_AUTO);
	else
		text = std::format(L"({:g})", axis.InstanceValue);
	ListView_SetItemText(m_controls->FontVariationsList, index, 2, text.data());
}

int App::FontElementEditorDialog::GetSelectedFontVariationAxis() const {
	const auto index = ListView_GetNextItem(m_controls->FontVariationsList, -1, LVNI_SELECTED);
	return index >= 0 && index < static_cast<int>(m_variationAxes.size()) ? index : -1;
}

void App::FontElementEditorDialog::RefreshFontVariationValueEdit() {
	const auto axisIndex = GetSelectedFontVariationAxis();

	std::wstring text, cue;
	if (axisIndex >= 0) {
		const auto& axis = m_variationAxes[axisIndex];

		const auto tag = FontChanger::DirectWriteUtil::TagToString(axis.Tag);
		if (const auto it = m_element.Lookup.Variations.find(tag); it != m_element.Lookup.Variations.end())
			text = std::format(L"{:g}", it->second);

		// Shown while the edit is empty: the value used when none is entered.
		if (axis.Tag == DWRITE_FONT_AXIS_TAG_OPTICAL_SIZE)
			cue = GetStringResource(IDS_FONTVARIATIONS_AUTO);
		else
			cue = std::format(L"{:g}", axis.InstanceValue);
	}

	m_bSettingVariationValueEdit = true;
	SetWindowTextW(m_controls->FontVariationValueEdit, text.c_str());
	m_bSettingVariationValueEdit = false;
	Edit_SetCueBannerTextFocused(m_controls->FontVariationValueEdit, cue.c_str(), TRUE);

	const auto enabled = axisIndex >= 0 && DrawsFontFiles(m_element.Source);
	EnableWindow(m_controls->FontVariationValueEdit, enabled);
}
