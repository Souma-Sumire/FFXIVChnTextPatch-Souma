#include "pch.h"
#include "FontElementEditorDialog.Internal.h"
#include "FontChanger.Presets/CodepointRanges.h"

namespace {
	// Counts the codepoints of the set in the range.
	size_t CountInRange(const std::set<char32_t>& codepoints, char32_t c1, char32_t c2) {
		return static_cast<size_t>(std::distance(codepoints.lower_bound(c1), codepoints.upper_bound(c2)));
	}
}

INT_PTR App::FontElementEditorDialog::CustomRangeEdit_OnCommand(uint16_t notiCode) {
	if (notiCode != EN_CHANGE)
		return 0;

	std::wstring description;
	for (const auto& [c1, c2] : ParseCustomRangeString()) {
		if (!description.empty())
			description += L", ";
		if (c1 == c2) {
			description += std::format(
				L"U+{:04X} {}",
				static_cast<uint32_t>(c1),
				xivres::util::unicode::represent_codepoint<std::wstring>(c1)
			);
		} else {
			description += std::format(
				L"U+{:04X}~{:04X} {}~{}",
				static_cast<uint32_t>(c1),
				static_cast<uint32_t>(c2),
				xivres::util::unicode::represent_codepoint<std::wstring>(c1),
				xivres::util::unicode::represent_codepoint<std::wstring>(c2)
			);
		}
	}

	Edit_SetText(m_controls->CustomRangePreview, description.data());

	return 0;
}

INT_PTR App::FontElementEditorDialog::CustomRangeAdd_OnCommand(uint16_t notiCode) {
	AddCodepointRanges(ParseCustomRangeString());
	return 0;
}

INT_PTR App::FontElementEditorDialog::CustomRangeSubtract_OnCommand(uint16_t notiCode) {
	RemoveCodepointRanges(ParseCustomRangeString());
	return 0;
}

INT_PTR App::FontElementEditorDialog::CodepointsList_OnCommand(uint16_t notiCode) {
	if (notiCode == LBN_DBLCLK)
		return CodepointsDeleteButton_OnCommand(BN_CLICKED);

	return 0;
}

INT_PTR App::FontElementEditorDialog::CodepointsClearButton_OnCommand(uint16_t notiCode) {
	SetCodepointRanges({});
	return 0;
}

INT_PTR App::FontElementEditorDialog::CodepointsDeleteButton_OnCommand(uint16_t notiCode) {
	std::vector<int> selItems(ListBox_GetSelCount(m_controls->CodepointsList));
	if (selItems.empty())
		return 0;

	ListBox_GetSelItems(m_controls->CodepointsList, static_cast<int>(selItems.size()), selItems.data());
	FontChanger::CodepointRanges::Ranges ranges;
	for (const auto itemIndex : selItems)
		ranges.push_back(m_element.WrapModifiers.Codepoints[itemIndex]);
	RemoveCodepointRanges(ranges);
	return 0;
}

INT_PTR App::FontElementEditorDialog::CodepointsMergeModeCombo_OnCommand(uint16_t notiCode) {
	if (notiCode != CBN_SELCHANGE)
		return 0;

	if (const auto v = static_cast<FontChanger::FixedSizeFont::codepoint_merge_mode>(ComboBox_GetCurSel(m_controls->CodepointsMergeModeCombo));
		v != m_element.MergeMode) {
		m_element.MergeMode = v;
		OnWrappedFontChanged();
	}
	return 0;
}

INT_PTR App::FontElementEditorDialog::UnicodeBlockSearchNameEdit_OnCommand(uint16_t notiCode) {
	if (notiCode != EN_CHANGE)
		return 0;

	RefreshUnicodeBlockSearchResults();
	return 0;
}

INT_PTR App::FontElementEditorDialog::UnicodeBlockSearchShowBlocksWithAnyOfCharactersInput_OnCommand(uint16_t notiCode) {
	RefreshUnicodeBlockSearchResults();
	return 0;
}

