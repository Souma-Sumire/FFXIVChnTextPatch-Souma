#include "../include/xivres/util.byte_regex.h"

namespace {
	const char* chars(std::span<const uint8_t> data) {
		return reinterpret_cast<const char*>(data.data());
	}
}

srell::regex xivres::util::byte_regex::compile(std::string_view pattern) {
	return {pattern.data(), pattern.size(), srell::regex_constants::dotall};
}

bool xivres::util::byte_regex::search(const srell::regex& pattern, std::span<const uint8_t> data, size_t from, srell::cmatch& match) {
	if (from >= data.size())
		return false;
	const auto begin = chars(data);
	return srell::regex_search(begin + from, begin + data.size(), begin, match, pattern);
}

size_t xivres::util::byte_regex::resume_offset(const srell::cmatch& match, std::span<const uint8_t> data, resume how) {
	const auto begin = chars(data);
	const auto first = static_cast<size_t>(match[0].first - begin);
	const auto last = static_cast<size_t>(match[0].second - begin);
	return how == resume::match_end && last != first ? last : first + 1;
}

std::optional<srell::cmatch> xivres::util::byte_regex::find_first(const srell::regex& pattern, std::span<const uint8_t> data) {
	if (srell::cmatch match; search(pattern, data, 0, match))
		return match;
	return std::nullopt;
}

std::vector<srell::cmatch> xivres::util::byte_regex::find_all(const srell::regex& pattern, std::span<const uint8_t> data, resume how) {
	std::vector<srell::cmatch> res;
	srell::cmatch match;
	for (size_t from = 0; search(pattern, data, from, match); from = resume_offset(match, data, how))
		res.push_back(match);
	return res;
}

xivres::util::byte_regex::unique_result xivres::util::byte_regex::find_unique(const srell::regex& pattern, std::span<const uint8_t> data, resume how) {
	unique_result res;
	if (!search(pattern, data, 0, res.First))
		return res;
	res.Count = search(pattern, data, resume_offset(res.First, data, how), res.Second) ? 2 : 1;
	return res;
}

bool xivres::util::byte_regex::match_at(const srell::regex& pattern, std::span<const uint8_t> data, srell::cmatch& match) {
	const auto begin = chars(data);
	return srell::regex_search(begin, begin + data.size(), match, pattern, srell::regex_constants::match_continuous);
}
