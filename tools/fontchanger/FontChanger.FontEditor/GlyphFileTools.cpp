#include "pch.h"
#include "GlyphFileTools.h"

#include "FontChanger.Presets/GlyphFiles.h"
#include "MainWindow.Internal.h"
#include "WicImage.h"

namespace {
	using FontChanger::FixedSizeFont::glyph_metrics;
	using FontChanger::FixedSizeFont::glyph_outline;

	// Up to three decimals, without trailing zeros.
	std::string FormatNumber(double value) {
		auto s = std::format("{:.3f}", value);
		while (s.ends_with('0'))
			s.pop_back();
		if (s.ends_with('.'))
			s.pop_back();
		if (s == "-0")
			s = "0";
		return s;
	}

	std::string FormatCodepoint(char32_t codepoint) {
		return std::format("U+{:04X}", static_cast<uint32_t>(codepoint));
	}

	std::string EncodeBase64(std::span<const uint8_t> data) {
		static constexpr char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string res;
		res.reserve((data.size() + 2) / 3 * 4);
		for (size_t i = 0; i < data.size(); i += 3) {
			const auto n = data.size() - i;
			const uint32_t v = data[i] << 16 | (n > 1 ? data[i + 1] << 8 : 0) | (n > 2 ? data[i + 2] : 0);
			res += Alphabet[v >> 18 & 63];
			res += Alphabet[v >> 12 & 63];
			res += n > 1 ? Alphabet[v >> 6 & 63] : '=';
			res += n > 2 ? Alphabet[v & 63] : '=';
		}
		return res;
	}

	// Maps pixels of the font, in the space of glyph_metrics, to the units of the SVG files.
	struct UnitMapping {
		double Scale;  // units per pixel
		int Ascent;

		[[nodiscard]] double X(double px) const { return px * Scale; }
		[[nodiscard]] double Y(double px) const { return FontChanger::FixedSizeFont::glyph_files_baseline_y + (px - Ascent) * Scale; }
	};

	std::string OutlineToPathData(const glyph_outline& outline, const UnitMapping& m) {
		std::string res;
		size_t pointIndex = 0;
		const auto appendPoints = [&](char command, size_t count) {
			if (!res.empty())
				res += ' ';
			res += command;
			for (size_t i = 0; i < count; i++, pointIndex++) {
				const auto& p = outline.Points[pointIndex];
				res += std::format("{}{},{}", i ? " " : "", FormatNumber(m.X(p.X)), FormatNumber(m.Y(p.Y)));
			}
		};
		for (const auto verb : outline.Verbs) {
			switch (verb) {
				case glyph_outline::verb::MoveTo: appendPoints('M', 1); break;
				case glyph_outline::verb::LineTo: appendPoints('L', 1); break;
				case glyph_outline::verb::QuadTo: appendPoints('Q', 2); break;
				case glyph_outline::verb::CubicTo: appendPoints('C', 3); break;
				case glyph_outline::verb::Close: res += " Z"; break;
			}
		}
		return res;
	}

	// Pixels of a glyph drawn with its origin at (-x1, -y1) into a buffer of the given size, as coverage.
	std::vector<uint8_t> DrawCoverage(const FontChanger::FixedSizeFont::fixed_size_font& font, char32_t codepoint, int x1, int y1, int width, int height) {
		std::vector<uint8_t> coverage(static_cast<size_t>(width) * height);
		font.draw(codepoint, coverage.data(), 1, -x1, -y1, width, height, 255, 0, 255, 0);
		return coverage;
	}

	std::vector<xivres::util::b8g8r8a8> CoverageToBlackPixels(std::span<const uint8_t> coverage) {
		std::vector<xivres::util::b8g8r8a8> pixels(coverage.size(), xivres::util::b8g8r8a8(0u));
		for (size_t i = 0; i < coverage.size(); i++)
			pixels[i].A = coverage[i];
		return pixels;
	}

