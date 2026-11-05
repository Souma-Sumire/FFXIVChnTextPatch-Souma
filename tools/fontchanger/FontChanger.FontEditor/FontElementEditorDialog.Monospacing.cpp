#include "pch.h"
#include "FontElementEditorDialog.Internal.h"

using namespace App::FontElementEditorDialogInternal;

namespace {
	// Width that an edit shows when monospacing is turned on without a width to start from.
	float DefaultMonospacingWidth(FontChanger::FixedSizeFont::monospacing_unit unit) {
		switch (unit) {
			case FontChanger::FixedSizeFont::monospacing_unit::Pixels: return 8.f;
			case FontChanger::FixedSizeFont::monospacing_unit::Em: return 0.6f;
			case FontChanger::FixedSizeFont::monospacing_unit::ReferenceCharacter:
			default: return 1.f;
		}
	}
}

void App::FontElementEditorDialog::InitializeMonospacingControls() {
	m_bRefreshingMonospacing = true;
	const auto& m = m_element.WrapModifiers.Monospacing;

	if (!m.MinAdvance && !m.MaxAdvance)
		m_monospacingMode = MonospacingMode::Off;
	else if (!m.MaxAdvance)
		m_monospacingMode = MonospacingMode::AtLeast;
	else if (!m.MinAdvance)
		m_monospacingMode = MonospacingMode::AtMost;
	else if (*m.MinAdvance == *m.MaxAdvance)  // NOLINT(clang-diagnostic-float-equal)
		m_monospacingMode = MonospacingMode::Fixed;
	else
		m_monospacingMode = MonospacingMode::Between;

	SetComboboxContent<MonospacingMode>(
		m_controls->MonospacingModeCombo,
		m_monospacingMode,
		{
			std::make_pair(MonospacingMode::Off, IDS_MONOSPACING_MODE_OFF),
			std::make_pair(MonospacingMode::Fixed, IDS_MONOSPACING_MODE_FIXED),
			std::make_pair(MonospacingMode::AtLeast, IDS_MONOSPACING_MODE_ATLEAST),
			std::make_pair(MonospacingMode::AtMost, IDS_MONOSPACING_MODE_ATMOST),
			std::make_pair(MonospacingMode::Between, IDS_MONOSPACING_MODE_BETWEEN),
		});
	SetComboboxContent<FontChanger::FixedSizeFont::monospacing_alignment>(
		m_controls->MonospacingAlignmentCombo,
		m.Alignment,
		{
			std::make_pair(FontChanger::FixedSizeFont::monospacing_alignment::Left, IDS_MONOSPACING_ALIGNMENT_LEFT),
			std::make_pair(FontChanger::FixedSizeFont::monospacing_alignment::CenterAdvance, IDS_MONOSPACING_ALIGNMENT_CENTERADVANCE),
			std::make_pair(FontChanger::FixedSizeFont::monospacing_alignment::CenterInk, IDS_MONOSPACING_ALIGNMENT_CENTERINK),
			std::make_pair(FontChanger::FixedSizeFont::monospacing_alignment::Right, IDS_MONOSPACING_ALIGNMENT_RIGHT),
		});
	SetComboboxContent<FontChanger::FixedSizeFont::monospacing_unit>(
		m_controls->MonospacingUnitCombo,
		m.Unit,
		{
			std::make_pair(FontChanger::FixedSizeFont::monospacing_unit::Pixels, IDS_MONOSPACING_UNIT_PIXELS),
			std::make_pair(FontChanger::FixedSizeFont::monospacing_unit::Em, IDS_MONOSPACING_UNIT_EM),
			std::make_pair(FontChanger::FixedSizeFont::monospacing_unit::ReferenceCharacter, IDS_MONOSPACING_UNIT_REFERENCECHARACTER),
		});

	// The names of the alignments are longer than the combobox is wide.
	SendMessageW(m_controls->MonospacingAlignmentCombo, CB_SETDROPPEDWIDTH, MulDiv(100, LOWORD(GetDialogBaseUnits()), 4), 0);

	// The first edit holds the only width of the modes with one, which is the upper limit for At most.
	const auto first = m.MinAdvance ? *m.MinAdvance : m.MaxAdvance ? *m.MaxAdvance : DefaultMonospacingWidth(m.Unit);
	SetWindowNumber(m_controls->MonospacingMinEdit, first);
	SetWindowNumber(m_controls->MonospacingMaxEdit, m.MaxAdvance ? *m.MaxAdvance : first);
	SetWindowTextW(m_controls->MonospacingReferenceEdit, xivres::util::unicode::convert<std::wstring>(std::u32string(1, m.ReferenceCharacter)).c_str());
	Button_SetCheck(m_controls->MonospacingDropKerningCheck, m.DropKerning ? BST_CHECKED : BST_UNCHECKED);

	m_bRefreshingMonospacing = false;
	SetMonospacingControlsEnabled();
}

void App::FontElementEditorDialog::SetMonospacingControlsEnabled() {
	if (!m_controls)
		return;

	// Empty elements have no glyphs to space.
	const auto available = m_element.Source != Structs::ElementSourceEnum::Empty;
	const auto on = available && m_monospacingMode != MonospacingMode::Off;
	EnableWindow(m_controls->MonospacingModeCombo, available);
	EnableWindow(m_controls->MonospacingAlignmentCombo, on);
	EnableWindow(m_controls->MonospacingMinEdit, on);
	EnableWindow(m_controls->MonospacingMaxEdit, on && m_monospacingMode == MonospacingMode::Between);
	EnableWindow(m_controls->MonospacingUnitCombo, on);
	EnableWindow(m_controls->MonospacingReferenceEdit, on && m_element.WrapModifiers.Monospacing.Unit == FontChanger::FixedSizeFont::monospacing_unit::ReferenceCharacter);
	EnableWindow(m_controls->MonospacingDropKerningCheck, on);
}

