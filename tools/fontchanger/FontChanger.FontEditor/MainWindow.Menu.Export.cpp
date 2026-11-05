#include "pch.h"
#include "FontChanger.Presets/Structs.h"
#include "ExportPreviewWindow.h"
#include "MainWindow.h"
#include "MainWindow.Internal.h"
#include "ProgressDialog.h"
#include "xivres/textools.h"
#include "resource.h"

namespace {
	// Compiles the fonts of the font set into font data and textures.
	App::CompiledFontSet CompileFontSet(App::ProgressDialog& progressDialog, App::Structs::FontSet& fontSet) {
		progressDialog.UpdateStatusMessage(GetStringResource(IDS_EXPORTPROGRESS_LOADFONTS));
		fontSet.ConsolidateFonts();

		progressDialog.UpdateStatusMessage(GetStringResource(IDS_EXPORTPROGRESS_KERNINGPAIRS));
		std::vector<std::string> tooManyKernings;
		for (const auto& pFont : fontSet.Fonts) {
			progressDialog.ThrowIfCancelled();
			if (const auto nKerns = pFont->GetMergedFont()->all_kerning_pairs().size(); nKerns >= 65536)
				tooManyKernings.emplace_back(std::format("\n{}: {}", pFont->Name, nKerns));
		}
		if (!tooManyKernings.empty()) {
			std::ranges::sort(tooManyKernings);
			std::wstring s(GetStringResource(IDS_ERROR_KERNINGTABLETOOLARGE));
			for (const auto& s2 : tooManyKernings)
				s += xivres::util::unicode::convert<std::wstring>(s2);
			throw WException(s);
		}

		FontChanger::FixedSizeFont::fontdata_packer packer;
		packer.set_discard_step(fontSet.DiscardStep);
		packer.set_side_length(fontSet.SideLength);

		for (auto& pFont : fontSet.Fonts)
			packer.add_font(pFont->GetMergedFont());

		packer.compile();

		while (!packer.wait(std::chrono::milliseconds(200))) {
			progressDialog.ThrowIfCancelled();

			switch (packer.progress_description()) {
				case FontChanger::FixedSizeFont::fontdata_packer::progress_status::prepare_source_fonts:
					progressDialog.UpdateStatusMessage(GetStringResource(IDS_COMPILESTATUS_PREPARESOURCEFONTS));
					break;
				case FontChanger::FixedSizeFont::fontdata_packer::progress_status::prepare_target_fonts:
					progressDialog.UpdateStatusMessage(GetStringResource(IDS_COMPILESTATUS_PREPARETARGETFONTS));
					break;
				case FontChanger::FixedSizeFont::fontdata_packer::progress_status::discover_glyphs:
					progressDialog.UpdateStatusMessage(GetStringResource(IDS_COMPILESTATUS_DISCOVERGLYPHS));
					break;
				case FontChanger::FixedSizeFont::fontdata_packer::progress_status::measure_glyphs:
					progressDialog.UpdateStatusMessage(GetStringResource(IDS_COMPILESTATUS_MEASUREGLYPHS));
					break;
				case FontChanger::FixedSizeFont::fontdata_packer::progress_status::layout_and_draw:
					progressDialog.UpdateStatusMessage(GetStringResource(IDS_COMPILESTATUS_LAYOUTANDDRAW));
					break;
			}
			progressDialog.UpdateProgress(packer.progress_scaled());
		}
		if (const auto err = packer.get_error_if_failed(); !err.empty())
			throw std::runtime_error(err);

		App::CompiledFontSet res{packer.compiled_fontdatas(), packer.compiled_mipmap_streams()};
		if (res.Mipmaps.empty())
			throw std::runtime_error("DEBUG: No mipmap produced");
		return res;
	}

	// Gets the file name of a path in the game's file system.
	std::string_view GetFileName(std::string_view path) {
		return path.substr(path.rfind('/') + 1);
	}

