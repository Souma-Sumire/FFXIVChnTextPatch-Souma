#include "pch.h"
#include "FileHistory.h"
#include "FontGeneratorConfig.h"

namespace {
	bool IsSamePath(const std::filesystem::path& l, const std::filesystem::path& r) {
		const auto& lw = l.native();
		const auto& rw = r.native();
		return CompareStringOrdinal(lw.data(), static_cast<int>(lw.size()), rw.data(), static_cast<int>(rw.size()), TRUE) == CSTR_EQUAL;
	}

	std::filesystem::path Normalize(const std::filesystem::path& path) {
		std::error_code ec;
		const auto res = std::filesystem::absolute(path, ec);
		return (ec ? path : res).lexically_normal();
	}
}

std::filesystem::path FileHistory::GetHistoryPath() {
	return FontGeneratorConfig::GetConfigPath().parent_path() / "history.json";
}

FileHistory FileHistory::Load() {
	FileHistory res;
	try {
		std::ifstream in(GetHistoryPath(), std::ios::binary);
		if (!in)
			return res;

		const auto json = nlohmann::json::parse(in);
		if (const auto it = json.find("recentFiles"); it != json.end() && it->is_array()) {
			for (const auto& v : *it) {
				if (v.is_string() && res.Files.size() < MaxFiles)
					res.Files.emplace_back(xivres::util::unicode::convert<std::wstring>(v.get<std::string>()));
			}
		}
	} catch (...) {
		res.Files.clear();
	}
	return res;
}

void FileHistory::Save() const {
	if (Files.empty()) {
		std::error_code ec;
		std::filesystem::remove(GetHistoryPath(), ec);
		return;
	}

	auto files = nlohmann::json::array();
	for (const auto& path : Files)
		files.emplace_back(xivres::util::unicode::convert<std::string>(path.wstring()));

	auto json = nlohmann::json::object();
	json.emplace("recentFiles", std::move(files));

	std::ofstream out(GetHistoryPath(), std::ios::binary);
	out << json.dump(1, '\t');
}

void FileHistory::Add(const std::filesystem::path& path) noexcept {
	try {
		const auto normalized = Normalize(path);
		auto history = Load();
		std::erase_if(history.Files, [&](const auto& p) { return IsSamePath(p, normalized); });
		history.Files.insert(history.Files.begin(), normalized);
		if (history.Files.size() > MaxFiles)
			history.Files.resize(MaxFiles);
		history.Save();
	} catch (...) {
		// The history is not worth failing to open or save a file for.
	}
}

void FileHistory::Remove(const std::filesystem::path& path) {
	const auto normalized = Normalize(path);
	auto history = Load();
	if (std::erase_if(history.Files, [&](const auto& p) { return IsSamePath(p, normalized); }))
		history.Save();
}

void FileHistory::Clear() {
	FileHistory{}.Save();
}
