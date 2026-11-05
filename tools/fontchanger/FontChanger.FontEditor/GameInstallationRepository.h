#pragma once

namespace App {
	// Vendors of the releases of the game; those after None are in the order of FontGeneratorConfig::GameReleases.
	enum class GameReleaseVendor {
		None,
		SquareEnix,
		ShandaGames,
		ActozSoft,
		UserjoyGames,
	};

	namespace GameInstallationRepository {
		// Finds the release of the game installed at a path or a folder under it, and its game folder.
		GameReleaseVendor DetermineGameRelease(std::filesystem::path path, std::filesystem::path& normalizedPath);

		// Finds the releases of the game that the registry knows of.
		std::vector<std::pair<GameReleaseVendor, std::filesystem::path>> AutoDetectInstalledGameReleases();
	}
}
