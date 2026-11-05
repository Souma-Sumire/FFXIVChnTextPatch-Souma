#ifndef XIVRES_UNICODE_H_
#define XIVRES_UNICODE_H_

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace xivres::util::unicode {
	constexpr char32_t UReplacement = U'\uFFFD';
	constexpr char32_t UInvalid = U'\uFFFF';

	constexpr bool is_space(char32_t c) {
		return (0x09 <= c && c <= 0x0D) || c == 0x20 || c == 0x85 || c == 0xA0 || c == 0x1680
			|| (0x2000 <= c && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
	}

	char32_t u8uint32_to_u32(uint32_t n);

	uint32_t u32_to_u8uint32(char32_t codepoint);

	uint16_t u32_to_sjisuint16(char32_t codepoint);

	template<typename> struct EncodingTag {};

	size_t decode(EncodingTag<char8_t>, char32_t& out, const char8_t* in, size_t nRemainingBytes, bool strict);
	size_t decode(EncodingTag<char16_t>, char32_t& out, const char16_t* in, size_t nRemainingBytes, bool strict);
	size_t decode(EncodingTag<char32_t>, char32_t& out, const char32_t* in, size_t nRemainingBytes, bool strict);
	size_t decode(EncodingTag<char>, char32_t& out, const char* in, size_t nRemainingBytes, bool strict);
	size_t decode(EncodingTag<wchar_t>, char32_t& out, const wchar_t* in, size_t nRemainingBytes, bool strict);

	template<typename T>
	size_t decode(char32_t& out, const T* in, size_t nRemainingBytes, bool strict = false) {
		return decode(EncodingTag<T>(), out, in, nRemainingBytes, strict);
	}

	size_t encode(EncodingTag<char8_t>, char8_t* ptr, char32_t c, bool strict);
	size_t encode(EncodingTag<char16_t>, char16_t* ptr, char32_t c, bool strict);
	size_t encode(EncodingTag<char32_t>, char32_t* ptr, char32_t c, bool strict);
	size_t encode(EncodingTag<char>, char* ptr, char32_t c, bool strict);
	size_t encode(EncodingTag<wchar_t>, wchar_t* ptr, char32_t c, bool strict);

	template<typename T>
	size_t encode(T* ptr, char32_t c, bool strict = false) {
		return encode(EncodingTag<T>(), ptr, c, strict);
	}

	template<class TTo>
	TTo& convert_from_codepoint(TTo& out, char32_t c, bool strict = false) {
		const auto encLen = encode<typename TTo::value_type>(nullptr, c, strict);
		const auto baseIndex = out.size();
		out.resize(baseIndex + encLen);
		encode(&out[baseIndex], c, strict);
		return out;
	}

	template<class TTo>
	TTo convert_from_codepoint(char32_t c, bool strict = false) {
		TTo out{};
		return convert_from_codepoint(out, c, strict);
	}

	const char32_t* codepoint_name(char32_t c);

	template<class TTo>
	TTo& represent_codepoint(TTo& out, char32_t c, bool strict = false) {
		if (const auto name = codepoint_name(c))
			return convert<TTo>(out, name);
		return convert_from_codepoint<TTo>(out, c, strict);
	}

	template<class TTo>
	TTo represent_codepoint(char32_t c, bool strict = false) {
		TTo out{};
		return represent_codepoint(out, c, strict);
	}

	char32_t lower(char32_t in);
	char32_t upper(char32_t in);

	template<typename TElem>
	int strcmp(const TElem* l, const TElem* r, char32_t(*pfnCharMap)(char32_t) = nullptr, size_t lmaxlen = (std::numeric_limits<size_t>::max)(), size_t rmaxlen = (std::numeric_limits<size_t>::max)()) {
		size_t li = 0, ri = 0;

		while (li < lmaxlen && ri < rmaxlen) {
			if (!l[li] && !r[li])
				return 0;

			char32_t lc, rc;
			li += decode(lc, &l[li], lmaxlen - li);
			ri += decode(rc, &r[ri], rmaxlen - ri);
			if (pfnCharMap) {
				lc = pfnCharMap(lc);
				rc = pfnCharMap(rc);
			}
			if (lc > rc)
				return 1;
			if (lc < rc)
				return -1;
		}

		if (li > ri)
			return 1;
		if (li < ri)
			return -1;
		return 0;
	}

	template<class TTo, class TFromElem, class TFromTraits = std::char_traits<TFromElem>>
	TTo& convert(TTo& out, const std::basic_string_view<TFromElem, TFromTraits>& in, char32_t(*pfnCharMap)(char32_t) = nullptr, bool strict = false) {
		out.reserve(out.size() + in.size() * 4 / sizeof(in[0]) / sizeof(out[0]));

		char32_t c{};
		for (size_t decLen = 0, decIdx = 0; decIdx < in.size() && ((decLen = decode(c, &in[decIdx], in.size() - decIdx, strict))); decIdx += decLen) {
			if (pfnCharMap)
				c = pfnCharMap(c);
			
			const auto encIdx = out.size();
			const auto encLen = encode<typename TTo::value_type>(nullptr, c, strict);
			out.resize(encIdx + encLen);
			encode(&out[encIdx], c, strict);
		}

		return out;
	}

	template<class TTo, class TFromElem, class TFromTraits = std::char_traits<TFromElem>, class TFromAlloc = std::allocator<TFromElem>>
	TTo& convert(TTo& out, const std::basic_string<TFromElem, TFromTraits, TFromAlloc>& in, char32_t(*pfnCharMap)(char32_t) = nullptr, bool strict = false) {
		return convert(out, std::basic_string_view<TFromElem, TFromTraits>(in), pfnCharMap, strict);
	}

	template<class TTo, class TFromElem>
	TTo& convert(TTo& out, const TFromElem* in, size_t length = (std::numeric_limits<size_t>::max)(), char32_t(*pfnCharMap)(char32_t) = nullptr, bool strict = false) requires (std::is_integral_v<TFromElem>) {
		if (length == (std::numeric_limits<size_t>::max)())
			length = std::char_traits<TFromElem>::length(in);

		return convert(out, std::basic_string_view<TFromElem>(in, length), pfnCharMap, strict);
	}

	template<class TTo, class TFromElem, class TFromTraits = std::char_traits<TFromElem>>
	TTo convert(const std::basic_string_view<TFromElem, TFromTraits>& in, char32_t(*pfnCharMap)(char32_t) = nullptr, bool strict = false) {
		TTo out{};
		return convert(out, in, pfnCharMap, strict);
	}

	template<class TTo, class TFromElem, class TFromTraits = std::char_traits<TFromElem>, class TFromAlloc = std::allocator<TFromElem>>
	TTo convert(const std::basic_string<TFromElem, TFromTraits, TFromAlloc>& in, char32_t(*pfnCharMap)(char32_t) = nullptr, bool strict = false) {
		TTo out{};
		return convert(out, std::basic_string_view<TFromElem, TFromTraits>(in), pfnCharMap, strict);
	}

	template<class TTo, class TFromElem>
	TTo convert(const TFromElem* in, size_t length = (std::numeric_limits<size_t>::max)(), char32_t(*pfnCharMap)(char32_t) = nullptr, bool strict = false) requires (std::is_integral_v<TFromElem>) {
		if (length == (std::numeric_limits<size_t>::max)())
			length = std::char_traits<TFromElem>::length(in);

		TTo out{};
		return convert(out, std::basic_string_view<TFromElem>(in, length), pfnCharMap, strict);
	}
	
	template <typename T>
	concept HasNative = requires(const T& t) { t.native(); t.c_str(); };

	template <class TTo>
	TTo convert(const std::filesystem::path& in, char32_t(*pfnCharMap)(char32_t) = nullptr, bool strict = false) {
		return convert<TTo>(in.native(), pfnCharMap, strict);
	}
	
	template <class TElem, class TTraits = std::char_traits<TElem>, class TAlloc = std::allocator<TElem>>
	std::basic_string<TElem, TTraits, TAlloc> convert(std::basic_string<TElem, TTraits, TAlloc> in) {
		return in;
	}

	namespace blocks {
		enum purpose_flags : uint64_t {
			LTR = 0,
			RTL = 1 << 0,
			Invalid = 1 << 1,
			UsedWithCombining = 1 << 22,
		};

		constexpr purpose_flags operator|(purpose_flags a, purpose_flags b) {
			return static_cast<purpose_flags>(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
		}

		constexpr purpose_flags operator&(purpose_flags a, purpose_flags b) {
			return static_cast<purpose_flags>(static_cast<uint64_t>(a) & static_cast<uint64_t>(b));
		}

		constexpr purpose_flags operator~(purpose_flags a) {
			return static_cast<purpose_flags>(~static_cast<uint64_t>(a));
		}

		enum negative_lsb_group : uint64_t {
			None,
			Combining,
			Cyrillic,
			Thai,
		};

		struct block_definition {
			char32_t First;
			char32_t Last;
			const char* Name;
			purpose_flags Purpose;
			negative_lsb_group NegativeLsbGroup = None;
		};

		std::span<const block_definition> all_blocks();

		const block_definition& block_for(char32_t codepoint);
	}
}

#endif
