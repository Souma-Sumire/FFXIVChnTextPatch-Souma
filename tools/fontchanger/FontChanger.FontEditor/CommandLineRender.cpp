// Copyright (c) 2022-2026 Soreepeong
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Modified by Souma-Sumire for FFXIVChnTextPatch-Souma:
// Added --export-raw, --export-ttmp, --autofit, --game-path, and headless progress reporting.

#include "pch.h"
#include "MainWindow.h"
#include "GlyphFileTools.h"
#include "WicImage.h"
#include "CommandLineRender.h"
#include "FontChanger.Presets/GlyphFiles.h"
#include "FontChanger.Presets/ElementFonts.h"
#include "FontChanger.Presets/FontsFromSystemFont.h"
#include "xivres/textools.h"

namespace {
	// The app has no console of its own; messages go to the console that started it, if any.
	void WriteError(const std::wstring& message) {
		auto handle = GetStdHandle(STD_ERROR_HANDLE);
		if (!handle || handle == INVALID_HANDLE_VALUE) {
			if (!AttachConsole(ATTACH_PARENT_PROCESS) && GetLastError() != ERROR_ACCESS_DENIED)
				return;
			handle = GetStdHandle(STD_ERROR_HANDLE);
			if (!handle || handle == INVALID_HANDLE_VALUE)
				return;
		}

		const auto line = message + L"\r\n";
		if (DWORD written; !WriteConsoleW(handle, line.data(), static_cast<DWORD>(line.size()), &written, nullptr)) {
			// Redirected to a file or a pipe.
			const auto utf8 = xivres::util::unicode::convert<std::string>(line);
			WriteFile(handle, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
		}
	}

	void ReportProgress(int percent, std::string_view message) {
		WriteError(xivres::util::unicode::convert<std::wstring>(std::format("PROGRESS: {}: {}", percent, message)));
	}

	std::wstring UnescapeText(std::wstring_view text) {
		std::wstring res;
		for (size_t i = 0; i < text.size(); i++) {
			if (text[i] == L'\\' && i + 1 < text.size() && (text[i + 1] == L'n' || text[i + 1] == L'\\')) {
				res += text[i + 1] == L'n' ? L'\n' : L'\\';
				i++;
			} else {
				res += text[i];
			}
		}
		return res;
	}

	App::CompiledFontSet CompileFontSetHeadless(App::Structs::FontSet& fontSet) {
		ReportProgress(10, "Loading fonts...");
		fontSet.ConsolidateFonts();

		ReportProgress(20, "Checking kerning pairs...");
		for (const auto& pFont : fontSet.Fonts) {
			if (const auto nKerns = pFont->GetMergedFont()->all_kerning_pairs().size(); nKerns >= 65536) {
				throw std::runtime_error(std::format("Kerning table too large for {}: {}", pFont->Name, nKerns));
			}
		}

		FontChanger::FixedSizeFont::fontdata_packer packer;
		packer.set_discard_step(fontSet.DiscardStep);
		packer.set_side_length(fontSet.SideLength);

		for (auto& pFont : fontSet.Fonts)
			packer.add_font(pFont->GetMergedFont());

		packer.compile();

		while (!packer.wait(std::chrono::milliseconds(200))) {
			int pct = 20 + static_cast<int>(packer.progress_scaled() * 60.0f);
			switch (packer.progress_description()) {
				case FontChanger::FixedSizeFont::fontdata_packer::progress_status::prepare_source_fonts:
					ReportProgress(pct, "Preparing source fonts...");
					break;
				case FontChanger::FixedSizeFont::fontdata_packer::progress_status::prepare_target_fonts:
					ReportProgress(pct, "Preparing target fonts...");
					break;
				case FontChanger::FixedSizeFont::fontdata_packer::progress_status::discover_glyphs:
					ReportProgress(pct, "Discovering glyphs...");
					break;
				case FontChanger::FixedSizeFont::fontdata_packer::progress_status::measure_glyphs:
					ReportProgress(pct, "Measuring glyphs...");
					break;
				case FontChanger::FixedSizeFont::fontdata_packer::progress_status::layout_and_draw:
					ReportProgress(pct, "Layout and draw...");
					break;
			}
		}

		if (const auto err = packer.get_error_if_failed(); !err.empty())
			throw std::runtime_error(err);

		App::CompiledFontSet res{packer.compiled_fontdatas(), packer.compiled_mipmap_streams()};
		if (res.Mipmaps.empty())
			throw std::runtime_error("No mipmap produced");
		return res;
	}

	std::string_view GetFileName(std::string_view path) {
		return path.substr(path.rfind('/') + 1);
	}

	struct ExportMapping {
		bool App::Structs::MultiFontSet::* Enabled;
		xivres::font_type Target;
	};

	static constexpr ExportMapping ExportMappings[]{
		{&App::Structs::MultiFontSet::ExportMapFontLobbyToFont, xivres::font_type::font_lobby},
		{&App::Structs::MultiFontSet::ExportMapChnAxisToFont, xivres::font_type::chn_axis},
		{&App::Structs::MultiFontSet::ExportMapKrnAxisToFont, xivres::font_type::krn_axis},
		{&App::Structs::MultiFontSet::ExportMapTcAxisToFont, xivres::font_type::tc_axis},
	};

	std::vector<std::pair<std::string, std::string>> GetExportAliases(const App::Structs::MultiFontSet& multiFontSet, const App::Structs::FontSet& fontSet, size_t textureCount) {
		std::vector<std::pair<std::string, std::string>> res;
		if (fontSet.TexFilenameFormat != "font{}.tex")
			return res;

		const auto sourceDefs = FontChanger::FixedSizeFont::get_fontdata_definition(xivres::font_type::font);
		for (const auto& mapping : ExportMappings) {
			if (!(multiFontSet.*mapping.Enabled))
				continue;

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

	void ExportRaw(const App::Structs::MultiFontSet& multiFontSet, const std::filesystem::path& outDir) {
		std::vector<char> buf(32768);
		const auto write = [&](const std::filesystem::path& path, xivres::stream& stream) {
			std::ofstream out(path, std::ios::binary);
			for (size_t read, pos = 0; (read = stream.read(pos, buf.data(), buf.size())); pos += read)
				out.write(buf.data(), read);
		};

		for (const auto& pFontSet : multiFontSet.FontSets) {
			const auto compiled = CompileFontSetHeadless(*pFontSet);
			const auto& [fdts, mips] = compiled;

			ReportProgress(85, "Writing font textures and metadata...");
			xivres::texture::stream textureOne(mips[0]->Type, mips[0]->Width, mips[0]->Height, 1, 1, 1);
			for (size_t i = 0; i < mips.size(); i++) {
				textureOne.set_mipmap(0, 0, mips[i]);
				const auto i1 = i + 1;
				write(outDir / std::vformat(pFontSet->TexFilenameFormat, std::make_format_args(i1)), textureOne);
			}

			for (size_t i = 0; i < fdts.size(); i++) {
				write(outDir / std::format("{}.fdt", pFontSet->Fonts[i]->Name), *fdts[i]);
			}

			for (const auto& [source, alias] : GetExportAliases(multiFontSet, *pFontSet, mips.size()))
				std::filesystem::copy(outDir / source, outDir / alias, std::filesystem::copy_options::overwrite_existing);
		}
		ReportProgress(100, "Done.");
	}

	void ExportTTMP(const App::Structs::MultiFontSet& multiFontSet, const std::filesystem::path& outPath) {
		xivres::textools::simple_ttmp2_writer writer(outPath);
		writer.ttmpl().Name = xivres::util::unicode::convert<std::string>(outPath.stem().wstring());

		const auto level = Z_BEST_COMPRESSION;
		writer.begin_packed(Z_BEST_COMPRESSION);

		for (auto& pFontSet : multiFontSet.FontSets) {
			const auto compiled = CompileFontSetHeadless(*pFontSet);
			const auto& [fdts, mips] = compiled;

			ReportProgress(85, "Packing mod files...");
			auto& modsList = writer.ttmpl().SimpleModsList;
			const auto beginIndex = modsList.size();

			for (size_t i = 0; i < fdts.size(); i++) {
				const auto targetFileName = std::format("common/font/{}.fdt", pFontSet->Fonts[i]->Name);
				writer.add_packed(xivres::compressing_packed_stream<xivres::standard_compressing_packer>(targetFileName, fdts[i], level));
			}

			for (size_t i = 0; i < mips.size(); i++) {
				const auto i1 = i + 1;
				const auto targetFileName = std::format("common/font/{}", std::vformat(pFontSet->TexFilenameFormat, std::make_format_args(i1)));
				auto textureOne = std::make_shared<xivres::texture::stream>(mips[i]->Type, mips[i]->Width, mips[i]->Height, 1, 1, 1);
				textureOne->set_mipmap(0, 0, mips[i]);
				writer.add_packed(xivres::compressing_packed_stream<xivres::texture_compressing_packer>(targetFileName, std::move(textureOne), level));
			}

			const auto endIndex = modsList.size();
			for (const auto& [source, alias] : GetExportAliases(multiFontSet, *pFontSet, mips.size())) {
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
		ReportProgress(100, "Done.");
	}

	int Render(const std::vector<std::wstring>& args) {
		std::optional<std::wstring> text, output, fontName, exportFolder, exportRawDir, exportTtmpPath, gamePath;
		std::optional<int> maxWidth;
		std::optional<size_t> elementIndex;
		std::vector<size_t> elementIndices;
		auto withAdjustments = false, exportSvg = true, exportPng = true, autofitMode = false;
		for (size_t i = 2; i < args.size(); i++) {
			const auto takeValue = [&]() -> const std::wstring& {
				if (i + 1 >= args.size())
					throw std::invalid_argument(std::format("{} needs a value.", xivres::util::unicode::convert<std::string>(args[i])));
				return args[++i];
			};
			if (args[i] == L"--render-text")
				text = UnescapeText(takeValue());
			else if (args[i] == L"--output")
				output = takeValue();
			else if (args[i] == L"--font")
				fontName = takeValue();
			else if (args[i] == L"--max-width")
				maxWidth = std::stoi(takeValue());
			else if (args[i] == L"--export-glyphs")
				exportFolder = takeValue();
			else if (args[i] == L"--export-raw")
				exportRawDir = takeValue();
			else if (args[i] == L"--export-ttmp")
				exportTtmpPath = takeValue();
			else if (args[i] == L"--game-path")
				gamePath = takeValue();
			else if (args[i] == L"--element") {
				const auto index = std::stoul(takeValue());
				elementIndices.push_back(index);
				if (!elementIndex)
					elementIndex = index;
			} else if (args[i] == L"--autofit")
				autofitMode = true;
			else if (args[i] == L"--with-adjustments")
				withAdjustments = true;
			else if (args[i] == L"--no-svg")
				exportSvg = false;
			else if (args[i] == L"--no-png")
				exportPng = false;
			else
				throw std::invalid_argument(std::format("Unknown argument: {}", xivres::util::unicode::convert<std::string>(args[i])));
		}
		if (args.size() < 2 || args[1].starts_with(L"--"))
			throw std::invalid_argument("The first argument must be the path of a configuration file.");

		if (gamePath && !gamePath->empty()) {
			const std::filesystem::path gp(*gamePath);
			App::ElementFonts::SetGameInstallationPathsProvider([gp](xivres::font_type) {
				return std::vector<std::filesystem::path>{ gp / L"game", gp };
			});
		}

		const auto configPath = std::filesystem::absolute(std::filesystem::path(args[1]));
		std::ifstream in(configPath, std::ios::binary);
		if (!in)
			throw std::runtime_error(std::format("Failed to open {}.", xivres::util::unicode::convert<std::string>(args[1])));
		App::ElementFonts::SetProjectDirectory(configPath.parent_path());
		const auto multiFontSet = nlohmann::json::parse(in).get<App::Structs::MultiFontSet>();

		if (exportRawDir) {
			ExportRaw(multiFontSet, *exportRawDir);
			return 0;
		}

		if (exportTtmpPath) {
			ExportTTMP(multiFontSet, *exportTtmpPath);
			return 0;
		}

		App::Structs::Font* presetFont = nullptr;
		App::Structs::Font* prefixFont = nullptr;
		App::Structs::Font* firstFont = nullptr;
		const auto targetFontUtf8 = fontName ? xivres::util::unicode::convert<std::string>(*fontName) : std::string{};
		for (const auto& fontSet : multiFontSet.FontSets) {
			for (const auto& f : fontSet->Fonts) {
				if (!firstFont)
					firstFont = f.get();
				if (!presetFont && (!fontName || f->Name == targetFontUtf8))
					presetFont = f.get();
				if (!prefixFont && fontName && f->Name.starts_with(targetFontUtf8))
					prefixFont = f.get();
			}
		}
		if (!presetFont)
			presetFont = prefixFont ? prefixFont : firstFont;
		if (!presetFont) {
			throw std::invalid_argument("The configuration has no font.");
		}

		if (autofitMode) {
			// --autofit：用组件自带的 AutoFit 拟合系统字体图层，输出该字号拟合后的元素列表。
			if (!output)
				throw std::invalid_argument("--output is required for --autofit.");
			std::vector<App::Structs::FontElement*> targets;
			if (elementIndices.empty()) {
				for (const auto& element : presetFont->Elements)
					targets.push_back(element.get());
			} else {
				for (const auto index : elementIndices) {
					if (index >= presetFont->Elements.size())
						throw std::invalid_argument(std::format("The font has {} elements.", presetFont->Elements.size()));
					targets.push_back(presetFont->Elements[index].get());
				}
			}
			const auto fitted = FontChanger::FontsFromSystemFont::AutoFit(*presetFont, targets);
			if (!fitted)
				throw std::runtime_error(std::format("{} is not one of the game's fonts.", presetFont->Name));
			nlohmann::json elements = nlohmann::json::array();
			for (const auto& element : presetFont->Elements)
				elements.push_back(*element);
			std::ofstream out(*output, std::ios::binary);
			out << elements.dump(2);
			if (!out)
				throw std::runtime_error(std::format("Failed to write {}.", xivres::util::unicode::convert<std::string>(*output)));
			return 0;
		}

		if (exportFolder) {
			const App::Structs::FontElement* element = nullptr;
			if (elementIndex) {
				if (*elementIndex >= presetFont->Elements.size())
					throw std::invalid_argument(std::format("The font has {} elements.", presetFont->Elements.size()));
				element = presetFont->Elements[*elementIndex].get();
			}
			const auto font = element ? App::GlyphFileTools::GetElementFontForExport(*element, withAdjustments) : presetFont->GetMergedFont();
			App::GlyphFileTools::ExportGlyphs(*font, *exportFolder, {
				.Svg = exportSvg,
				.Png = exportPng,
				.WithAdjustments = withAdjustments || !element,
				.Source = element ? App::GlyphFileTools::DescribeSource(*element) : App::GlyphFileTools::DescribeSource(*presetFont),
			});
			if (!output)
				return 0;
		}

		if (!output)
			throw std::invalid_argument("--output is required for text rendering.");
		if (!text)
			throw std::invalid_argument("--render-text is required.");

		const auto& font = *presetFont->GetMergedFont();
		const auto measured = FontChanger::FixedSizeFont::text_measurer(font)
			.max_width(maxWidth.value_or((std::numeric_limits<int>::max)()))
			.use_kerning(true)
			.measure(xivres::util::unicode::convert<std::string>(*text));

		const auto mipmap = measured.create_mipmap(font, {0xFF, 0xFF, 0xFF, 0xFF}, {0x00, 0x00, 0x00, 0xFF}, 16);
		App::WicImage::SavePng(*mipmap, *output);
		return 0;
	}
}

std::optional<int> App::RunCommandLineRender(const std::vector<std::wstring>& args) {
	if (std::ranges::find(args, L"--render-text") == args.end() &&
		std::ranges::find(args, L"--export-glyphs") == args.end() &&
		std::ranges::find(args, L"--export-raw") == args.end() &&
		std::ranges::find(args, L"--export-ttmp") == args.end() &&
		std::ranges::find(args, L"--autofit") == args.end())
		return std::nullopt;

	App::ElementFonts::SetGameFontErrorHandler(nullptr);

	try {
		return Render(args);
	} catch (const _com_error& e) {
		WriteError(std::format(L"Error: {}", e.ErrorMessage()));
	} catch (const std::exception& e) {
		WriteError(std::format(L"Error: {}", xivres::util::unicode::convert<std::wstring>(e.what())));
	}
	return 1;
}
