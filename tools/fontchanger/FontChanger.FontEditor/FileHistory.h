#pragma once

struct FileHistory {
	static constexpr size_t MaxFiles = 10;

	std::vector<std::filesystem::path> Files;

	static std::filesystem::path GetHistoryPath();

	static FileHistory Load();
	void Save() const;

	static void Add(const std::filesystem::path& path) noexcept;
	static void Remove(const std::filesystem::path& path);
	static void Clear();
};
