#ifndef XIVRES_INTERNAL_MISC_H_
#define XIVRES_INTERNAL_MISC_H_

#include <algorithm>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "util.unicode.h"

namespace xivres::util {
	template<typename T>
	T clamp(T value, T minValue, T maxValue) {
		return (std::min)(maxValue, (std::max)(minValue, value));
	}

	template<typename T, typename TFrom>
	T range_check_cast(TFrom value) {
		if (value < (std::numeric_limits<T>::min)())
			throw std::range_error("Out of range");
		if (value > (std::numeric_limits<T>::max)())
			throw std::range_error("Out of range");
		return static_cast<T>(value);
	}

	template<typename T, size_t C>
	bool all_same_value(T(&arr)[C], std::remove_cv_t<T> supposedValue = 0) {
		for (size_t i = 0; i < C; ++i) {
			if (arr[i] != supposedValue)
				return false;
		}
		return true;
	}

	template<typename T>
	bool all_same_value(std::span<T> arr, std::remove_cv_t<T> supposedValue = 0) {
		for (const auto& e : arr)
			if (e != supposedValue)
				return false;
		return true;
	}

	template<class TElem, class TTraits, class TAlloc>
	[[nodiscard]] std::vector<std::basic_string<TElem, TTraits, TAlloc>> split(const std::basic_string<TElem, TTraits, TAlloc>& str, const std::basic_string<TElem, TTraits, TAlloc>& delimiter, size_t maxSplit = SIZE_MAX) {
		std::vector<std::basic_string<TElem, TTraits, TAlloc>> result;
		if (delimiter.empty()) {
			for (size_t i = 0; i < str.size(); ++i)
				result.push_back(str.substr(i, 1));
		} else {
			size_t previousOffset = 0, offset;
			while (maxSplit && (offset = str.find(delimiter, previousOffset)) != std::string::npos) {
				result.push_back(str.substr(previousOffset, offset - previousOffset));
				previousOffset = offset + delimiter.length();
				--maxSplit;
			}
			result.push_back(str.substr(previousOffset));
		}
		return result;
	}

	template<class TElem, class TTraits, class TAlloc>
	[[nodiscard]] std::basic_string<TElem, TTraits, TAlloc> trim(const std::basic_string<TElem, TTraits, TAlloc>& s, bool left = true, bool right = true) {
		size_t firstNonSpace = s.size(), lastNonSpaceEnd = 0;
		for (size_t i = 0; i < s.size();) {
			char32_t c;
			const auto length = unicode::decode(c, &s[i], s.size() - i, true);
			if (!unicode::is_space(c)) {
				firstNonSpace = (std::min)(firstNonSpace, i);
				lastNonSpaceEnd = i + length;
			}
			i += length;
		}

		const auto from = left ? firstNonSpace : 0;
		const auto to = right ? lastNonSpaceEnd : s.size();
		return from < to ? s.substr(from, to - from) : std::basic_string<TElem, TTraits, TAlloc>();
	}

	template<class TElem, class TTraits, class TAlloc>
	[[nodiscard]] std::basic_string<TElem, TTraits, TAlloc> trim_ascii(const std::basic_string<TElem, TTraits, TAlloc>& s, bool left = true, bool right = true) {
		const auto isSpace = [](TElem c) { return c == 0x20 || (0x09 <= c && c <= 0x0D); };
		size_t from = 0, to = s.size();
		if (left)
			while (from < to && isSpace(s[from]))
				++from;
		if (right)
			while (from < to && isSpace(s[to - 1]))
				--to;
		return s.substr(from, to - from);
	}

	template<class TElem, class TTraits, class TAlloc>
	[[nodiscard]] std::basic_string<TElem, TTraits, TAlloc> replace(const std::basic_string<TElem, TTraits, TAlloc>& source, const std::basic_string<TElem, TTraits, TAlloc>& from, const std::basic_string<TElem, TTraits, TAlloc>& to) {
		std::basic_string<TElem, TTraits, TAlloc> s;
		s.reserve(source.length());

		size_t last = 0;
		size_t pos;

		while (std::string::npos != (pos = source.find(from, last))) {
			s.append(&source[last], &source[pos]);
			s += to;
			last = pos + from.length();
		}

		s += source.substr(last);
		return s;
	}
}

#endif
