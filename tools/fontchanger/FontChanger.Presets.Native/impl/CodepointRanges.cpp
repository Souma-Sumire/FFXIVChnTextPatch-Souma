#include "pch.h"
#include "FontChanger.Presets/CodepointRanges.h"

namespace {
	bool IsAsciiSpace(char32_t c) {
		return c < 128 && std::isspace(static_cast<int>(c));
	}

	std::u32string_view Trim(std::u32string_view s) {
		while (!s.empty() && IsAsciiSpace(s.front()))
			s.remove_prefix(1);
		while (!s.empty() && IsAsciiSpace(s.back()))
			s.remove_suffix(1);
		return s;
	}

	bool TrimHexPrefix(std::u32string_view& s) {
		for (const auto prefix : {U"0x", U"0X", U"U+", U"u+", U"\\x", U"\\X"}) {
			if (s.starts_with(prefix)) {
				s.remove_prefix(2);
				return true;
			}
		}
		return false;
	}

	char32_t ParseHex(std::u32string_view s) {
		return static_cast<char32_t>(std::strtol(xivres::util::unicode::convert<std::string>(Trim(s)).c_str(), nullptr, 16));
	}

	FontChanger::CodepointRanges::Ranges Normalize(FontChanger::CodepointRanges::Ranges ranges) {
		std::ranges::sort(ranges);
		FontChanger::CodepointRanges::Ranges res;
		for (const auto& [c1, c2] : ranges) {
			if (!res.empty() && res.back().second + 1 >= c1)
				res.back().second = (std::max)(res.back().second, c2);
			else
				res.emplace_back(c1, c2);
		}
		return res;
	}
}

FontChanger::CodepointRanges::Ranges FontChanger::CodepointRanges::Parse(std::wstring_view text) {
	const auto input = xivres::util::unicode::convert<std::u32string>(text);
	Ranges ranges;
	for (size_t i = 0; i <= input.size();) {
		auto next = input.find_first_of(U",;", i);
		if (next == std::u32string::npos)
			next = input.size();
		auto part = Trim(std::u32string_view(input).substr(i, next - i));
		i = next + 1;

		if (!TrimHexPrefix(part)) {
			for (const auto c : part)
				ranges.emplace_back(c, c);
			continue;
		}

		if (const auto sep = part.find_first_of(U"-~:"); sep != std::u32string_view::npos) {
			auto part2 = Trim(part.substr(sep + 1));
			TrimHexPrefix(part2);
			const auto c1 = ParseHex(part.substr(0, sep));
			const auto c2 = ParseHex(part2);
			ranges.emplace_back((std::min)(c1, c2), (std::max)(c1, c2));
		} else {
			const auto c = ParseHex(part);
			ranges.emplace_back(c, c);
		}
	}
	return Normalize(std::move(ranges));
}

std::wstring FontChanger::CodepointRanges::Format(const std::u32string& codepoints) {
	std::wstring res;
	for (size_t i = 0; i < codepoints.size(); i++) {
		const auto first = codepoints[i];
		while (i + 1 < codepoints.size() && codepoints[i + 1] == codepoints[i] + 1)
			i++;

		if (!res.empty())
			res += L", ";
		if (first == codepoints[i])
			res += std::format(L"U+{:04X}", static_cast<uint32_t>(first));
		else
			res += std::format(L"U+{:04X}-{:04X}", static_cast<uint32_t>(first), static_cast<uint32_t>(codepoints[i]));
	}
	return res;
}

FontChanger::CodepointRanges::Ranges FontChanger::CodepointRanges::FromCodepoints(const std::set<char32_t>& codepoints) {
	Ranges res;
	for (const auto c : codepoints) {
		if (!res.empty() && res.back().second + 1 == c)
			res.back().second = c;
		else
			res.emplace_back(c, c);
	}
	return res;
}

FontChanger::CodepointRanges::Ranges FontChanger::CodepointRanges::FromCodepoints(const std::u32string& codepoints) {
	return FromCodepoints(std::set<char32_t>(codepoints.begin(), codepoints.end()));
}

FontChanger::CodepointRanges::Ranges FontChanger::CodepointRanges::Subtract(const Ranges& ranges, const Ranges& others) {
	const auto removed = Normalize(others);
	Ranges res;
	for (auto [a, b] : Normalize(ranges)) {
		for (const auto& [c1, c2] : removed) {
			if (c2 < a || b < c1)
				continue;
			if (a < c1)
				res.emplace_back(a, c1 - 1);
			a = c2 + 1;
		}
		if (a <= b)
			res.emplace_back(a, b);
	}
	return res;
}

bool FontChanger::CodepointRanges::Contains(const Ranges& ranges, char32_t codepoint) {
	return std::ranges::any_of(ranges, [codepoint](const auto& r) { return r.first <= codepoint && codepoint <= r.second; });
}

FontChanger::CodepointRanges::Ranges FontChanger::CodepointRanges::Intersect(const Ranges& ranges, const Ranges& others) {
	return Subtract(ranges, Subtract(ranges, others));
}

const FontChanger::CodepointRanges::Ranges& FontChanger::CodepointRanges::GetScriptGroupCodepoints(ScriptGroup group) {
	static const Ranges han{
		{0x2E80, 0x2FDF}, {0x2FF0, 0x303F}, {0x3100, 0x312F}, {0x3190, 0x31EF}, {0x3200, 0x33FF}, {0x3400, 0x4DBF},
		{0x4E00, 0x9FFF}, {0xF900, 0xFAFF}, {0xFE30, 0xFE4F}, {0xFF01, 0xFF60}, {0xFFE0, 0xFFE6}, {0x20000, 0x3FFFF},
	};
	static const Ranges kana{{0x3040, 0x30FF}, {0x31F0, 0x31FF}, {0xFF65, 0xFF9F}, {0x1AFF0, 0x1B16F}};
	static const Ranges hangul{{0x1100, 0x11FF}, {0x3130, 0x318F}, {0xA960, 0xA97F}, {0xAC00, 0xD7FF}, {0xFFA0, 0xFFDC}};
	static const Ranges nonCjk = [] {
		auto others = han;
		others.insert(others.end(), kana.begin(), kana.end());
		others.insert(others.end(), hangul.begin(), hangul.end());
		others.emplace_back(0xE000, 0xF8FF);
		return Subtract({{0x20, 0x10FFFF}}, others);
	}();

	switch (group) {
		case ScriptGroup::Han: return han;
		case ScriptGroup::Kana: return kana;
		case ScriptGroup::Hangul: return hangul;
		default: return nonCjk;
	}
}