	// Finds the glyph merging font that draws the codepoint, and how far the glyph is moved from where that font puts it.
	const FontChanger::FixedSizeFont::glyph_merging_fixed_size_font* FindGlyphMergingFont(const FontChanger::FixedSizeFont::fixed_size_font& font, char32_t codepoint, int& dx, int& dy) {
		const auto base = dynamic_cast<const FontChanger::FixedSizeFont::glyph_merging_fixed_size_font*>(font.get_base_font(codepoint));
		if (!base)
			return nullptr;

		glyph_metrics gm, baseGm;
		if (!font.try_get_glyph_metrics(codepoint, gm) || !base->try_get_glyph_metrics(codepoint, baseGm))
			return nullptr;
		dx = gm.X1 - baseGm.X1;
		dy = gm.Y1 - baseGm.Y1;
		return base;
	}

	std::string MakeSvg(const FontChanger::FixedSizeFont::fixed_size_font& font, char32_t codepoint, const glyph_metrics& gm, const UnitMapping& m) {
		const auto lineHeight = font.line_height();
		const auto advance = m.X(gm.AdvanceX);
		const auto top = m.Y(0);
		const auto height = lineHeight * m.Scale;

		// A viewBox cannot be empty; glyphs that do not advance, such as combining marks, keep their advance in the attribute.
		const auto width = advance > 0 ? advance : (std::max)(1., m.X(gm.X2));

		std::string svg;
		svg += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
		svg += std::format(
			"<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\""
			" xmlns:sodipodi=\"http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd\" xmlns:inkscape=\"http://www.inkscape.org/namespaces/inkscape\"\n"
			"     width=\"{0}\" height=\"{1}\" viewBox=\"0 {2} {0} {1}\"\n"
			"     data-xivfont-codepoint=\"{3}\" data-xivfont-units-per-em=\"{4}\" data-xivfont-baseline=\"{5}\" data-xivfont-advance=\"{6}\">\n",
			FormatNumber(width), FormatNumber(height), FormatNumber(top),
			FormatCodepoint(codepoint), FormatNumber(FontChanger::FixedSizeFont::glyph_files_units_per_em), FormatNumber(FontChanger::FixedSizeFont::glyph_files_baseline_y), FormatNumber(advance));

		// Inkscape stores the positions of guides relative to the bottom left of the page, which is the viewBox here.
		const auto guide = [&](const char* id, const char* label, double x, double y, bool horizontal) {
			svg += std::format(
				"    <sodipodi:guide id=\"{}\" inkscape:label=\"{}\" position=\"{},{}\" orientation=\"{}\" inkscape:locked=\"true\" />\n",
				id, label, FormatNumber(x), FormatNumber(top + height - y), horizontal ? "0,1" : "1,0");
		};
		svg += "  <sodipodi:namedview id=\"namedview\" pagecolor=\"#ffffff\" inkscape:document-units=\"px\" showguides=\"true\">\n";
		guide("guide-ascent", "ascent", 0, top, true);
		guide("guide-baseline", "baseline", 0, FontChanger::FixedSizeFont::glyph_files_baseline_y, true);
		guide("guide-descent", "descent", 0, top + height, true);
		guide("guide-origin", "origin", 0, top, false);
		guide("guide-advance", "advance", advance, top, false);
		svg += "  </sodipodi:namedview>\n";

		glyph_outline outline;
		if (font.try_get_glyph_outline(codepoint, outline)) {
			svg += "  <g id=\"glyph\" inkscape:groupmode=\"layer\" inkscape:label=\"Glyph\">\n";
			if (!outline.empty()) {
				svg += std::format(
					"    <path d=\"{}\"{} />\n",
					OutlineToPathData(outline, m),
					outline.EvenOdd ? " fill-rule=\"evenodd\"" : "");
			}
			svg += "  </g>\n";
		} else {
			// Glyphs without outlines are given as an image to trace, in a locked layer that is not drawn back.
			svg += "  <g id=\"xivfont-reference\" inkscape:groupmode=\"layer\" inkscape:label=\"Reference\" sodipodi:insensitive=\"true\" opacity=\"0.5\">\n";
			if (!gm.is_effectively_empty()) {
				const auto coverage = DrawCoverage(font, codepoint, gm.X1, gm.Y1, gm.width(), gm.height());
				const auto png = App::WicImage::EncodePng(gm.width(), gm.height(), CoverageToBlackPixels(coverage));
				svg += std::format(
					"    <image x=\"{}\" y=\"{}\" width=\"{}\" height=\"{}\" preserveAspectRatio=\"none\" style=\"image-rendering:pixelated\" xlink:href=\"data:image/png;base64,{}\" />\n",
					FormatNumber(m.X(gm.X1)), FormatNumber(m.Y(gm.Y1)), FormatNumber(gm.width() * m.Scale), FormatNumber(gm.height() * m.Scale),
					EncodeBase64(png));
			}

			// The shape of a merged glyph is in the units of shapes at the size and ascent of the element.
			int dx, dy;
			if (const auto merging = FindGlyphMergingFont(font, codepoint, dx, dy)) {
				if (const auto pathData = merging->get_shape_path_data(codepoint)) {
					const auto shapeScale = merging->font_size() / FontChanger::FixedSizeFont::glyph_files_units_per_em;
					const auto a = shapeScale * m.Scale;
					const auto e = m.X(dx);
					const auto f = m.Y(merging->ascent() + dy - FontChanger::FixedSizeFont::glyph_files_baseline_y * shapeScale);
					svg += std::format(
						"    <path d=\"{}\" transform=\"matrix({} 0 0 {} {} {})\" fill=\"none\" stroke=\"#0080ff\" stroke-width=\"{}\" />\n",
						*pathData, FormatNumber(a), FormatNumber(a), FormatNumber(e), FormatNumber(f), FormatNumber(4 / a));
				}
			}
			svg += "  </g>\n";
			svg += "  <g id=\"glyph\" inkscape:groupmode=\"layer\" inkscape:label=\"Glyph\" />\n";
		}
		svg += "</svg>\n";
		return svg;
	}

