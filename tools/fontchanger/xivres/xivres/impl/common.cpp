#include "../include/xivres/common.h"

#include <nlohmann/json.hpp>

#include "../include/xivres/util.unicode.h"

void xivres::to_json(nlohmann::json& j, const game_language& value) {
	switch (value) {
		case game_language::Japanese:
			j = "Japanese";
			break;
		case game_language::English:
			j = "English";
			break;
		case game_language::German:
			j = "German";
			break;
		case game_language::French:
			j = "French";
			break;
		case game_language::ChineseSimplified:
			j = "ChineseSimplified";
			break;
		case game_language::ChineseTraditional:
			j = "ChineseTraditional";
			break;
		case game_language::Korean:
			j = "Korean";
			break;
		case game_language::TraditionalChinese:
			j = "TraditionalChinese";
			break;
		case game_language::Unspecified:
		default:
			j = "Unspecified"; // fallback
	}
}

void xivres::from_json(const nlohmann::json& j, game_language& newValue) {
	const auto newValueString = util::unicode::convert<std::string>(j.get<std::string>(), &util::unicode::lower);

	newValue = game_language::Unspecified;
	if (newValueString.empty())
		return;

	if (newValueString == "ja" || newValueString == "japanese")
		newValue = game_language::Japanese;
	else if (newValueString == "en" || newValueString == "english")
		newValue = game_language::English;
	else if (newValueString == "de" || newValueString == "deutsche" || newValueString == "german")
		newValue = game_language::German;
	else if (newValueString == "fr" || newValueString == "french")
		newValue = game_language::French;
	else if (newValueString == "chs" || newValueString == "chinese" || newValueString == "chinesesimplified" || newValueString == "zh")
		newValue = game_language::ChineseSimplified;
	else if (newValueString == "cht" || newValueString == "chinesetraditional")
		newValue = game_language::ChineseTraditional;
	else if (newValueString == "tc" || newValueString == "chinesetraditional2" || newValueString == "traditionalchinesetc" || newValueString == "traditionalchinese")
		newValue = game_language::TraditionalChinese;
	else if (newValueString == "ko" || newValueString == "korean")
		newValue = game_language::Korean;
}

void xivres::to_json(nlohmann::json& j, const game_publisher& value) {
	switch (value) {
		case game_publisher::SquareEnixJapan:
			j = "SquareEnixJapan";
			break;
		case game_publisher::SquareEnixAmerica:
			j = "SquareEnixAmerica";
			break;
		case game_publisher::SquareEnixEurope:
			j = "SquareEnixEurope";
			break;
		case game_publisher::ShengquGames:
			j = "ShengquGames";
			break;
		case game_publisher::ActozSoft:
			j = "ActozSoft";
			break;
		case game_publisher::UserjoyGames:
			j = "UserjoyGames";
			break;
		case game_publisher::Unspecified:
		default:
			j = "Unspecified";
	}
}

void xivres::from_json(const nlohmann::json& j, game_publisher& newValue) {
	const auto newValueString = util::unicode::convert<std::string>(j.get<std::string>(), &util::unicode::lower);

	newValue = game_publisher::Unspecified;
	if (newValueString.empty())
		return;

	if (newValueString == "squareenixjapan" || newValueString == "japan")
		newValue = game_publisher::SquareEnixJapan;
	else if (newValueString == "squareenixamerica" || newValueString == "northamerica")
		newValue = game_publisher::SquareEnixAmerica;
	else if (newValueString == "squareenixeurope" || newValueString == "europe")
		newValue = game_publisher::SquareEnixEurope;
	else if (newValueString == "shengqugames" || newValueString == "shandagames" || newValueString == "china")
		newValue = game_publisher::ShengquGames;
	else if (newValueString == "actozsoft" || newValueString == "korea")
		newValue = game_publisher::ActozSoft;
	else if (newValueString == "userjoygames" || newValueString == "taiwan")
		newValue = game_publisher::UserjoyGames;
}

void xivres::to_json(nlohmann::json& j, const game_release_publisher& value) {
	switch (value) {
		case game_release_publisher::SquareEnix:
			j = "SquareEnix";
			break;
		case game_release_publisher::ShengquGames:
			j = "ShengquGames";
			break;
		case game_release_publisher::ActozSoft:
			j = "ActozSoft";
			break;
		case game_release_publisher::UserjoyGames:
			j = "UserjoyGames";
			break;
		case game_release_publisher::Unspecified:
		default:
			j = "Unspecified";
	}
}

void xivres::from_json(const nlohmann::json& j, game_release_publisher& newValue) {
	const auto newValueString = util::unicode::convert<std::string>(j.get<std::string>(), &util::unicode::lower);

	newValue = game_release_publisher::Unspecified;
	if (newValueString.empty())
		return;

	if (newValueString == "squareenix" || newValueString == "international")
		newValue = game_release_publisher::SquareEnix;
	else if (newValueString == "shengqugames" || newValueString == "shandagames" || newValueString == "chinese")
		newValue = game_release_publisher::ShengquGames;
	else if (newValueString == "actozsoft" || newValueString == "korean")
		newValue = game_release_publisher::ActozSoft;
	else if (newValueString == "userjoygames" || newValueString == "taiwanese")
		newValue = game_release_publisher::UserjoyGames;
}

const char* xivres::game_language_code(game_language lang) {
	switch (lang) {
		case game_language::Unspecified: return nullptr;
		case game_language::Japanese: return "ja";
		case game_language::English: return "en";
		case game_language::German: return "de";
		case game_language::French: return "fr";
		case game_language::ChineseSimplified: return "chs";
		case game_language::ChineseTraditional: return "cht";
		case game_language::Korean: return "ko";
		case game_language::TraditionalChinese: return "tc";
		default: throw std::out_of_range("Invalid language");
	}
}
