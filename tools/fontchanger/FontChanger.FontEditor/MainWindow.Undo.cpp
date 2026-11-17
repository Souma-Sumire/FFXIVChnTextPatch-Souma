#include "pch.h"
#include "FontChanger.Presets/Structs.h"
#include "FontElementEditorDialog.h"
#include "MainWindow.h"

namespace {
	constexpr size_t MaxUndoSteps = 100;
	constexpr ULONGLONG CoalesceWindowMs = 1500;

	// Replaces the data of the embedded glyphs in the JSON of an element with the addresses of the strings that hold it,
	// which the copies of an element share. The history would keep a copy of the data in each step otherwise.
	void ReplaceEmbeddedGlyphData(nlohmann::json& json, const App::Structs::FontElement& element) {
		const auto& embedded = element.RendererSpecific.GlyphImages.Embedded;
		if (embedded.empty())
			return;

		auto ids = nlohmann::json::array();
		for (const auto& [codepoint, glyph] : embedded) {
			ids.push_back(static_cast<uint32_t>(codepoint));
			ids.push_back(reinterpret_cast<uintptr_t>(glyph.Svg.get()));
			ids.push_back(reinterpret_cast<uintptr_t>(glyph.PngBase64.get()));
		}

		const auto rendererSpecific = json.find("renderSpecific");
		if (rendererSpecific == json.end())
			return;
		const auto glyphImages = rendererSpecific->find("glyphImages");
		if (glyphImages == rendererSpecific->end())
			return;
		if (const auto it = glyphImages->find("embedded"); it != glyphImages->end())
			(*it)["glyphs"] = std::move(ids);
	}

	// Makes the elements and the alignment of the font those of the state.
	// Elements that are in both keep their objects, and those that differ only in how they are wrapped keep their base
	// fonts, so that undoing small changes does not make every font again.
	void RestoreFont(App::Structs::Font& font, const App::Structs::Font& state) {
		struct LiveElement {
			std::unique_ptr<App::Structs::FontElement> Element;
			nlohmann::json Full;
			nlohmann::json Base;
		};
		const auto withoutWrapping = [](nlohmann::json json) {
			json.erase("mergeMode");
			json.erase("wrapModifiers");
			return json;
		};

		std::vector<LiveElement> live;
		for (auto& pElement : font.Elements) {
			nlohmann::json full(*pElement);
			auto base = withoutWrapping(full);
			live.push_back(LiveElement{std::move(pElement), std::move(full), std::move(base)});
		}

		std::vector<nlohmann::json> wanted;
		for (const auto& pElement : state.Elements)
			wanted.emplace_back(*pElement);

		std::vector<std::unique_ptr<App::Structs::FontElement>> elements(state.Elements.size());
		for (size_t i = 0; i < elements.size(); i++) {
			for (auto& candidate : live) {
				if (candidate.Element && candidate.Full == wanted[i]) {
					elements[i] = std::move(candidate.Element);
					break;
				}
			}
		}
		for (size_t i = 0; i < elements.size(); i++) {
			if (elements[i])
				continue;

			const auto base = withoutWrapping(wanted[i]);
			for (auto& candidate : live) {
				if (candidate.Element && candidate.Base == base) {
					elements[i] = std::move(candidate.Element);
					elements[i]->MergeMode = state.Elements[i]->MergeMode;
					elements[i]->WrapModifiers = state.Elements[i]->WrapModifiers;
					elements[i]->OnFontWrappingParametersChange();
					break;
				}
			}
			if (!elements[i])
				elements[i] = std::make_unique<App::Structs::FontElement>(*state.Elements[i]);
		}

		font.Elements = std::move(elements);
		font.VerticalAlignment = state.VerticalAlignment;
		font.OnElementChange();
	}
}

App::FontEditorWindow::UndoScope::UndoScope(FontEditorWindow& window, std::vector<Structs::Font*> fonts, uint64_t coalesceKey)
	: m_window(window)
	, m_fonts(std::move(fonts))
	, m_selectionBefore(window.GetSelectedElementIndices())
	, m_coalesceKey(coalesceKey) {
	m_window.m_nUndoScopeDepth++;
}

App::FontEditorWindow::UndoScope::~UndoScope() {
	// The outermost scope makes the step.
	if (--m_window.m_nUndoScopeDepth)
		return;

	try {
		m_window.Undo_Commit(m_fonts, m_window.m_pActiveFont, std::move(m_selectionBefore), m_window.GetSelectedElementIndices(), m_coalesceKey);
	} catch (...) {
		// Destructors must not throw.
	}
}

std::shared_ptr<const App::FontEditorWindow::FontSnapshot> App::FontEditorWindow::Undo_TakeSnapshot(const Structs::Font& font) {
	auto snapshot = std::make_shared<FontSnapshot>();
	snapshot->State = font;

	// The copy shares the cached fonts, which the history would keep alive.
	snapshot->State.FlushCache();
	snapshot->State.PreviewText.clear();
	snapshot->Json = nlohmann::json(snapshot->State);
	if (const auto elements = snapshot->Json.find("elements"); elements != snapshot->Json.end()) {
		for (size_t i = 0; i < snapshot->State.Elements.size() && i < elements->size(); i++)
			ReplaceEmbeddedGlyphData((*elements)[i], *snapshot->State.Elements[i]);
	}
	return snapshot;
}

uint64_t App::FontEditorWindow::Undo_MakeCoalesceKey(CoalesceGroup group, const void* subject, const std::vector<int>& indices) {
	auto key = 0xcbf29ce484222325ull;
	const auto mix = [&key](uint64_t value) {
		key = (key ^ value) * 0x100000001b3ull;
	};
	mix(static_cast<uint64_t>(group));
	mix(reinterpret_cast<uintptr_t>(subject));
	for (const auto index : indices)
		mix(static_cast<uint64_t>(index) + 1);
	return key ? key : 1;
}

