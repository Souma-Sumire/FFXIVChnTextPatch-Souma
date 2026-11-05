#include "../include/xivres/packed_stream.placeholder.h"

xivres::placeholder_packed_stream::placeholder_packed_stream(xivres::path_spec pathSpec, std::shared_ptr<const stream> strm, uint32_t decompressedSize)
	: packed_stream(std::move(pathSpec))
	, m_stream(std::move(strm))
	, m_header(packed::file_header::new_empty(decompressedSize == UINT32_MAX ? static_cast<uint32_t>(m_stream->size()) : decompressedSize, static_cast<size_t>(m_stream->size()))) {
}

xivres::placeholder_packed_stream::placeholder_packed_stream(xivres::path_spec pathSpec)
	: packed_stream(std::move(pathSpec))
	, m_header(packed::file_header::new_empty()) {
}

std::streamsize xivres::placeholder_packed_stream::size() const {
	return m_header.HeaderSize + (m_stream ? align(m_stream->size()).Alloc : 0);
}

std::streamsize xivres::placeholder_packed_stream::read(std::streamoff offset, void* buf, std::streamsize length) const {
	if (!length)
		return 0;

	auto relativeOffset = offset;
	auto out = std::span(static_cast<char*>(buf), static_cast<size_t>(length));

	if (std::cmp_less(relativeOffset, sizeof m_header)) {
		const auto src = util::span_cast<uint8_t>(1, &m_header).subspan(static_cast<size_t>(relativeOffset));
		const auto available = (std::min)(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty()) return length;
	} else
		relativeOffset -= sizeof m_header;

	if (const auto afterHeaderPad = static_cast<std::streamoff>(m_header.HeaderSize - sizeof m_header);
		relativeOffset < afterHeaderPad) {
		const auto available = (std::min<size_t>)(out.size_bytes(), afterHeaderPad);
		std::fill_n(out.begin(), available, 0);
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty()) return length;
	} else
		relativeOffset -= afterHeaderPad;

	if (const auto dataSize = m_stream ? m_stream->size() : 0) {
		if (relativeOffset < dataSize) {
			const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(dataSize - relativeOffset));
			m_stream->read_fully(relativeOffset, out.data(),static_cast<std::streamsize>(available));
			out = out.subspan(available);
			relativeOffset = 0;

			if (out.empty()) return length;
		} else
			relativeOffset -= dataSize;
	}

	if (const auto pad = m_stream ? align(m_stream->size()).Pad : 0) {
		if (relativeOffset < pad) {
			const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(pad));
			std::fill_n(out.begin(), available, 0);
			out = out.subspan(available);
			// relativeOffset = 0;

			if (out.empty()) return length;
		} // else relativeOffset -= pad;
	}

	return static_cast<std::streamsize>(length - out.size_bytes());
}

xivres::packed::type xivres::placeholder_packed_stream::get_packed_type() const {
	return packed::type::placeholder;
}

const xivres::placeholder_packed_stream& xivres::placeholder_packed_stream::instance() {
	static const placeholder_packed_stream s_instance{xivres::path_spec()};
	return s_instance;
}