	// Gets the files that an export also writes the files of the font set as, as pairs of the file names: those of the
	// game fonts of the enabled export mappings with the same family and size.
	std::vector<std::pair<std::string, std::string>> GetExportAliases(const App::Structs::MultiFontSet& multiFontSet, const App::Structs::FontSet& fontSet, size_t textureCount) {
		std::vector<std::pair<std::string, std::string>> res;
		if (fontSet.TexFilenameFormat != "font{}.tex")
			return res;

		const auto sourceDefs = FontChanger::FixedSizeFont::get_fontdata_definition(xivres::font_type::font);
		for (const auto& mapping : ExportMappings) {
			if (!(multiFontSet.*mapping.Enabled))
				continue;

			// The fonts of the other releases replace only AXIS; the names (families) of their definitions differ.
			for (const auto& target : FontChanger::FixedSizeFont::get_fontdata_definition(mapping.Target)) {
				const auto it = std::ranges::find_if(sourceDefs, [&](const auto& source) {
					return source.Size == target.Size && (mapping.Target == xivres::font_type::font_lobby
						? source.Family == target.Family
						: source.Family == "AXIS");
				});
				if (it == sourceDefs.end())
					continue;

				if (std::ranges::any_of(fontSet.Fonts, [&](const auto& pFont) { return pFont->Name == it->Name; }))
					res.emplace_back(it->Name + ".fdt", target.Name + ".fdt");
			}

			const auto targetFormat = GetFileName(FontChanger::FixedSizeFont::get_font_tex_filename_format(mapping.Target));
			for (size_t i = 1; i <= textureCount; i++)
				res.emplace_back(std::vformat(fontSet.TexFilenameFormat, std::make_format_args(i)), std::vformat(targetFormat, std::make_format_args(i)));
		}
		return res;
	}

	// Glyphs placed in textures past the limit will be read from textures the game never loaded.
	void ThrowIfTooManyTextures(const App::Structs::MultiFontSet& multiFontSet, const App::Structs::FontSet& fontSet, size_t textureCount) {
		std::vector<std::string> targets{fontSet.TexFilenameFormat};
		if (fontSet.TexFilenameFormat == "font{}.tex") {
			for (const auto& mapping : ExportMappings) {
				if (multiFontSet.*mapping.Enabled)
					targets.emplace_back(GetFileName(FontChanger::FixedSizeFont::get_font_tex_filename_format(mapping.Target)));
			}
		}

		for (const auto& target : targets) {
			const auto limit = GetGameTextureCount(target);
			if (limit && textureCount > static_cast<size_t>(*limit)) {
				throw std::runtime_error(std::format(
					"{} texture files are required, but the game loads only up to {} texture files for \"{}\".\n"
					"Reduce the number of glyphs or the font sizes, or increase the texture size.",
					textureCount, *limit, target));
			}
		}
	}

	// When drawing AXIS_36, the game may use the glyph of AXIS_18 at the same position in the glyph table instead,
	// so both must list the same characters in the same order.
	void ThrowIfAxisGlyphTablesMismatch(const App::Structs::FontSet& fontSet, const std::vector<std::shared_ptr<xivres::fontdata::stream>>& fdts) {
		for (const auto& [smallName, largeName] : FontChanger::FixedSizeFont::get_linked_fontdata_names()) {
			const xivres::fontdata::stream* pSmall = nullptr;
			const xivres::fontdata::stream* pLarge = nullptr;
			for (size_t i = 0; i < fdts.size(); i++) {
				if (fontSet.Fonts[i]->Name == smallName)
					pSmall = fdts[i].get();
				else if (fontSet.Fonts[i]->Name == largeName)
					pLarge = fdts[i].get();
			}
			if (!pSmall || !pLarge)
				continue;

			const auto& smallGlyphs = pSmall->get_glyphs();
			const auto& largeGlyphs = pLarge->get_glyphs();
			const auto toUtf8Value = [](const xivres::fontdata::glyph_entry& e) { return *e.Utf8Value; };
			const auto [itSmall, itLarge] = std::ranges::mismatch(smallGlyphs, largeGlyphs, {}, toUtf8Value, toUtf8Value);
			if (itSmall == smallGlyphs.end() && itLarge == largeGlyphs.end())
				continue;

			const auto& [missingFrom, extraIn, codepoint] = itSmall == smallGlyphs.end() || (itLarge != largeGlyphs.end() && *itLarge->Utf8Value < *itSmall->Utf8Value)
				? std::make_tuple(smallName, largeName, itLarge->codepoint())
				: std::make_tuple(largeName, smallName, itSmall->codepoint());
			throw std::runtime_error(std::format(
				"{} and {} must contain the same characters, as the game substitutes glyphs of {} into {} by their position in the glyph table.\n"
				"U+{:04X} is in {} but not in {}.",
				smallName, largeName, smallName, largeName,
				static_cast<uint32_t>(codepoint), extraIn, missingFrom));
		}
	}

	// Throws if the game cannot use the compiled font set as it is.
	void ThrowIfNotUsableByGame(const App::Structs::MultiFontSet& multiFontSet, const App::Structs::FontSet& fontSet, const App::CompiledFontSet& compiled) {
		ThrowIfTooManyTextures(multiFontSet, fontSet, compiled.Mipmaps.size());
		ThrowIfAxisGlyphTablesMismatch(fontSet, compiled.Fdts);
	}
}

