#include "../include/xivres/unpacked_stream.standard.h"

#include <vector>

xivres::standard_unpacker::standard_unpacker(const packed::file_header& header, std::shared_ptr<const packed_stream> strm)
	: base_unpacker(header, std::move(strm))
	, m_headerSize(header.HeaderSize) {

	const auto locators = m_stream->read_vector<packed::standard_block_locator>(sizeof(packed::file_header), header.BlockCountOrVersion);
	m_blocks.reserve(locators.size());
	auto requestOffset = 0U;
	for (const auto& locator : locators) {
		auto& block = m_blocks.emplace_back();
		block.RequestOffset = requestOffset;
		block.BlockSize = locator.BlockSize;
		block.BlockOffset = header.HeaderSize + locator.Offset;
		requestOffset += locator.DecompressedDataSize;
	}
}

std::streamsize xivres::standard_unpacker::read(std::streamoff offset, void* buf, std::streamsize length) {
	if (!length || m_blocks.empty())
		return 0;

	block_decoder info(buf, length, offset);

	auto it = std::ranges::upper_bound(m_blocks, static_cast<uint32_t>(offset), {}, &block_info::RequestOffset);
	if (it != m_blocks.begin())
		--it;

	const auto itEnd = std::ranges::upper_bound(it, m_blocks.end(), static_cast<uint32_t>(offset + length), {}, &block_info::RequestOffset);
	info.multithreaded(std::cmp_greater_equal(std::distance(it, itEnd), MinBlockCountForMultithreadedDecompression));

	const auto preloadFrom = static_cast<std::streamoff>(it->BlockOffset);
	const auto preloadTo = static_cast<std::streamoff>(itEnd == m_blocks.end() ? m_blocks.back().BlockOffset + m_blocks.back().BlockSize : itEnd->BlockOffset);

	auto pooledPreload = *m_preloads;
	if (!pooledPreload)
		pooledPreload.emplace();
	auto& preload = *pooledPreload;
	preload.resize(preloadTo - preloadFrom);
	util::thread_pool::pool::current().release_working_status([&] { m_stream->read_fully(preloadFrom, std::span(preload)); });

	for (; it < m_blocks.end(); ++it) {
		if (info.skip_to(it->RequestOffset))
			break;
		if (info.forward_sqblock(std::span(preload).subspan(it->BlockOffset - preloadFrom, it->BlockSize)))
			break;
	}
	
	info.skip_to(size());
	return info.filled();
}