void App::FontElementEditorDialog::ApplyMonospacingWidths() {
	auto& m = m_element.WrapModifiers.Monospacing;

	// Widths are kept within what the font data can store; see wrapping_fixed_size_font::MaxMonospacingAdvance.
	const auto unitPixels = FontChanger::FixedSizeFont::wrapping_fixed_size_font::get_monospacing_unit_pixels(*m_element.GetBaseFont(), m.Unit, m.ReferenceCharacter);
	const auto maxWidth = unitPixels && *unitPixels > 0
		? static_cast<float>(FontChanger::FixedSizeFont::wrapping_fixed_size_font::MaxMonospacingAdvance) / *unitPixels
		: (std::numeric_limits<float>::max)();
	const auto read = [&](HWND hwnd, float fallback) {
		auto value = fallback;
		void(TryGetOrEvaluateValueInto(hwnd, value, fallback));
		return std::clamp(value, 0.f, maxWidth);
	};

	const auto first = read(m_controls->MonospacingMinEdit, DefaultMonospacingWidth(m.Unit));
	switch (m_monospacingMode) {
		case MonospacingMode::Off:
			m.MinAdvance.reset();
			m.MaxAdvance.reset();
			break;
		case MonospacingMode::Fixed:
			m.MinAdvance = m.MaxAdvance = first;
			break;
		case MonospacingMode::AtLeast:
			m.MinAdvance = first;
			m.MaxAdvance.reset();
			break;
		case MonospacingMode::AtMost:
			m.MinAdvance.reset();
			m.MaxAdvance = first;
			break;
		case MonospacingMode::Between: {
			const auto second = read(m_controls->MonospacingMaxEdit, first);
			m.MinAdvance = (std::min)(first, second);
			m.MaxAdvance = (std::max)(first, second);
			break;
		}
	}
}

INT_PTR App::FontElementEditorDialog::Monospacing_OnCommand(uint16_t id, uint16_t notiCode) {
	if (m_bRefreshingMonospacing)
		return 0;

	auto& m = m_element.WrapModifiers.Monospacing;
	const auto before = m;

	switch (id) {
		case IDC_COMBO_MONOSPACING_MODE:
			if (notiCode != CBN_SELCHANGE)
				return 0;
			m_monospacingMode = GetComboboxSelData<MonospacingMode>(m_controls->MonospacingModeCombo);
			if (m_monospacingMode == MonospacingMode::Between && GetWindowString(m_controls->MonospacingMaxEdit, true).empty())
				SetWindowTextW(m_controls->MonospacingMaxEdit, GetWindowString(m_controls->MonospacingMinEdit).c_str());
			ApplyMonospacingWidths();
			SetMonospacingControlsEnabled();
			break;

		case IDC_COMBO_MONOSPACING_ALIGNMENT:
			if (notiCode != CBN_SELCHANGE)
				return 0;
			m.Alignment = GetComboboxSelData<FontChanger::FixedSizeFont::monospacing_alignment>(m_controls->MonospacingAlignmentCombo);
			break;

		case IDC_EDIT_MONOSPACING_MIN:
		case IDC_EDIT_MONOSPACING_MAX:
			if (notiCode == EN_KILLFOCUS) {
				// Shows the width that was taken, after it was kept within the limits.
				m_bRefreshingMonospacing = true;
				if (id == IDC_EDIT_MONOSPACING_MIN && (m.MinAdvance || m.MaxAdvance))
					SetWindowNumber(m_controls->MonospacingMinEdit, m_monospacingMode == MonospacingMode::AtMost ? *m.MaxAdvance : *m.MinAdvance);
				else if (id == IDC_EDIT_MONOSPACING_MAX && m_monospacingMode == MonospacingMode::Between)
					SetWindowNumber(m_controls->MonospacingMaxEdit, *m.MaxAdvance);
				m_bRefreshingMonospacing = false;
				return 0;
			}
			if (notiCode != EN_CHANGE)
				return 0;
			ApplyMonospacingWidths();
			break;

		case IDC_COMBO_MONOSPACING_UNIT:
			if (notiCode != CBN_SELCHANGE)
				return 0;
			m.Unit = GetComboboxSelData<FontChanger::FixedSizeFont::monospacing_unit>(m_controls->MonospacingUnitCombo);
			ApplyMonospacingWidths();
			SetMonospacingControlsEnabled();
			break;

		case IDC_EDIT_MONOSPACING_REFERENCE: {
			if (notiCode != EN_CHANGE)
				return 0;
			const auto text = xivres::util::unicode::convert<std::u32string>(GetWindowString(m_controls->MonospacingReferenceEdit));
			if (text.empty())
				return 0;
			m.ReferenceCharacter = text.front();
			ApplyMonospacingWidths();
			break;
		}

		case IDC_CHECK_MONOSPACING_DROPKERNING:
			if (notiCode != BN_CLICKED)
				return 0;
			m.DropKerning = Button_GetCheck(m_controls->MonospacingDropKerningCheck) == BST_CHECKED;
			break;

		default:
			return 0;
	}

	if (m != before)
		OnWrappedFontChanged();
	return 0;
}
