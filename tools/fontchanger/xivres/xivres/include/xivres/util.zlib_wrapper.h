#ifndef XIVRES_INTERNAL_ZLIBWRAPPER_H_
#define XIVRES_INTERNAL_ZLIBWRAPPER_H_

#include <cstdint>
#include <format>
#include <stdexcept>
#include <span>
#include <utility>
#include <vector>
#include <zlib.h>

#include "util.thread_pool.h"

namespace xivres::util {
	class zlib_error : public std::runtime_error {
	public:
		static std::string zlib_error_to_string(int code);

		explicit zlib_error(int returnCode);
	};

	class zlib_inflater {
		const int m_windowBits;
		const size_t m_defaultBufferSize;
		z_stream m_zstream{};
		bool m_initialized = false;
		std::vector<uint8_t> m_buffer;

		void initialize_inflation();

	public:
		zlib_inflater(int windowBits = 15, int defaultBufferSize = 16384);
		zlib_inflater(zlib_inflater&&) = delete;
		zlib_inflater(const zlib_inflater&) = delete;
		zlib_inflater& operator=(zlib_inflater&&) = delete;
		zlib_inflater& operator=(const zlib_inflater&) = delete;
		~zlib_inflater();

		[[nodiscard]] bool is(int windowBits = 15, int defaultBufferSize = 16384) const {
			return m_windowBits == windowBits && std::cmp_equal(m_defaultBufferSize, defaultBufferSize);
		}

		std::span<uint8_t> operator()(std::span<const uint8_t> source);

		std::span<uint8_t> operator()(std::span<const uint8_t> source, size_t maxSize);

		std::span<uint8_t> operator()(std::span<const uint8_t> source, std::span<uint8_t> target);

		static thread_pool::object_pool<zlib_inflater>::scoped_pooled_object pooled();
	};

	class zlib_deflater {
		const int m_level;
		const int m_method;
		const int m_windowBits;
		const int m_memLevel;
		const int m_strategy;
		const size_t m_defaultBufferSize;
		z_stream m_zstream{};
		bool m_initialized = false;
		std::vector<uint8_t> m_buffer;

		std::span<uint8_t> m_latestResult;

		void initialize_deflation();

	public:
		zlib_deflater(
			int level = Z_DEFAULT_COMPRESSION,
			int method = Z_DEFLATED,
			int windowBits = 15,
			int memLevel = 8,
			int strategy = Z_DEFAULT_STRATEGY,
			size_t defaultBufferSize = 16384);
		zlib_deflater(zlib_deflater&&) = delete;
		zlib_deflater(const zlib_deflater&) = delete;
		zlib_deflater& operator=(zlib_deflater&&) = delete;
		zlib_deflater& operator=(const zlib_deflater&) = delete;
		~zlib_deflater();

		[[nodiscard]] bool is(
			int level = Z_DEFAULT_COMPRESSION,
			int method = Z_DEFLATED,
			int windowBits = 15,
			int memLevel = 8,
			int strategy = Z_DEFAULT_STRATEGY,
			size_t defaultBufferSize = 16384) const {
			return m_level == level && m_method == method && m_windowBits == windowBits && m_memLevel == memLevel && m_strategy == strategy && m_defaultBufferSize == defaultBufferSize;
		}

		std::span<uint8_t> deflate(std::span<const uint8_t> source);

		std::span<uint8_t> operator()(std::span<const uint8_t> source);

		[[nodiscard]] const std::span<uint8_t>& result() const;

		[[nodiscard]] std::vector<uint8_t>& result();

		static thread_pool::object_pool<zlib_deflater>::scoped_pooled_object pooled();
	};
}

#endif