	// Watches folders on a thread of its own, and tells a window about changes once the files have been quiet for a
	// moment, as editors often save through temporary files.
	class FolderWatcher {
		static constexpr DWORD QuietMilliseconds = 300;

		std::mutex m_mtx;
		HWND m_hWnd = nullptr;
		UINT m_message = 0;
		std::set<std::filesystem::path> m_folders;
		std::vector<std::filesystem::path> m_changed;
		HANDLE m_wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		std::thread m_thread;

		void Run() {
			std::map<std::filesystem::path, HANDLE> handles;
			std::map<std::filesystem::path, ULONGLONG> pending;
			while (true) {
				{
					const auto lock = std::scoped_lock(m_mtx);
					if (!m_hWnd)
						break;
					for (const auto& folder : m_folders) {
						if (handles.contains(folder))
							continue;
						const auto h = FindFirstChangeNotificationW(folder.c_str(), FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE);
						handles.emplace(folder, h);
					}
				}

				std::vector<HANDLE> waitHandles{m_wakeEvent};
				std::vector<std::filesystem::path> waitFolders{{}};
				for (const auto& [folder, h] : handles) {
					if (h != INVALID_HANDLE_VALUE && waitHandles.size() < MAXIMUM_WAIT_OBJECTS) {
						waitHandles.push_back(h);
						waitFolders.push_back(folder);
					}
				}

				auto timeout = INFINITE;
				const auto now = GetTickCount64();
				for (const auto& [_, since] : pending)
					timeout = (std::min<DWORD>)(timeout, static_cast<DWORD>(since + QuietMilliseconds > now ? since + QuietMilliseconds - now : 0));

				const auto r = WaitForMultipleObjects(static_cast<DWORD>(waitHandles.size()), waitHandles.data(), FALSE, timeout);
				if (r > WAIT_OBJECT_0 && r < WAIT_OBJECT_0 + waitHandles.size()) {
					const auto i = r - WAIT_OBJECT_0;
					pending[waitFolders[i]] = GetTickCount64();
					FindNextChangeNotification(waitHandles[i]);
				}

				std::vector<std::filesystem::path> quiet;
				for (auto it = pending.begin(); it != pending.end();) {
					if (GetTickCount64() >= it->second + QuietMilliseconds) {
						quiet.push_back(it->first);
						it = pending.erase(it);
					} else {
						++it;
					}
				}
				if (!quiet.empty()) {
					for (const auto& folder : quiet)
						FontChanger::GlyphFiles::InvalidateFolder(folder);

					const auto lock = std::scoped_lock(m_mtx);
					m_changed.insert(m_changed.end(), quiet.begin(), quiet.end());
					if (m_hWnd)
						PostMessageW(m_hWnd, m_message, 0, 0);
				}
			}

			for (const auto& [_, h] : handles) {
				if (h != INVALID_HANDLE_VALUE)
					FindCloseChangeNotification(h);
			}
		}

