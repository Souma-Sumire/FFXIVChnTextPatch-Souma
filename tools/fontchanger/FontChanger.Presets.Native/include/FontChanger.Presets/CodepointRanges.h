#pragma once

#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace FontChanger::CodepointRanges {
	// Inclusive, as in wrap_modifiers::Codepoints.
	using Ranges = std::vector<std::pair<char32_t, char32_t>>;

	// Reads ranges such as "U+3000-30FF", "0x41~5A", "\x41:5A", "U+20" or literal characters, separated by commas or
	// semicolons, into sorted and coalesced ranges.
	[[nodiscard]] Ranges Parse(std::wstring_view text);

	// Writes what Parse reads back.
	[[nodiscard]] std::wstring Format(const std::u32string& codepoints);

	[[nodiscard]] Ranges FromCodepoints(const std::set<char32_t>& codepoints);
	[[nodiscard]] Ranges FromCodepoints(const std::u32string& codepoints);

	// The result is sorted and coalesced, so Subtract(ranges, {}) normalizes ranges.
	[[nodiscard]] Ranges Subtract(const Ranges& ranges, const Ranges& others);

	[[nodiscard]] bool Contains(const Ranges& ranges, char32_t codepoint);

	[[nodiscard]] Ranges Intersect(const Ranges& ranges, const Ranges& others);

	// Scripts that are sized apart when fitting fonts to the game's.
	enum class ScriptGroup {
		NonCjk,
		Han,
		Kana,
		Hangul,
	};

	// Han takes what is drawn on the ideographic em box with it, such as radicals, CJK punctuation, bopomofo, and enclosed,
	// compatibility and fullwidth forms; NonCjk leaves out control characters and the private use area.
	[[nodiscard]] const Ranges& GetScriptGroupCodepoints(ScriptGroup group);
}
