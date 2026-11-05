#ifndef XIVRES_UTIL_BYTE_REGEX_H_
#define XIVRES_UTIL_BYTE_REGEX_H_

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <srell.hpp>

// Finding code by regular expressions over its bytes (SRELL): each byte is a character, \xNN matches one, and with
// dotall, . matches any. Matches may overlap: a search may look behind into what came before where it starts.
namespace xivres::util::byte_regex {
	// Compiles a pattern with dotall, so that . matches any byte.
	[[nodiscard]] srell::regex compile(std::string_view pattern);

	// Where a search for the next match goes on from: the byte after a match's start, or its end (the next byte if it
	// matched nothing).
	enum class resume {
		next_byte,
		match_end,
	};

	// Searches data from an offset on; lookbehind sees data before it.
	bool search(const srell::regex& pattern, std::span<const uint8_t> data, size_t from, srell::cmatch& match);

	// Where the search after a match goes on from, as an offset into data.
	[[nodiscard]] size_t resume_offset(const srell::cmatch& match, std::span<const uint8_t> data, resume how);

	// The first match, if any.
	[[nodiscard]] std::optional<srell::cmatch> find_first(const srell::regex& pattern, std::span<const uint8_t> data);

	// Every match, the next searched as resume says.
	[[nodiscard]] std::vector<srell::cmatch> find_all(const srell::regex& pattern, std::span<const uint8_t> data, resume how = resume::match_end);

	// The first match and whether there is another: Count is 0, 1, or 2 for more than one, which Second is the next of.
	struct unique_result {
		srell::cmatch First;
		srell::cmatch Second;
		size_t Count = 0;
	};

	[[nodiscard]] unique_result find_unique(const srell::regex& pattern, std::span<const uint8_t> data, resume how = resume::match_end);

	// Matches at the start of data only.
	bool match_at(const srell::regex& pattern, std::span<const uint8_t> data, srell::cmatch& match);
}

#endif
