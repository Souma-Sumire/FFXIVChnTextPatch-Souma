#include "../include/xivres/unpacked_stream.texture.h"

xivres::texture_unpacker::texture_unpacker(const packed::file_header& header, std::shared_ptr<const packed_stream> strm)
	: base_unpacker(header, std::move(strm)) {
	auto readOffset = static_cast<std::streamoff>(sizeof(packed::file_header));
	const auto locators = m_stream->read_vector<packed::mipmap_block_locator>(readOffset, header.BlockCountOrVersion);
	readOffset += static_cast<std::streamoff>(std::span(locators).size_bytes());

	m_head = m_stream->read_vector<uint8_t>(header.HeaderSize, locators[0].CompressedOffset);

	m_blocks.reserve(locators.size());
	for (uint32_t i = 0; i < locators.size(); ++i) {
		const auto& locator = locators[i];

		const auto baseRequestOffset = m_blocks.empty() ? static_cast<uint32_t>(m_head.size()) : m_blocks.back().request_offset_end();
		const auto blockSizes = m_stream->read_vector<uint16_t>(readOffset, locator.BlockCount);
		
		auto& block = m_blocks.emplace_back();
		block.DecompressedSize = locator.DecompressedSize;
		block.Subblocks.reserve(blockSizes.size());
		for (const auto& blockSize : blockSizes) {
			auto& subblock = block.Subblocks.emplace_back();
			subblock.BlockSize = blockSize;
			subblock.RequestOffset = subblock.BlockOffset = (std::numeric_limits<uint32_t>::max)();
		}
		block.Subblocks.front().RequestOffset = baseRequestOffset;
		block.Subblocks.front().BlockOffset = header.HeaderSize + locator.CompressedOffset;
		readOffset += static_cast<std::streamoff>(std::span(blockSizes).size_bytes());
	}
}

std::streamsize xivres::texture_unpacker::read(std::streamoff offset, void* buf, std::streamsize length) {
	if (!length)
		return 0;

	block_decoder info(buf, length, offset);
	if (info.forward_copy(m_head))
		return info.filled();

	if (info.current_offset() >= size())
		return info.filled();

	auto it = std::ranges::upper_bound(m_blocks, info.current_offset(), {}, &block_info::request_offset_begin);
	if (it != m_blocks.begin())
		--it;

	bool multithreaded = false;
	const auto itEnd = std::ranges::upper_bound(it, m_blocks.end(), static_cast<uint32_t>(offset + length), {}, &block_info::request_offset_begin);
	info.multithreaded(multithreaded = multithreaded || std::cmp_greater_equal(std::distance(it, itEnd), MinBlockCountForMultithreadedDecompression));

	const auto preloadFrom = static_cast<std::streamoff>(it->Subblocks.front().BlockOffset);
	const auto preloadTo = static_cast<std::streamoff>(itEnd == m_blocks.end() ? m_packedSize : itEnd->Subblocks.front().BlockOffset);
	
	auto pooledPreload = *m_preloads;
	if (!pooledPreload)
		pooledPreload.emplace();
	auto& preload = *pooledPreload;
	preload.resize(preloadTo - preloadFrom);
	util::thread_pool::pool::current().release_working_status([&] { m_stream->read_fully(preloadFrom, std::span(preload)); });

	for (; it != m_blocks.end() && !info.complete(); ++it) {
		auto it2 = std::ranges::upper_bound(it->Subblocks, info.current_offset(), {}, &subblock_info::RequestOffset);
		if (it2 != it->Subblocks.begin())
			--it2;

		multithreaded = multithreaded || (!it->Subblocks.back() && it->Subblocks.size() >= MinBlockCountForMultithreadedDecompression);
		info.multithreaded(multithreaded);

		while (it2 != it->Subblocks.end() && !info.complete()) {
			const auto blockSpan = std::span(preload).subspan(it2->BlockOffset - preloadFrom, it2->BlockSize);
			const auto& blockHeader = *reinterpret_cast<const packed::block_header*>(blockSpan.data());
			it2->DecompressedSize = static_cast<uint16_t>(blockHeader.DecompressedSize);
			if (info.skip_to(it2->RequestOffset))
				break;
			if (info.forward_sqblock(blockSpan))
				break;

			const auto prev = it2++;
			if (it2 == it->Subblocks.end())
				break;
			it2->RequestOffset = prev->RequestOffset + prev->DecompressedSize;
			it2->BlockOffset = prev->BlockOffset + prev->BlockSize;
		}
	}

	info.skip_to(size());
	return info.filled();
}