App::CompiledFontSet App::FontEditorWindow::CompileCurrentFontSet(ProgressDialog& progressDialog, Structs::FontSet& fontSet) {
	auto res = CompileFontSet(progressDialog, fontSet);
	if (fontSet.ExpectedTexCount != static_cast<int>(res.Mipmaps.size())) {
		fontSet.ExpectedTexCount = static_cast<int>(res.Mipmaps.size());
		Changes_MarkDirty();
	}
	return res;
}

LRESULT App::FontEditorWindow::Menu_Export_Preview() {
	using namespace FontChanger::FixedSizeFont;

	return TryCatchShowError<ProgressDialog::ProgressDialogCancelledError>(m_hWnd, IDS_ERROR_EXPORTFAILURE_BODY, LRESULT{1}, [&]() -> LRESULT {
		ProgressDialog progressDialog(m_hWnd, std::wstring(GetStringResource(IDS_WINDOWTITLE_EXPORTRAW)));
		ShowWindow(m_hWnd, SW_HIDE);
		const auto hideWhilePacking = xivres::util::on_dtor([this] { ShowWindow(m_hWnd, SW_SHOW); });

		std::vector<std::pair<std::string, std::shared_ptr<fixed_size_font>>> resultFonts;
		for (const auto& fontSet : m_multiFontSet.FontSets) {
			const auto [fdts, mips] = CompileCurrentFontSet(progressDialog, *fontSet);

			auto texturesAll = std::make_shared<xivres::texture::stream>(mips[0]->Type, mips[0]->Width, mips[0]->Height, 1, 1, mips.size());
			for (size_t i = 0; i < mips.size(); i++)
				texturesAll->set_mipmap(0, i, mips[i]);

			for (size_t i = 0; i < fdts.size(); i++)
				resultFonts.emplace_back(fontSet->Fonts[i]->Name, std::make_shared<fontdata_fixed_size_font>(fdts[i], mips, fontSet->Fonts[i]->Name, ""));

			std::thread([texturesAll]() { preview(*texturesAll); }).detach();
		}

		ExportPreviewWindow::ShowNew(std::move(resultFonts));
		return 0;
	});
}

LRESULT App::FontEditorWindow::Menu_Export_Raw() {
	return TryCatchShowError<ProgressDialog::ProgressDialogCancelledError>(m_hWnd, IDS_ERROR_EXPORTFAILURE_BODY, LRESULT{1}, [&]() -> LRESULT {
		const auto basePath = PickFolder(m_hWnd, Guid_IFileDialog_Export, IDS_WINDOWTITLE_EXPORTRAW);
		if (!basePath)
			return 0;

		ProgressDialog progressDialog(m_hWnd, std::wstring(GetStringResource(IDS_WINDOWTITLE_EXPORTRAW)));
		ShowWindow(m_hWnd, SW_HIDE);
		const auto hideWhilePacking = xivres::util::on_dtor([this]() { ShowWindow(m_hWnd, SW_SHOW); });

		std::vector<char> buf(32768);
		const auto write = [&](const std::filesystem::path& path, xivres::stream& stream) {
			std::ofstream out(path, std::ios::binary);
			for (size_t read, pos = 0; (read = stream.read(pos, buf.data(), buf.size())); pos += read) {
				progressDialog.ThrowIfCancelled();
				out.write(buf.data(), read);
			}
		};

		for (const auto& pFontSet : m_multiFontSet.FontSets) {
			const auto compiled = CompileCurrentFontSet(progressDialog, *pFontSet);
			ThrowIfNotUsableByGame(m_multiFontSet, *pFontSet, compiled);
			const auto& [fdts, mips] = compiled;

			progressDialog.UpdateProgress(std::nanf(""));
			progressDialog.UpdateStatusMessage(GetStringResource(IDS_EXPORTPROGRESS_WRITINGTOFILES));

			xivres::texture::stream textureOne(mips[0]->Type, mips[0]->Width, mips[0]->Height, 1, 1, 1);
			for (size_t i = 0; i < mips.size(); i++) {
				progressDialog.ThrowIfCancelled();
				textureOne.set_mipmap(0, 0, mips[i]);
				const auto i1 = i + 1;
				write(*basePath / std::vformat(pFontSet->TexFilenameFormat, std::make_format_args(i1)), textureOne);
			}

			for (size_t i = 0; i < fdts.size(); i++) {
				progressDialog.ThrowIfCancelled();
				write(*basePath / std::format("{}.fdt", pFontSet->Fonts[i]->Name), *fdts[i]);
			}

			for (const auto& [source, alias] : GetExportAliases(m_multiFontSet, *pFontSet, mips.size()))
				copy(*basePath / source, *basePath / alias, std::filesystem::copy_options::overwrite_existing);
		}
		return 0;
	});
}