INT_PTR App::FontElementEditorDialog::UnicodeBlockSearchResultList_OnCommand(uint16_t notiCode) {
	if (notiCode == LBN_SELCHANGE || notiCode == LBN_SELCANCEL) {
		std::vector<int> selItems(ListBox_GetSelCount(m_controls->UnicodeBlockSearchResultList));
		if (selItems.empty())
			return 0;

		ListBox_GetSelItems(m_controls->UnicodeBlockSearchResultList, static_cast<int>(selItems.size()), selItems.data());

		const auto& codepoints = m_element.GetBaseFont()->all_codepoints();
		std::wstring containingChars;

		containingChars.reserve(8192);

		for (const auto itemIndex : selItems) {
			const auto& block = *reinterpret_cast<xivres::util::unicode::blocks::block_definition*>(ListBox_GetItemData(m_controls->UnicodeBlockSearchResultList, itemIndex));

			for (auto it = codepoints.lower_bound(block.First), it_ = codepoints.upper_bound(block.Last); it != it_; ++it) {
				xivres::util::unicode::represent_codepoint(containingChars, *it);
				if (containingChars.size() >= 8192)
					break;
			}

			if (containingChars.size() >= 8192)
				break;
		}

		Static_SetText(m_controls->UnicodeBlockSearchSelectedPreviewEdit, containingChars.c_str());
	} else if (notiCode == LBN_DBLCLK) {
		return UnicodeBlockSearchAdd_OnCommand(BN_CLICKED);
	}
	return 0;
}

INT_PTR App::FontElementEditorDialog::UnicodeBlockSearchAddAll_OnCommand(uint16_t notiCode) {
	FontChanger::CodepointRanges::Ranges ranges;
	for (int i = 0, i_ = ListBox_GetCount(m_controls->UnicodeBlockSearchResultList); i < i_; i++) {
		const auto& block = *reinterpret_cast<const xivres::util::unicode::blocks::block_definition*>(ListBox_GetItemData(m_controls->UnicodeBlockSearchResultList, i));
		ranges.emplace_back(block.First, block.Last);
	}
	AddCodepointRanges(ranges);
	return 0;
}

INT_PTR App::FontElementEditorDialog::UnicodeBlockSearchAdd_OnCommand(uint16_t notiCode) {
	std::vector<int> selItems(ListBox_GetSelCount(m_controls->UnicodeBlockSearchResultList));
	if (selItems.empty())
		return 0;

	ListBox_GetSelItems(m_controls->UnicodeBlockSearchResultList, static_cast<int>(selItems.size()), selItems.data());

	FontChanger::CodepointRanges::Ranges ranges;
	for (const auto itemIndex : selItems) {
		const auto& block = *reinterpret_cast<const xivres::util::unicode::blocks::block_definition*>(ListBox_GetItemData(m_controls->UnicodeBlockSearchResultList, itemIndex));
		ranges.emplace_back(block.First, block.Last);
	}
	AddCodepointRanges(ranges);
	return 0;
}

INT_PTR App::FontElementEditorDialog::UnicodeBlockSearchSubtract_OnCommand(uint16_t notiCode) {
	std::vector<int> selItems(ListBox_GetSelCount(m_controls->UnicodeBlockSearchResultList));
	if (selItems.empty())
		return 0;

	ListBox_GetSelItems(m_controls->UnicodeBlockSearchResultList, static_cast<int>(selItems.size()), selItems.data());
	
	std::vector<std::pair<char32_t, char32_t>> ranges;
	ranges.reserve(selItems.size());
	for (const auto itemIndex : selItems) {
		const auto& block = *reinterpret_cast<const xivres::util::unicode::blocks::block_definition*>(ListBox_GetItemData(m_controls->UnicodeBlockSearchResultList, itemIndex));
		ranges.emplace_back(block.First, block.Last);
	}

	RemoveCodepointRanges(ranges);

	return 0;
}

std::vector<std::pair<char32_t, char32_t>> App::FontElementEditorDialog::ParseCustomRangeString() {
	return FontChanger::CodepointRanges::Parse(GetWindowString(m_controls->CustomRangeEdit));
}

INT_PTR App::FontElementEditorDialog::PresetRangeScript_OnCommand(bool add, uint16_t notiCode) {
	const auto index = ComboBox_GetCurSel(m_controls->PresetRangeScriptCombo);
	if (index == CB_ERR)
		return 0;

	const auto& ranges = FontChanger::CodepointRanges::GetScriptGroupCodepoints(static_cast<FontChanger::CodepointRanges::ScriptGroup>(ComboBox_GetItemData(m_controls->PresetRangeScriptCombo, index)));
	if (add)
		AddCodepointRanges(ranges);
	else
		RemoveCodepointRanges(ranges);
	return 0;
}