void App::FontEditorWindow::Undo_Reset() {
	m_undo.clear();
	m_redo.clear();
	m_bCanMergeUndoStep = false;

	m_undoBaselines.clear();
	for (const auto& pFontSet : m_multiFontSet.FontSets) {
		for (const auto& pFont : pFontSet->Fonts)
			m_undoBaselines[pFont.get()] = Undo_TakeSnapshot(*pFont);
	}
}

bool App::FontEditorWindow::Undo_Commit(const std::vector<Structs::Font*>& fonts, Structs::Font* activeFont, std::vector<int> selectionBefore, std::vector<int> selectionAfter, uint64_t coalesceKey) {
	UndoStep step{
		.SelectionBefore = std::move(selectionBefore),
		.SelectionAfter = std::move(selectionAfter),
		.ActiveFont = activeFont,
		.CoalesceKey = coalesceKey,
		.Tick = GetTickCount64(),
	};
	for (const auto pFont : fonts) {
		if (!pFont)
			continue;

		auto& baseline = m_undoBaselines[pFont];
		auto after = Undo_TakeSnapshot(*pFont);
		if (baseline && baseline->Json == after->Json)
			continue;

		if (baseline)
			step.Fonts.push_back({pFont, baseline, after});
		baseline = std::move(after);
	}
	if (step.Fonts.empty())
		return false;

	if (coalesceKey && m_bCanMergeUndoStep && !m_undo.empty() && m_undo.back().CoalesceKey == coalesceKey && step.Tick - m_undo.back().Tick < CoalesceWindowMs) {
		auto& top = m_undo.back();
		for (auto& state : step.Fonts) {
			if (const auto it = std::ranges::find(top.Fonts, state.Target, &UndoStep::FontState::Target); it != top.Fonts.end())
				it->After = std::move(state.After);
			else
				top.Fonts.emplace_back(std::move(state));
		}
		top.SelectionAfter = std::move(step.SelectionAfter);
		top.Tick = step.Tick;

		// A burst that ends where it began is no step.
		std::erase_if(top.Fonts, [](const UndoStep::FontState& state) { return state.Before->Json == state.After->Json; });
		if (top.Fonts.empty()) {
			m_undo.pop_back();
			m_bCanMergeUndoStep = false;
		}
	} else {
		// The saved state was among the steps to redo, which are gone now.
		if (m_savedDepth && *m_savedDepth > m_undo.size())
			m_savedDepth.reset();
		m_redo.clear();

		m_undo.emplace_back(std::move(step));
		if (m_undo.size() > MaxUndoSteps) {
			m_undo.erase(m_undo.begin());
			if (m_savedDepth && *m_savedDepth)
				--*m_savedDepth;
			else
				m_savedDepth.reset();
		}
		m_bCanMergeUndoStep = true;
	}

	Changes_UpdateTitle();
	return true;
}

void App::FontEditorWindow::Undo_Restore(const UndoStep& step, bool redo) {
	for (const auto& state : step.Fonts) {
		const auto& snapshot = redo ? state.After : state.Before;

		// Editors refer to the elements, which may not be there afterwards.
		CloseEditors(*state.Target);
		RestoreFont(*state.Target, snapshot->State);
		m_undoBaselines[state.Target] = snapshot;
	}

	if (step.ActiveFont && step.ActiveFont != m_pActiveFont) {
		for (int i = 0, i_ = ListBox_GetCount(m_hFontsListBox); i < i_; i++) {
			if (reinterpret_cast<Structs::Font*>(ListBox_GetItemData(m_hFontsListBox, i)) != step.ActiveFont)
				continue;

			ListBox_SetCurSel(m_hFontsListBox, i);
			m_pActiveFont = step.ActiveFont;
			m_nPreviewScrollY = 0;
			break;
		}
	}

	UpdateFontElementList();
	if (!m_pActiveFont)
		return;

	{
		const auto tempDisableRedraw = SuppressRedraw(m_hFontElementsListView);

		// Elements that kept their objects, and so their items, may have changed.
		for (const auto& pElement : m_pActiveFont->Elements)
			UpdateFontElementListViewItem(*pElement);

		const auto& selection = redo ? step.SelectionAfter : step.SelectionBefore;
		for (int i = 0, i_ = ListView_GetItemCount(m_hFontElementsListView); i < i_; i++)
			ListView_SetItemState(m_hFontElementsListView, i, std::ranges::binary_search(selection, i) ? LVIS_SELECTED : 0, LVIS_SELECTED);
		if (!selection.empty())
			ListView_EnsureVisible(m_hFontElementsListView, selection.front(), FALSE);
	}

	Window_Redraw();
}

LRESULT App::FontEditorWindow::Menu_Edit_Undo() {
	FontElementsListView_CancelDrag();
	if (m_undo.empty() || m_nUndoScopeDepth)
		return 0;

	auto step = std::move(m_undo.back());
	m_undo.pop_back();
	Undo_Restore(step, false);
	m_redo.emplace_back(std::move(step));

	m_bCanMergeUndoStep = false;
	Changes_UpdateTitle();
	return 0;
}

LRESULT App::FontEditorWindow::Menu_Edit_Redo() {
	FontElementsListView_CancelDrag();
	if (m_redo.empty() || m_nUndoScopeDepth)
		return 0;

	auto step = std::move(m_redo.back());
	m_redo.pop_back();
	Undo_Restore(step, true);
	m_undo.emplace_back(std::move(step));

	m_bCanMergeUndoStep = false;
	Changes_UpdateTitle();
	return 0;
}
