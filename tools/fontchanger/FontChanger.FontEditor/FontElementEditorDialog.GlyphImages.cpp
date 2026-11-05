#include "pch.h"
#include "GlyphFileTools.h"
#include "FontElementEditorDialog.Internal.h"
#include "FontChanger.Presets/GlyphFiles.h"

using namespace App::FontElementEditorDialogInternal;

namespace {
	constexpr GUID Guid_IFileDialog_GlyphImagesFolder{0x5c2fc703, 0x7406, 0x4704, {0x92, 0x12, 0xae, 0x41, 0x1d, 0x4b, 0x74, 0x72}};

	void SetOptionalNumber(HWND hwnd, const std::optional<float>& value) {
		if (value)
			SetWindowNumber(hwnd, *value);
		else
			SetWindowTextW(hwnd, L"");
	}
}

void App::FontElementEditorDialog::InitializeGlyphImagesGroup() {
	// Empty values are taken from the files.
	const auto autoText = std::wstring(GetStringResource(IDS_FONTVARIATIONS_AUTO));
	for (const auto hwnd : {
		     m_controls->GlyphImagesUnitsPerEmEdit,
		     m_controls->GlyphImagesBaselineEdit,
		     m_controls->GlyphImagesAscentEdit,
		     m_controls->GlyphImagesLineHeightEdit,
	     })
		Edit_SetCueBannerTextFocused(hwnd, autoText.c_str(), TRUE);
	RefreshGlyphImagesGroup();
}

void App::FontElementEditorDialog::RefreshGlyphImagesGroup() {
	const auto& settings = m_element.RendererSpecific.GlyphImages;

	m_bRefreshingGlyphImages = true;
	if (GetWindowString(m_controls->GlyphImagesPathEdit) != xivres::util::unicode::convert<std::wstring>(settings.Path))
		SetWindowTextW(m_controls->GlyphImagesPathEdit, xivres::util::unicode::convert<std::wstring>(settings.Path).c_str());
	SetOptionalNumber(m_controls->GlyphImagesUnitsPerEmEdit, settings.UnitsPerEm);
	SetOptionalNumber(m_controls->GlyphImagesBaselineEdit, settings.BaselineY);
	SetOptionalNumber(m_controls->GlyphImagesAscentEdit, settings.Ascent);
	SetOptionalNumber(m_controls->GlyphImagesLineHeightEdit, settings.LineHeight);
	m_bRefreshingGlyphImages = false;

	std::wstring info;
	if (m_element.Source == Structs::ElementSourceEnum::GlyphImages) {
		if (settings.IsEmbedded()) {
			const auto count = settings.Embedded.size();
			info = std::vformat(GetStringResource(IDS_GLYPHIMAGES_INFO_EMBEDDED), std::make_wformat_args(count));
		} else if (const auto set = GlyphFiles::LoadGlyphSet(settings); !set->Error.empty()) {
			info = set->Error;
		} else {
			const auto count = set->Glyphs.size();
			info = std::vformat(GetStringResource(IDS_GLYPHIMAGES_INFO), std::make_wformat_args(count, set->SkippedCount));
		}
	}
	SetWindowTextW(m_controls->GlyphImagesInfoStatic, info.c_str());
	SetGlyphImagesControlsEnabled(m_element.Source == Structs::ElementSourceEnum::GlyphImages);
}

void App::FontElementEditorDialog::SetGlyphImagesControlsEnabled(bool enabled) {
	const auto embedded = m_element.RendererSpecific.GlyphImages.IsEmbedded();
	EnableWindow(m_controls->GlyphImagesPathEdit, enabled && !embedded);
	EnableWindow(m_controls->GlyphImagesBrowseButton, enabled);
	EnableWindow(m_controls->GlyphImagesEmbedButton, enabled && !embedded && !m_element.RendererSpecific.GlyphImages.Path.empty());
	EnableWindow(m_controls->GlyphImagesUnitsPerEmEdit, enabled);
	EnableWindow(m_controls->GlyphImagesBaselineEdit, enabled);
	EnableWindow(m_controls->GlyphImagesAscentEdit, enabled);
	EnableWindow(m_controls->GlyphImagesLineHeightEdit, enabled);
}