LRESULT App::FontEditorWindow::Menu_Export_TTMP(CompressionMode compressionMode) {
	static constexpr COMDLG_FILTERSPEC fileTypes[] = {
		{L"TTMP2 file (*.ttmp2)", L"*.ttmp2"},
		{L"ZIP file (*.zip)", L"*.zip"},
		{L"All files (*.*)", L"*"},
	};

	return TryCatchShowError<ProgressDialog::ProgressDialogCancelledError>(m_hWnd, IDS_ERROR_EXPORTFAILURE_BODY, LRESULT{1}, [&]() -> LRESULT {
		const auto finalPath = PickFile(
			m_hWnd,
			true,
			Guid_IFileDialog_Export,
			IDS_WINDOWTITLE_EXPORTTTMP,
			fileTypes,
			std::format(L"{}.ttmp2", std::filesystem::path(GetCurrentFileName()).replace_extension(L"").wstring()),
			L"json");
		if (!finalPath)
			return 0;

		xivres::textools::simple_ttmp2_writer writer(*finalPath);

		writer.ttmpl().Name = xivres::util::unicode::convert<std::string>(std::filesystem::path(GetCurrentFileName()).stem().wstring());

		ProgressDialog progressDialog(m_hWnd, std::wstring(GetStringResource(IDS_WINDOWTITLE_EXPORTTTMP)));
		ShowWindow(m_hWnd, SW_HIDE);
		const auto hideWhilePacking = xivres::util::on_dtor([this]() { ShowWindow(m_hWnd, SW_SHOW); });

		const auto level = compressionMode == CompressionMode::CompressWhilePacking ? Z_BEST_COMPRESSION : Z_NO_COMPRESSION;
		writer.begin_packed(compressionMode == CompressionMode::CompressAfterPacking ? Z_BEST_COMPRESSION : Z_NO_COMPRESSION);
		for (auto& pFontSet : m_multiFontSet.FontSets) {
			const auto compiled = CompileCurrentFontSet(progressDialog, *pFontSet);
			ThrowIfNotUsableByGame(m_multiFontSet, *pFontSet, compiled);
			const auto& [fdts, mips] = compiled;

			const auto reportWriting = [&](const std::string& targetFileName) {
				progressDialog.ThrowIfCancelled();
				const auto targetFileNameW = xivres::util::unicode::convert<std::wstring>(targetFileName);
				progressDialog.UpdateStatusMessage(std::vformat(GetStringResource(IDS_EXPORTPROGRESS_WRITINGFILE), std::make_wformat_args(targetFileNameW)));
			};

			auto& modsList = writer.ttmpl().SimpleModsList;
			const auto beginIndex = modsList.size();

			for (size_t i = 0; i < fdts.size(); i++) {
				const auto targetFileName = std::format("common/font/{}.fdt", pFontSet->Fonts[i]->Name);
				reportWriting(targetFileName);
				writer.add_packed(xivres::compressing_packed_stream<xivres::standard_compressing_packer>(targetFileName, fdts[i], level));
			}

			for (size_t i = 0; i < mips.size(); i++) {
				const auto i1 = i + 1;
				const auto targetFileName = std::format("common/font/{}", std::vformat(pFontSet->TexFilenameFormat, std::make_format_args(i1)));
				reportWriting(targetFileName);

				auto textureOne = std::make_shared<xivres::texture::stream>(mips[i]->Type, mips[i]->Width, mips[i]->Height, 1, 1, 1);
				textureOne->set_mipmap(0, 0, mips[i]);
				writer.add_packed(xivres::compressing_packed_stream<xivres::texture_compressing_packer>(targetFileName, std::move(textureOne), level));
			}

			// The other files are the same packed data under other names.
			const auto endIndex = modsList.size();
			for (const auto& [source, alias] : GetExportAliases(m_multiFontSet, *pFontSet, mips.size())) {
				for (size_t i = beginIndex; i < endIndex; i++) {
					if (modsList[i].Name != std::format("common/font/{}", source))
						continue;

					auto tmp = modsList[i];
					tmp.Name = std::format("common/font/{}", alias);
					tmp.FullPath = xivres::util::unicode::convert<std::string>(tmp.Name, &xivres::util::unicode::lower);
					modsList.push_back(std::move(tmp));
				}
			}
		}
		writer.close();
		return 0;
	});
}

LRESULT App::FontEditorWindow::Menu_Export_ToggleMapping(const ExportMapping& mapping) {
	m_multiFontSet.*mapping.Enabled = !(m_multiFontSet.*mapping.Enabled);
	Changes_MarkDirty();
	return 0;
}
