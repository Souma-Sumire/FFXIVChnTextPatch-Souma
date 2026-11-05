#include "pch.h"
#include "FontGeneratorConfig.h"
#include "MainWindow.Internal.h"
#include "resource.h"

const std::array<FontGeneratorConfig::GameRelease, 4> FontGeneratorConfig::GameReleases{{
	{"global", xivres::font_type::font, IDS_GAMEINSTALLATIONLISTVIEW_COLUMN_VENDOR_SQEX},
	{"china", xivres::font_type::chn_axis, IDS_GAMEINSTALLATIONLISTVIEW_COLUMN_VENDOR_SNDA},
	{"korea", xivres::font_type::krn_axis, IDS_GAMEINSTALLATIONLISTVIEW_COLUMN_VENDOR_ACTOZ},
	{"traditionalchinese", xivres::font_type::tc_axis, IDS_GAMEINSTALLATIONLISTVIEW_COLUMN_VENDOR_USERJOY},
}};

const FontGeneratorConfig FontGeneratorConfig::Default{
	.GamePaths = {{
		{
			R"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game)",
		},
		{
			u8R"(C:\Program Files (x86)\上海数龙科技有限公司\最终幻想XIV\game)",
			R"(C:\Program Files (x86)\SNDA\FFXIV\game)",
		},
		{
			R"(C:\Program Files (x86)\FINAL FANTASY XIV - KOREA\game)",
		},
		{
			R"(C:\Program Files\USERJOY GAMES\FINAL FANTASY XIV TC\game)",
		},
	}},
	.FontElementListViewHeight = ::FontElementListViewHeight,
	.PreviewEditHeight = ::PreviewEditHeight,
};

void from_json(const nlohmann::json& json, FontGeneratorConfig& value) {
	value = {};
	for (size_t i = 0; i < FontGeneratorConfig::GameReleases.size(); i++) {
		if (auto it = json.find(FontGeneratorConfig::GameReleases[i].JsonKey); it != json.end() && it->is_array()) {
			for (const auto& [_, p] : it->items())
				value.GamePaths[i].emplace_back(xivres::util::unicode::convert<std::wstring>(p.get<std::string>()));
		}
	}

	value.FontElementListViewHeight = (std::max)(
		json.value("FontElementListViewHeight", json.value("FaceElementListViewHeight", FontElementListViewHeight)),
		MinFontElementListViewHeight);
	value.PreviewEditHeight = (std::max)(
		json.value("PreviewEditHeight", PreviewEditHeight),
		MinPreviewEditHeight);

	value.LanguageCode = json.value("Language", "");
}

void to_json(nlohmann::json& json, const FontGeneratorConfig& value) {
	json = nlohmann::json::object();

	for (size_t i = 0; i < FontGeneratorConfig::GameReleases.size(); i++) {
		auto arr = nlohmann::json::array();
		for (const auto& p : value.GamePaths[i])
			arr.emplace_back(xivres::util::unicode::convert<std::string>(p.wstring()));
		json.emplace(FontGeneratorConfig::GameReleases[i].JsonKey, std::move(arr));
	}

	json.emplace("FontElementListViewHeight", value.FontElementListViewHeight);
	json.emplace("PreviewEditHeight", value.PreviewEditHeight);

	json.emplace("Language", value.LanguageCode);
}

std::filesystem::path FontGeneratorConfig::GetConfigPath() {
	std::wstring path(PATHCCH_MAX_CCH + 1, L'\0');
	path.resize(
		GetModuleFileNameW(
			GetModuleHandle(nullptr),
			path.data(),
			static_cast<DWORD>(path.size())));

	return std::filesystem::path(path).parent_path() / "config.json";
}

FontGeneratorConfig FontGeneratorConfig::Load() {
	if (!exists(GetConfigPath())) {
		Default.Save();
		return Default;
	}

	FontGeneratorConfig res = Default;
	if (std::ifstream configFile(GetConfigPath()); configFile)
		from_json(nlohmann::json::parse(configFile), res);
	return res;
}

std::vector<std::filesystem::path> FontGeneratorConfig::GetGamePaths(xivres::font_type fontType) const {
	// The releases other than the global one have fonts of their own.
	for (size_t i = 1; i < GameReleases.size(); i++) {
		if (GameReleases[i].FontType == fontType)
			return GamePaths[i];
	}

	// Every release has the fonts of the global one.
	std::vector<std::filesystem::path> paths;
	for (const auto& list : GamePaths)
		paths.insert(paths.end(), list.begin(), list.end());
	return paths;
}

void FontGeneratorConfig::MarkDirty() {
	Dirty = true;
}

void FontGeneratorConfig::SaveIfDirty() {
	if (!Dirty)
		return;

	Dirty = false;
	Save();
}

void FontGeneratorConfig::Save() const {
	std::ofstream configFile(GetConfigPath());
	nlohmann::json json;
	to_json(json, *this);
	configFile << json;
}