INT_PTR App::FontElementEditorDialog::PresetRangeGameFont_OnCommand(bool add, uint16_t notiCode) {
	const auto index = ComboBox_GetCurSel(m_controls->PresetRangeGameFontCombo);
	if (index == CB_ERR || static_cast<size_t>(index) >= m_gameFontNames.size())
		return 0;

	const auto def = FontChanger::FixedSizeFont::find_fontdata_definition(m_gameFontNames[index]);
	if (!def)
		return 0;

	// The game's font as an element of it would draw it; empty if the game is not found.
	Structs::FontElement gameFont;
	gameFont.Source = Structs::ElementSourceEnum::Game;
	gameFont.Lookup.FamilyName = def->Family;
	gameFont.Size = def->Size;
	const auto ranges = FontChanger::CodepointRanges::FromCodepoints(gameFont.GetBaseFont()->all_codepoints());
	if (add)
		AddCodepointRanges(ranges);
	else
		RemoveCodepointRanges(ranges);
	return 0;
}

void App::FontElementEditorDialog::InitializePresetRangeControls() {
	using FontChanger::CodepointRanges::ScriptGroup;
	ComboBox_ResetContent(m_controls->PresetRangeScriptCombo);
	for (const auto& [group, stringId] : {
		     std::pair{ScriptGroup::NonCjk, IDS_SCRIPTGROUP_NONCJK},
		     std::pair{ScriptGroup::Han, IDS_SCRIPTGROUP_HAN},
		     std::pair{ScriptGroup::Kana, IDS_SCRIPTGROUP_KANA},
		     std::pair{ScriptGroup::Hangul, IDS_SCRIPTGROUP_HANGUL},
	     }) {
		const auto index = ComboBox_AddString(m_controls->PresetRangeScriptCombo, std::wstring(GetStringResource(stringId)).c_str());
		ComboBox_SetItemData(m_controls->PresetRangeScriptCombo, index, static_cast<LPARAM>(group));
	}
	ComboBox_SetCurSel(m_controls->PresetRangeScriptCombo, 0);

	// Each family once, as its sizes have the same characters; the fonts of the lobby are those of the game.
	m_gameFontNames.clear();
	ComboBox_ResetContent(m_controls->PresetRangeGameFontCombo);
	std::set<std::string> families;
	for (const auto fontType : FontChanger::FixedSizeFont::get_font_types()) {
		if (fontType == xivres::font_type::font_lobby)
			continue;
		for (const auto& def : FontChanger::FixedSizeFont::get_fontdata_definition(fontType)) {
			if (!families.insert(def.Family).second)
				continue;
			m_gameFontNames.push_back(def.Name);
			ComboBox_AddString(m_controls->PresetRangeGameFontCombo, xivres::util::unicode::convert<std::wstring>(def.Family).c_str());
		}
	}
	ComboBox_SetCurSel(m_controls->PresetRangeGameFontCombo, 0);
	SendMessageW(m_controls->PresetRangeGameFontCombo, CB_SETDROPPEDWIDTH, MulDiv(120, HIWORD(GetDialogBaseUnits()), 8), 0);
}

void App::FontElementEditorDialog::AddCodepointRanges(const std::vector<std::pair<char32_t, char32_t>>& ranges) {
	auto codepoints = m_element.WrapModifiers.Codepoints;
	codepoints.insert(codepoints.end(), ranges.begin(), ranges.end());
	SetCodepointRanges(FontChanger::CodepointRanges::Subtract(codepoints, {}));
}

void App::FontElementEditorDialog::SetCodepointRanges(std::vector<std::pair<char32_t, char32_t>> ranges) {
	if (ranges == m_element.WrapModifiers.Codepoints)
		return;

	m_element.WrapModifiers.Codepoints = std::move(ranges);
	RefreshCodepointsList();
	OnWrappedFontChanged();
}

void App::FontElementEditorDialog::RefreshCodepointsList() {
	ListBox_ResetContent(m_controls->CodepointsList);
	for (int i = 0, i_ = static_cast<int>(m_element.WrapModifiers.Codepoints.size()); i < i_; i++)
		AddCodepointRangeToListBox(i, m_element.WrapModifiers.Codepoints[i].first, m_element.WrapModifiers.Codepoints[i].second);
}

