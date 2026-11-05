#include "pch.h"
#include "GlyphFileTools.h"
#include "FontChanger.Presets/GlyphFiles.h"
#include "MainWindow.h"
#include "ProgressDialog.h"
#include "resource.h"

namespace {
	constexpr GUID Guid_IFileDialog_ExportGlyphs{0x5c2fc703, 0x7406, 0x4704, {0x92, 0x12, 0xae, 0x41, 0x1d, 0x4b, 0x74, 0x71}};

	struct ExportJob {
		std::wstring FolderName;
		std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font> Font;
		App::GlyphFileTools::ExportOptions Options;
	};

	// Characters that cannot be in file names are replaced.
	std::wstring SanitizeFileName(std::wstring name) {
		for (auto& c : name) {
			if (c < 0x20 || std::wstring_view(L"<>:\"/\\|?*").find(c) != std::wstring_view::npos)
				c = L'_';
		}
		while (!name.empty() && (name.back() == L' ' || name.back() == L'.'))
			name.pop_back();
		return name.empty() ? L"_" : name;
	}

	LRESULT RunExportJobs(HWND hWnd, const std::vector<ExportJob>& jobs) {
		return TryCatchShowError<App::ProgressDialog::ProgressDialogCancelledError>(hWnd, IDS_ERROR_EXPORTFAILURE_BODY, LRESULT{1}, [&]() -> LRESULT {
			const auto folder = PickFolder(hWnd, Guid_IFileDialog_ExportGlyphs, IDS_WINDOWTITLE_EXPORTGLYPHS);
			if (!folder)
				return 0;

			App::ProgressDialog progressDialog(hWnd, std::wstring(GetStringResource(IDS_WINDOWTITLE_EXPORTGLYPHS)));
			for (size_t i = 0; i < jobs.size(); i++) {
				const auto& job = jobs[i];
				const auto target = jobs.size() == 1 ? *folder : *folder / SanitizeFileName(job.FolderName);
				App::GlyphFileTools::ExportGlyphs(*job.Font, target, job.Options, [&](size_t done, size_t total) {
					progressDialog.ThrowIfCancelled();
					if (done % 64 == 0 || done == total) {
						progressDialog.UpdateStatusMessage(std::vformat(GetStringResource(IDS_EXPORTPROGRESS_GLYPHS), std::make_wformat_args(done, total)));
						progressDialog.UpdateProgress((static_cast<float>(i) + (total ? static_cast<float>(done) / static_cast<float>(total) : 1.f)) / static_cast<float>(jobs.size()));
					}
				});
			}
			return 0;
		});
	}
}

void App::FontEditorWindow::UpdateProjectDirectory() {
	auto directory = m_currentPath.parent_path();
	if (directory != ElementFonts::GetProjectDirectory()) {
		ElementFonts::SetProjectDirectory(std::move(directory));
		ElementFonts::OnProjectDirectoryChange(m_multiFontSet);
	}
}

void App::FontEditorWindow::StartWatchingGlyphFolders() {
	GlyphFileTools::SetChangeNotificationWindow(m_hWnd, GetGlyphFoldersChangedMessage());
}

void App::FontEditorWindow::StopWatchingGlyphFolders() {
	GlyphFileTools::SetChangeNotificationWindow(nullptr, 0);
}

UINT App::FontEditorWindow::GetGlyphFoldersChangedMessage() {
	static const auto s_message = RegisterWindowMessageW(L"FontChanger.FontEditor.GlyphFoldersChanged");
	return s_message;
}

LRESULT App::FontEditorWindow::OnGlyphFoldersChanged() {
	const auto changed = GlyphFileTools::TakeChangedFolders();
	if (changed.empty())
		return 0;

	auto redraw = false;
	for (const auto& fontSet : m_multiFontSet.FontSets) {
		for (const auto& font : fontSet->Fonts) {
			auto fontChanged = false;
			for (const auto& element : font->Elements) {
				const auto& settings = element->RendererSpecific.GlyphImages;
				if (element->Source != Structs::ElementSourceEnum::GlyphImages || settings.IsEmbedded() || settings.Path.empty())
					continue;
				if (std::ranges::find(changed, GlyphFiles::ResolvePath(settings.Path)) == changed.end())
					continue;

				element->OnFontCreateParametersChange();
				fontChanged = true;
				if (font.get() == m_pActiveFont)
					UpdateFontElementListViewItem(*element);
			}
			if (fontChanged) {
				font->OnElementChange();
				redraw |= font.get() == m_pActiveFont;
			}
		}
	}
	if (redraw)
		Window_Redraw();
	return 0;
}

LRESULT App::FontEditorWindow::Menu_Export_Glyphs(bool withAdjustments) {
	if (!m_pActiveFont)
		return 0;

	std::vector<ExportJob> jobs;
	for (const auto i : GetSelectedElementIndices()) {
		const auto& element = *m_pActiveFont->Elements[i];
		if (element.Source == Structs::ElementSourceEnum::Empty)
			continue;

		jobs.push_back({
			.FolderName = std::format(L"{} {}", i + 1, xivres::util::unicode::convert<std::wstring>(element.GetBaseFont()->family_name())),
			.Font = GlyphFileTools::GetElementFontForExport(element, withAdjustments),
			.Options = {
				.WithAdjustments = withAdjustments,
				.Source = GlyphFileTools::DescribeSource(element),
			},
		});
	}
	if (jobs.empty())
		return 0;

	return RunExportJobs(m_hWnd, jobs);
}

LRESULT App::FontEditorWindow::Menu_Export_FontGlyphs() {
	if (!m_pActiveFont)
		return 0;

	return RunExportJobs(m_hWnd, {{
		.FolderName = xivres::util::unicode::convert<std::wstring>(m_pActiveFont->Name),
		.Font = m_pActiveFont->GetMergedFont(),
		.Options = {
			.WithAdjustments = true,
			.Source = GlyphFileTools::DescribeSource(*m_pActiveFont),
		},
	}});
}
