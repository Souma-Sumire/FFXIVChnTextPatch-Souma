/*
 *
 * TinySHA1 - a header only implementation of the SHA1 algorithm in C++. Based
 * on the implementation in boost::uuid::details.
 *
 * SHA1 Wikipedia Page: http://en.wikipedia.org/wiki/SHA-1
 *
 * Copyright (c) 2012-22 SAURAV MOHAPATRA <mohaps@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef XIVRES_INTERNAL_TINYSHA1_H_
#define XIVRES_INTERNAL_TINYSHA1_H_

#include <cstdint>
#include <cstring>
#include <span>

#include "common.h"
#include "util.h"

namespace xivres::util {
	class hash_sha1 {
	public:
		typedef uint32_t digest32_t[5];
		typedef uint8_t digest8_t[20];

	private:
		static constexpr size_t BlockSize = 64;

		digest32_t m_digest{};
		uint8_t m_block[BlockSize]{};
		size_t m_blockByteIndex = 0;
		uint64_t m_byteCount = 0;

	public:
		hash_sha1() {
			reset();
		}

		hash_sha1(const hash_sha1&) = default;
		hash_sha1& operator=(const hash_sha1&) = default;
		hash_sha1(hash_sha1&&) = delete;
		hash_sha1& operator=(hash_sha1&&) = delete;
		~hash_sha1() = default;

		hash_sha1& reset() {
			m_digest[0] = 0x67452301;
			m_digest[1] = 0xEFCDAB89;
			m_digest[2] = 0x98BADCFE;
			m_digest[3] = 0x10325476;
			m_digest[4] = 0xC3D2E1F0;
			m_blockByteIndex = 0;
			m_byteCount = 0;
			return *this;
		}

		hash_sha1& process_byte(uint8_t octet) {
			return process_bytes(&octet, 1);
		}

		hash_sha1& process_block(const void* const start, const void* const end) {
			return process_bytes(start, static_cast<size_t>(static_cast<const uint8_t*>(end) - static_cast<const uint8_t*>(start)));
		}

		hash_sha1& process_bytes(const void* const data, size_t len) {
			auto p = static_cast<const uint8_t*>(data);
			m_byteCount += len;

			if (m_blockByteIndex) {
				const auto available = (std::min)(len, BlockSize - m_blockByteIndex);
				std::memcpy(&m_block[m_blockByteIndex], p, available);
				m_blockByteIndex += available;
				p += available;
				len -= available;
				if (m_blockByteIndex < BlockSize)
					return *this;
				m_blockByteIndex = 0;
				process_block(m_block);
			}

			for (; len >= BlockSize; p += BlockSize, len -= BlockSize)
				process_block(p);

			std::memcpy(m_block, p, len);
			m_blockByteIndex = len;
			return *this;
		}

		const uint32_t* get_digest(digest32_t digest) const {
			auto finished = *this;
			const auto bitCount = m_byteCount * 8;

			uint8_t padding[BlockSize + 8]{0x80};
			const auto zeroes = (BlockSize * 2 - 8 - 1 - m_blockByteIndex) % BlockSize;
			for (size_t i = 0; i < 8; ++i)
				padding[1 + zeroes + i] = static_cast<uint8_t>(bitCount >> (56 - 8 * i));
			finished.process_bytes(padding, 1 + zeroes + 8);

			std::memcpy(digest, finished.m_digest, sizeof finished.m_digest);
			return digest;
		}

		const uint8_t* get_digest_bytes(digest8_t digest) const {
			digest32_t d32;
			get_digest(d32);
			for (size_t i = 0; i < 5; ++i) {
				digest[i * 4 + 0] = static_cast<uint8_t>(d32[i] >> 24);
				digest[i * 4 + 1] = static_cast<uint8_t>(d32[i] >> 16);
				digest[i * 4 + 2] = static_cast<uint8_t>(d32[i] >> 8);
				digest[i * 4 + 3] = static_cast<uint8_t>(d32[i]);
			}
			return digest;
		}

	private:
		void process_block(const uint8_t* block) {
			uint32_t w[80];
			for (size_t i = 0; i < 16; i++) {
				w[i] = static_cast<uint32_t>(block[i * 4 + 0]) << 24;
				w[i] |= static_cast<uint32_t>(block[i * 4 + 1]) << 16;
				w[i] |= static_cast<uint32_t>(block[i * 4 + 2]) << 8;
				w[i] |= static_cast<uint32_t>(block[i * 4 + 3]);
			}
			for (size_t i = 16; i < 80; i++) {
				w[i] = left_rotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
			}

			uint32_t a = m_digest[0];
			uint32_t b = m_digest[1];
			uint32_t c = m_digest[2];
			uint32_t d = m_digest[3];
			uint32_t e = m_digest[4];

			for (std::size_t i = 0; i < 80; ++i) {
				uint32_t f;
				uint32_t k;

				if (i < 20) {
					f = (b & c) | (~b & d);
					k = 0x5A827999;
				} else if (i < 40) {
					f = b ^ c ^ d;
					k = 0x6ED9EBA1;
				} else if (i < 60) {
					f = (b & c) | (b & d) | (c & d);
					k = 0x8F1BBCDC;
				} else {
					f = b ^ c ^ d;
					k = 0xCA62C1D6;
				}
				const uint32_t temp = left_rotate(a, 5) + f + e + k + w[i];
				e = d;
				d = c;
				c = left_rotate(b, 30);
				b = a;
				a = temp;
			}

			m_digest[0] += a;
			m_digest[1] += b;
			m_digest[2] += c;
			m_digest[3] += d;
			m_digest[4] += e;
		}

		static uint32_t left_rotate(uint32_t value, size_t count) {
			return (value << count) ^ (value >> (32 - count));
		}
	};
}

namespace xivres {
	struct sha1_value {
		uint8_t Value[20]{};

		bool operator==(const sha1_value& r) const {
			return std::memcmp(r.Value, Value, sizeof Value) == 0;
		}

		bool operator!=(const sha1_value& r) const {
			return std::memcmp(r.Value, Value, sizeof Value) != 0;
		}

		bool operator<(const sha1_value& r) const {
			return std::memcmp(r.Value, Value, sizeof Value) < 0;
		}

		bool operator<=(const sha1_value& r) const {
			return std::memcmp(r.Value, Value, sizeof Value) <= 0;
		}

		bool operator>(const sha1_value& r) const {
			return std::memcmp(r.Value, Value, sizeof Value) > 0;
		}

		bool operator>=(const sha1_value& r) const {
			return std::memcmp(r.Value, Value, sizeof Value) >= 0;
		}

		bool operator==(const char (&r)[20]) const {
			return std::memcmp(r, Value, sizeof Value) == 0;
		}

		bool operator!=(const char (&r)[20]) const {
			return std::memcmp(r, Value, sizeof Value) != 0;
		}

		[[nodiscard]] bool IsZero() const {
			return util::all_same_value(Value);
		}

		void set_from_ptr(const void* data, size_t size) {
			util::hash_sha1 hasher;
			hasher.process_bytes(data, size);
			hasher.get_digest_bytes(Value);
		}

		template<typename T>
		void set_from(std::span<T> data) {
			set_from_ptr(data.data(), data.size_bytes());
		}

		template<typename ...Args>
		void set_from_span(Args ...args) {
			set_from(std::span(std::forward<Args>(args)...));
		}

		void verify(const void* data, size_t size, const char* errorMessage) const {
			sha1_value t;
			t.set_from_ptr(data, size);
			if (*this != t) {
				if (!size && util::all_same_value(Value)) // all zero values can be in place of SHA-1 value of empty value
					return;
				throw bad_data_error(errorMessage);
			}
		}

		template<typename T>
		void verify(std::span<T> data, const char* errorMessage) const {
			verify(data.data(), data.size_bytes(), errorMessage);
		}
	};
}

#endif
