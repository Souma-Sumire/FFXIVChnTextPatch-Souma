#include "../include/xivres/packed_stream.h"
#include "../include/xivres/unpacked_stream.h"

xivres::unpacked_stream xivres::packed_stream::get_unpacked(std::span<uint8_t> obfuscatedHeaderRewrite) const {
	return {std::static_pointer_cast<const packed_stream>(shared_from_this()), obfuscatedHeaderRewrite};
}

std::unique_ptr<xivres::unpacked_stream> xivres::packed_stream::make_unpacked_ptr(std::span<uint8_t> obfuscatedHeaderRewrite) const {
	return std::make_unique<unpacked_stream>(std::static_pointer_cast<const packed_stream>(shared_from_this()), obfuscatedHeaderRewrite);
}

void xivres::compressing_packer::preload() {
	m_preloadedStream.emplace(m_stream);
}

void xivres::compressing_packer::compress_block(uint32_t offset, uint32_t length, block_data& blockData) const {
	if (cancelled())
		return;

	auto pooledBuffer = util::thread_pool::pooled_byte_buffer();
	if (!pooledBuffer)
		pooledBuffer.emplace();
	auto& buffer = *pooledBuffer;

	buffer.clear();
	buffer.resize(length);
	if (const auto read = static_cast<size_t>(unpacked().read(offset, buffer.data(), length)); read != length)
		std::fill_n(&buffer[read], length - read, 0);

	blockData.DecompressedSize = length;
	if (compression_level()) {
		auto deflater = util::zlib_deflater::pooled();
		if (!deflater || !deflater->is(compression_level(), Z_DEFLATED, -15))
			deflater.emplace(compression_level(), Z_DEFLATED, -15);
		if (deflater->deflate(std::span(buffer)).size() < buffer.size()) {
			blockData.Data = std::move(deflater->result());
			blockData.Deflated = true;
			return;
		}
	}
	blockData.Data = std::move(buffer);
}