void App::FontElementEditorDialog::AddCodepointRangeToListBox(int index, char32_t c1, char32_t c2) {
	const auto count = CountInRange(m_element.GetBaseFont()->all_codepoints(), c1, c2);

	const auto block = std::lower_bound(xivres::util::unicode::blocks::all_blocks().begin(), xivres::util::unicode::blocks::all_blocks().end(), c1, [](const auto& l, const auto& r) { return l.First < r; });
	if (block != xivres::util::unicode::blocks::all_blocks().end() && block->First == c1 && block->Last == c2) {
		if (c1 == c2) {
			ListBox_InsertString(m_controls->CodepointsList, index, std::format(
				L"U+{:04X} {} [{}]",
				static_cast<uint32_t>(c1),
				xivres::util::unicode::convert<std::wstring>(block->Name),
				xivres::util::unicode::represent_codepoint<std::wstring>(c1)
			).c_str());
		} else {
			ListBox_InsertString(m_controls->CodepointsList, index, std::format(
				L"U+{:04X}~{:04X} {} ({}) {} ~ {}",
				static_cast<uint32_t>(c1),
				static_cast<uint32_t>(c2),
				xivres::util::unicode::convert<std::wstring>(block->Name),
				count,
				xivres::util::unicode::represent_codepoint<std::wstring>(c1),
				xivres::util::unicode::represent_codepoint<std::wstring>(c2)
			).c_str());
		}
	} else if (c1 == c2) {
		ListBox_InsertString(m_controls->CodepointsList, index, std::format(
			L"U+{:04X} [{}]",
			static_cast<int>(c1),
			xivres::util::unicode::represent_codepoint<std::wstring>(c1)
		).c_str());
	} else {
		ListBox_InsertString(m_controls->CodepointsList, index, std::format(
			L"U+{:04X}~{:04X} ({}) {} ~ {}",
			static_cast<uint32_t>(c1),
			static_cast<uint32_t>(c2),
			count,
			xivres::util::unicode::represent_codepoint<std::wstring>(c1),
			xivres::util::unicode::represent_codepoint<std::wstring>(c2)
		).c_str());
	}
}

void App::FontElementEditorDialog::RemoveCodepointRanges(const std::vector<std::pair<char32_t, char32_t>>& ranges) {
	SetCodepointRanges(FontChanger::CodepointRanges::Subtract(m_element.WrapModifiers.Codepoints, ranges));
}

void App::FontElementEditorDialog::RefreshUnicodeBlockSearchResults() {
	const auto input = xivres::util::unicode::convert<std::string>(GetWindowString(m_controls->UnicodeBlockSearchNameEdit));
	const auto input32 = xivres::util::unicode::convert<std::u32string>(input);
	ListBox_ResetContent(m_controls->UnicodeBlockSearchResultList);

	const auto searchByChar = Button_GetCheck(m_controls->UnicodeBlockSearchShowBlocksWithAnyOfCharactersInput);

	const auto& codepoints = m_element.GetBaseFont()->all_codepoints();
	for (const auto& block : xivres::util::unicode::blocks::all_blocks()) {
		const auto nameView = std::string_view(block.Name);
		const auto it = std::search(nameView.begin(), nameView.end(), input.begin(), input.end(), [](char ch1, char ch2) {
			return std::toupper(ch1) == std::toupper(ch2);
		});
		if (it == nameView.end()) {
			if (searchByChar) {
				auto contains = false;
				for (const auto& c : input32) {
					if (block.First <= c && block.Last >= c) {
						contains = true;
						break;
					}
				}
				if (!contains)
					continue;
			} else
				continue;
		}

		const auto count = CountInRange(codepoints, block.First, block.Last);
		if (!count)
			continue;

		ListBox_AddString(m_controls->UnicodeBlockSearchResultList, std::format(
			L"U+{:04X}~{:04X} {} ({}) {} ~ {}",
			static_cast<uint32_t>(block.First),
			static_cast<uint32_t>(block.Last),
			xivres::util::unicode::convert<std::wstring>(nameView),
			count,
			xivres::util::unicode::represent_codepoint<std::wstring>(block.First),
			xivres::util::unicode::represent_codepoint<std::wstring>(block.Last)
		).c_str());
		ListBox_SetItemData(m_controls->UnicodeBlockSearchResultList, ListBox_GetCount(m_controls->UnicodeBlockSearchResultList) - 1, &block);
	}
}