	public:
		~FolderWatcher() {
			{
				const auto lock = std::scoped_lock(m_mtx);
				m_hWnd = nullptr;
			}
			SetEvent(m_wakeEvent);
			if (m_thread.joinable())
				m_thread.join();
			CloseHandle(m_wakeEvent);
		}

		static FolderWatcher& Instance() {
			static FolderWatcher s_instance;
			return s_instance;
		}

		void SetWindow(HWND hWnd, UINT message) {
			{
				const auto lock = std::scoped_lock(m_mtx);
				m_hWnd = hWnd;
				m_message = message;
			}
			SetEvent(m_wakeEvent);
			if (hWnd && !m_thread.joinable())
				m_thread = std::thread([this] { Run(); });
			else if (!hWnd && m_thread.joinable())
				m_thread.join();
		}

		void Watch(const std::filesystem::path& folder) {
			{
				const auto lock = std::scoped_lock(m_mtx);
				if (!m_hWnd || !m_folders.insert(folder).second)
					return;
			}
			SetEvent(m_wakeEvent);
		}

		std::vector<std::filesystem::path> TakeChanged() {
			const auto lock = std::scoped_lock(m_mtx);
			return std::exchange(m_changed, {});
		}
	};

	void WriteFile(const std::filesystem::path& path, std::string_view data) {
		std::ofstream out(path, std::ios::binary);
		if (!out)
			throw std::runtime_error(std::format("Failed to open {} for writing.", xivres::util::unicode::convert<std::string>(path.wstring())));
		out.write(data.data(), static_cast<std::streamsize>(data.size()));
		if (!out)
			throw std::runtime_error(std::format("Failed to write to {}.", xivres::util::unicode::convert<std::string>(path.wstring())));
	}
}

void App::GlyphFileTools::SetChangeNotificationWindow(HWND hWnd, UINT message) {
	FolderWatcher::Instance().SetWindow(hWnd, message);
	FontChanger::GlyphFiles::SetFolderObserver(hWnd ? [](const std::filesystem::path& folder) { FolderWatcher::Instance().Watch(folder); } : std::function<void(const std::filesystem::path&)>());
}

std::vector<std::filesystem::path> App::GlyphFileTools::TakeChangedFolders() {
	return FolderWatcher::Instance().TakeChanged();
}


void App::GlyphFileTools::Embed(Structs::GlyphImagesStruct& settings) {
	if (settings.IsEmbedded())
		return;

	const auto set = FontChanger::GlyphFiles::LoadGlyphSet(settings);
	if (!set->Error.empty())
		throw std::runtime_error(xivres::util::unicode::convert<std::string>(set->Error));

	std::map<char32_t, Structs::GlyphImagesStruct::EmbeddedGlyph> embedded;
	for (const auto& [codepoint, source] : set->Glyphs) {
		if (source.Svg)
			embedded[codepoint].Svg = source.Svg;
		else if (const auto it = set->PngFiles.find(codepoint); it != set->PngFiles.end())
			embedded[codepoint].PngBase64 = std::make_shared<const std::string>(EncodeBase64({reinterpret_cast<const uint8_t*>(it->second->data()), it->second->size()}));
	}
	if (embedded.empty())
		throw std::runtime_error("The folder has no glyph files.");

	settings.Embedded = std::move(embedded);
	if (!set->Metadata.is_null())
		settings.EmbeddedMetadata = set->Metadata;
	else
		settings.EmbeddedMetadata.reset();
}

std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font> App::GlyphFileTools::GetElementFontForExport(const Structs::FontElement& element, bool withAdjustments) {
	if (withAdjustments)
		return element.GetWrappedFont();

	// The codepoints and their replacements are kept, as they decide which glyphs the element adds.
	auto modifiers = element.WrapModifiers;
	modifiers.LetterSpacing = modifiers.HorizontalOffset = modifiers.VerticalOffset = 0;
	return std::make_shared<FontChanger::FixedSizeFont::wrapping_fixed_size_font>(element.GetBaseFont(), modifiers);
}

