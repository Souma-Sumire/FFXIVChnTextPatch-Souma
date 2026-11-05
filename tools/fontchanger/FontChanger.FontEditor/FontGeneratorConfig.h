#pragma once

#include <nlohmann/json_fwd.hpp>

struct FontGeneratorConfig {
	// A release of the game: the key of its installations in the configuration file, the font type of its own fonts, and
	// the string resource of the name of its vendor.
	struct GameRelease {
		const char* JsonKey;
		xivres::font_type FontType;
		UINT VendorNameResId;
	};

	// The releases, in the order of GamePaths and of App::GameReleaseVendor. The global one comes first.
	static const std::array<GameRelease, 4> GameReleases;

	// Installations of the game, by release.
	std::array<std::vector<std::filesystem::path>, 4> GamePaths;

	std::string LanguageCode;

	int FontElementListViewHeight;
	int PreviewEditHeight;

	bool Dirty;

	static const FontGeneratorConfig Default;

	static std::filesystem::path GetConfigPath();

	// Reads the configuration file, or makes it from the default if there is none.
	static FontGeneratorConfig Load();

	// Gets the installations to read the game fonts of a font type from.
	[[nodiscard]] std::vector<std::filesystem::path> GetGamePaths(xivres::font_type fontType) const;

	void MarkDirty();
	void SaveIfDirty();

	void Save() const;
};

void from_json(const nlohmann::json& json, FontGeneratorConfig& value);
void to_json(nlohmann::json& json, const FontGeneratorConfig& value);
