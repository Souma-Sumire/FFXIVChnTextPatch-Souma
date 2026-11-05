#include "../include/xivres/unpacked_stream.placeholder.h"

xivres::placeholder_unpacker::placeholder_unpacker(const packed::file_header& header, std::shared_ptr<const packed_stream> strm, std::span<uint8_t> headerRewrite)
	: base_unpacker(header, std::move(strm)) {
	if (header.DecompressedSize < header.BlockCountOrVersion) {
		auto src = m_stream->read_vector<uint8_t>(header.HeaderSize, header.DecompressedSize);
		if (!headerRewrite.empty())
			std::copy_n(headerRewrite.begin(), (std::min)(headerRewrite.size(), src.size()), src.begin());
		m_inflater.emplace(-MAX_WBITS, header.DecompressedSize);
		m_provider.emplace(m_stream->path_spec(), std::make_shared<memory_stream>((*m_inflater)(src)));

	} else {
		m_partialView = std::make_shared<partial_view_stream>(m_stream, header.HeaderSize);
		m_provider.emplace(m_stream->path_spec(), m_partialView);
	}
}

std::streamsize xivres::placeholder_unpacker::read(std::streamoff offset, void* buf, std::streamsize length) {
	return m_provider->read(offset, buf, length);
}