INT_PTR App::FontElementEditorDialog::GlyphImages_OnCommand(uint16_t id, uint16_t notiCode) {
	if (notiCode != EN_CHANGE || m_bRefreshingGlyphImages)
		return 0;

	auto& settings = m_element.RendererSpecific.GlyphImages;
	if (id == IDC_EDIT_GLYPHIMAGES_PATH) {
		auto path = xivres::util::unicode::convert<std::string>(GetWindowString(m_controls->GlyphImagesPathEdit, true));
		if (path == settings.Path)
			return 0;
		settings.Path = std::move(path);
	} else {
		const std::array<std::tuple<int, HWND, std::optional<float>*>, 4> fields{{
			{IDC_EDIT_GLYPHIMAGES_UNITSPEREM, m_controls->GlyphImagesUnitsPerEmEdit, &settings.UnitsPerEm},
			{IDC_EDIT_GLYPHIMAGES_BASELINE, m_controls->GlyphImagesBaselineEdit, &settings.BaselineY},
			{IDC_EDIT_GLYPHIMAGES_ASCENT, m_controls->GlyphImagesAscentEdit, &settings.Ascent},
			{IDC_EDIT_GLYPHIMAGES_LINEHEIGHT, m_controls->GlyphImagesLineHeightEdit, &settings.LineHeight},
		}};
		const auto it = std::ranges::find(fields, static_cast<int>(id), [](const auto& f) { return std::get<0>(f); });
		if (it == fields.end())
			return 0;

		const auto& [_, hwnd, field] = *it;
		// Units per em divide the sizes.
		std::optional<float> value;
		if (!TryReadOptionalNumber(hwnd, value) || (id == IDC_EDIT_GLYPHIMAGES_UNITSPEREM && value && !(*value > 0)))
			return 0;
		if (value == *field)
			return 0;
		*field = value;
	}

	OnBaseFontChanged();
	RefreshGlyphImagesGroup();
	return 0;
}

INT_PTR App::FontElementEditorDialog::GlyphImagesBrowseButton_OnCommand(uint16_t notiCode) {
	return TryCatchShowError(m_controls->Window, IDS_ERROR_OPENFILEFAILURE_BODY, INT_PTR{0}, [&]() -> INT_PTR {
		const auto picked = PickFolder(m_controls->Window, Guid_IFileDialog_GlyphImagesFolder, IDS_GLYPHIMAGES_SELECTFOLDER);
		if (!picked)
			return 0;

		// Folders inside the folder of the configuration are kept relative, so that the two can be moved together.
		auto folder = picked->lexically_normal();
		if (const auto base = ElementFonts::GetProjectDirectory(); !base.empty()) {
			if (const auto relative = folder.lexically_relative(base.lexically_normal()); !relative.empty() && *relative.begin() != L"..")
				folder = relative;
		}

		auto& settings = m_element.RendererSpecific.GlyphImages;
		settings.Path = xivres::util::unicode::convert<std::string>(folder.wstring());
		settings.Embedded.clear();
		settings.EmbeddedMetadata.reset();
		GlyphFiles::InvalidateFolder(GlyphFiles::ResolvePath(settings.Path));
		OnBaseFontChanged();
		RefreshGlyphImagesGroup();
		RefreshUnicodeBlockSearchResults();
		return 0;
	});
}

INT_PTR App::FontElementEditorDialog::GlyphImagesEmbedButton_OnCommand(uint16_t notiCode) {
	return TryCatchShowError(m_controls->Window, IDS_ERROR_OPENFILEFAILURE_BODY, INT_PTR{0}, [&]() -> INT_PTR {
		GlyphFileTools::Embed(m_element.RendererSpecific.GlyphImages);
		OnBaseFontChanged();
		RefreshGlyphImagesGroup();
		return 0;
	});
}
