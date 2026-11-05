#include "../include/xivres/util.zlib_wrapper.h"

xivres::util::zlib_error::zlib_error(int returnCode)
	: std::runtime_error(zlib_error_to_string(returnCode)) {
}

std::string xivres::util::zlib_error::zlib_error_to_string(int code) {
	switch (code) {
		case Z_OK: return "OK";
		case Z_STREAM_END: return "Stream end";
		case Z_NEED_DICT: return "Need dict";
		case Z_ERRNO: return std::generic_category().message(code);
		case Z_STREAM_ERROR: return "Stream error";
		case Z_DATA_ERROR: return "Data error";
		case Z_MEM_ERROR: return "Memory error";
		case Z_BUF_ERROR: return "Buffer error";
		case Z_VERSION_ERROR: return "Version error";
		default: return std::format("Unknown return code {}", code);
	}
}

std::span<uint8_t> xivres::util::zlib_inflater::operator()(std::span<const uint8_t> source, std::span<uint8_t> target) {
	initialize_inflation();

	m_zstream.next_in = const_cast<Bytef*>(source.data());
	m_zstream.avail_in = static_cast<uint32_t>(source.size());
	m_zstream.next_out = target.data();
	m_zstream.avail_out = static_cast<uint32_t>(target.size());

	if (const auto res = inflate(&m_zstream, Z_FINISH);
		res != Z_OK && res != Z_BUF_ERROR && res != Z_STREAM_END) {
		throw zlib_error(res);
	}

	return target.subspan(0, target.size() - m_zstream.avail_out);
}

std::span<uint8_t> xivres::util::zlib_inflater::operator()(std::span<const uint8_t> source, size_t maxSize) {
	if (m_buffer.size() < maxSize)
		m_buffer.resize(maxSize);

	return operator()(source, std::span(m_buffer));
}

std::span<uint8_t> xivres::util::zlib_inflater::operator()(std::span<const uint8_t> source) {
	initialize_inflation();

	m_zstream.next_in = const_cast<Bytef*>(source.data());
	m_zstream.avail_in = static_cast<uint32_t>(source.size());

	if (m_buffer.size() < m_defaultBufferSize)
		m_buffer.resize(m_defaultBufferSize);
	while (true) {
		m_zstream.next_out = &m_buffer[m_zstream.total_out];
		m_zstream.avail_out = static_cast<uint32_t>(m_buffer.size() - m_zstream.total_out);

		if (const auto res = inflate(&m_zstream, Z_FINISH);
			res != Z_OK && res != Z_BUF_ERROR && res != Z_STREAM_END) {
			throw zlib_error(res);
		} else {
			if (res == Z_STREAM_END)
				break;
			m_buffer.resize(m_buffer.size() + std::min<size_t>(m_buffer.size(), 65536));
		}
	}

	return std::span(m_buffer).subspan(0, m_zstream.total_out);
}

xivres::util::zlib_inflater::~zlib_inflater() {
	if (m_initialized)
		inflateEnd(&m_zstream);
}

xivres::util::zlib_inflater::zlib_inflater(int windowBits, int defaultBufferSize)
	: m_windowBits(windowBits)
	, m_defaultBufferSize(defaultBufferSize) {

}

void xivres::util::zlib_inflater::initialize_inflation() {
	int res;
	if (!m_initialized) {
		res = inflateInit2(&m_zstream, m_windowBits);
		m_initialized = true;
	} else
		res = inflateReset2(&m_zstream, m_windowBits);
	if (res != Z_OK)
		throw zlib_error(res);
}

xivres::util::thread_pool::object_pool<xivres::util::zlib_inflater>::scoped_pooled_object xivres::util::zlib_inflater::pooled() {
	static thread_pool::object_pool<zlib_inflater> s_pool;
	return *s_pool;
}

std::vector<uint8_t>& xivres::util::zlib_deflater::result() {
	m_buffer.resize(m_latestResult.size());
	return m_buffer;
}

const std::span<uint8_t>& xivres::util::zlib_deflater::result() const {
	return m_latestResult;
}

std::span<uint8_t> xivres::util::zlib_deflater::operator()(std::span<const uint8_t> source) {
	return deflate(source);
}

std::span<uint8_t> xivres::util::zlib_deflater::deflate(std::span<const uint8_t> source) {
	initialize_deflation();

	m_zstream.next_in = const_cast<Bytef*>(source.data());
	m_zstream.avail_in = static_cast<uint32_t>(source.size());

	if (m_buffer.size() < m_defaultBufferSize)
		m_buffer.resize(m_defaultBufferSize);
	while (true) {
		m_zstream.next_out = &m_buffer[m_zstream.total_out];
		m_zstream.avail_out = static_cast<uint32_t>(m_buffer.size() - m_zstream.total_out);

		if (const auto res = ::deflate(&m_zstream, Z_FINISH);
			res != Z_OK && res != Z_BUF_ERROR && res != Z_STREAM_END)
			throw zlib_error(res);
		else {
			if (res == Z_STREAM_END)
				break;
			m_buffer.resize(m_buffer.size() + std::min<size_t>(m_buffer.size(), 65536));
		}
	}

	return m_latestResult = std::span(m_buffer).subspan(0, m_zstream.total_out);
}

xivres::util::zlib_deflater::~zlib_deflater() {
	if (m_initialized)
		deflateEnd(&m_zstream);
}

xivres::util::zlib_deflater::zlib_deflater(int level, int method, int windowBits, int memLevel, int strategy, size_t defaultBufferSize)
	: m_level(level)
	, m_method(method)
	, m_windowBits(windowBits)
	, m_memLevel(memLevel)
	, m_strategy(strategy)
	, m_defaultBufferSize(defaultBufferSize) {}

void xivres::util::zlib_deflater::initialize_deflation() {
	int res;
	if (!m_initialized) {
		res = deflateInit2(&m_zstream, m_level, m_method, m_windowBits, m_memLevel, m_strategy);
		m_initialized = true;
	} else
		res = deflateReset(&m_zstream);
	if (res != Z_OK)
		throw zlib_error(res);
}

xivres::util::thread_pool::object_pool<xivres::util::zlib_deflater>::scoped_pooled_object xivres::util::zlib_deflater::pooled() {
	static thread_pool::object_pool<zlib_deflater> s_pool;
	return *s_pool;
}