nlohmann::json App::GlyphFileTools::DescribeSource(const Structs::FontElement& element) {
	const auto& font = *element.GetBaseFont();
	return {
		{"family", font.family_name()},
		{"subfamily", font.subfamily_name()},
		{"elementSource", xivres::util::unicode::convert<std::string>(GetSourceRepresentation(element))},
		{"element", element},
	};
}

nlohmann::json App::GlyphFileTools::DescribeSource(const Structs::Font& font) {
	return {
		{"font", font.Name},
	};
}

void App::GlyphFileTools::ExportGlyphs(
	const FontChanger::FixedSizeFont::fixed_size_font& font,
	const std::filesystem::path& folder,
	const ExportOptions& options,
	const std::function<void(size_t, size_t)>& progress) {
	create_directories(folder);

	const auto size = font.font_size();
	if (!(size > 0))
		throw std::invalid_argument("The font has no size.");

	// Pixel exact: units are taken so that the metrics, which are in whole pixels, round back to the same values when
	// the files are drawn at the same size.
	const UnitMapping m{FontChanger::FixedSizeFont::glyph_files_units_per_em / size, font.ascent()};
	const auto lineHeight = font.line_height();

	auto glyphs = nlohmann::json::object();
	const std::vector<char32_t> codepoints(font.all_codepoints().begin(), font.all_codepoints().end());
	for (size_t i = 0; i < codepoints.size(); i++) {
		if (progress)
			progress(i, codepoints.size());

		const auto codepoint = codepoints[i];
		glyph_metrics gm;
		if (!font.try_get_glyph_metrics(codepoint, gm))
			continue;

		const auto stem = FontChanger::FixedSizeFont::get_glyph_file_stem(codepoint);
		if (options.Svg)
			WriteFile(folder / (stem + L".svg"), MakeSvg(font, codepoint, gm, m));

		if (options.Png) {
			// The image covers the line box, and grows to keep the pixels that are outside of it.
			const auto x1 = (std::min)(0, gm.is_effectively_empty() ? 0 : gm.X1);
			const auto y1 = (std::min)(0, gm.is_effectively_empty() ? 0 : gm.Y1);
			const auto x2 = (std::max)({x1 + 1, gm.AdvanceX, gm.is_effectively_empty() ? 0 : gm.X2});
			const auto y2 = (std::max)({y1 + 1, lineHeight, gm.is_effectively_empty() ? 0 : gm.Y2});
			const auto coverage = DrawCoverage(font, codepoint, x1, y1, x2 - x1, y2 - y1);
			App::WicImage::SavePng(x2 - x1, y2 - y1, CoverageToBlackPixels(coverage), folder / (stem + L".png"));

			if (x1 != 0 || y1 != 0 || x2 != gm.AdvanceX) {
				glyphs[FormatCodepoint(codepoint)] = {
					{"advancePx", gm.AdvanceX},
					{"pngOrigin", nlohmann::json::array({-x1, -y1})},
				};
			}
		}
	}

	auto kerning = nlohmann::json::array();
	for (const auto& [pair, value] : font.all_kerning_pairs()) {
		if (value)
			kerning.push_back(nlohmann::json::array({static_cast<uint32_t>(pair.first), static_cast<uint32_t>(pair.second), std::stod(FormatNumber(value * m.Scale))}));
	}

	nlohmann::json metadata{
		{"generator", "FontChanger.FontEditor"},
		{"unitsPerEm", FontChanger::FixedSizeFont::glyph_files_units_per_em},
		{"baselineY", FontChanger::FixedSizeFont::glyph_files_baseline_y},
		{"ascent", std::stod(FormatNumber(font.ascent() * m.Scale))},
		{"lineHeight", std::stod(FormatNumber(lineHeight * m.Scale))},
		{"exportedSize", size},
		{"ascentPx", font.ascent()},
		{"lineHeightPx", lineHeight},
		{"pixelExact", true},
		{"withAdjustments", options.WithAdjustments},
		{"source", options.Source},
		{"kerning", std::move(kerning)},
	};
	if (!glyphs.empty())
		metadata["glyphs"] = std::move(glyphs);
	WriteFile(folder / FontChanger::FixedSizeFont::glyph_files_metadata_file_name, metadata.dump(1, '\t'));

	if (progress)
		progress(codepoints.size(), codepoints.size());
}
