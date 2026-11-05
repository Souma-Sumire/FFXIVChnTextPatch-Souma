#include "../include/xivres/unpacked_stream.model.h"
#include "../include/xivres/model.h"

xivres::model_unpacker::model_unpacker(const packed::file_header& header, std::shared_ptr<const packed_stream> strm)
	: base_unpacker(header, std::move(strm)) {
	const auto underlyingSize = m_stream->size();
	auto readOffset = static_cast<std::streamoff>(sizeof(packed::file_header));
	const auto locator = m_stream->read_fully<packed::model_block_locator>(readOffset);
	const auto blockCount = static_cast<size_t>(locator.FirstBlockIndices.Index[2]) + locator.BlockCount.Index[2];

	readOffset += sizeof locator;
	for (const auto blockSize : m_stream->read_vector<uint16_t>(readOffset, blockCount)) {
		m_blocks.emplace_back(block_info{
			.RequestOffsetPastHeader = 0,
			.BlockOffset = m_blocks.empty() ? *header.HeaderSize : m_blocks.back().BlockOffset + m_blocks.back().PaddedChunkSize,
			.PaddedChunkSize = blockSize,
			.DecompressedSize = 0,
			.GroupIndex = UINT16_MAX,
			.GroupBlockIndex = 0,
		});
	}

	m_header.Version = header.BlockCountOrVersion;
	m_header.VertexDeclarationCount = locator.VertexDeclarationCount;
	m_header.MaterialCount = locator.MaterialCount;
	m_header.LodCount = locator.LodCount;
	m_header.EnableIndexBufferStreaming = locator.EnableIndexBufferStreaming;
	m_header.EnableEdgeGeometry = locator.EnableEdgeGeometry;
	m_header.Padding = locator.Padding;

	if (m_blocks.empty())
		return;

	for (uint16_t i = 0; i < 11; ++i) {
		if (!locator.BlockCount.at(i))
			continue;

		const size_t blockIndex = *locator.FirstBlockIndices.at(i);
		auto& firstBlock = m_blocks[blockIndex];
		firstBlock.GroupIndex = i;
		firstBlock.GroupBlockIndex = 0;

		for (uint16_t j = 1, j_ = locator.BlockCount.at(i); j < j_; ++j) {
			if (blockIndex + j >= blockCount)
				throw bad_data_error("Out of bounds index information detected");

			auto& block = m_blocks[blockIndex + j];
			if (block.GroupIndex != UINT16_MAX)
				throw bad_data_error("Overlapping index information detected");
			block.GroupIndex = i;
			block.GroupBlockIndex = j;
		}
	}

	auto lastOffset = 0;
	for (auto& block : m_blocks) {
		packed::block_header blockHeader;

		if (std::cmp_equal(block.BlockOffset, underlyingSize))
			blockHeader.DecompressedSize = blockHeader.CompressedSize = 0;
		else
			m_stream->read_fully(block.BlockOffset, &blockHeader, sizeof blockHeader);

		block.DecompressedSize = static_cast<uint16_t>(blockHeader.DecompressedSize);
		block.RequestOffsetPastHeader = lastOffset;
		lastOffset += block.DecompressedSize;
	}

	for (size_t blkI = locator.FirstBlockIndices.Stack, i_ = blkI + locator.BlockCount.Stack; blkI < i_; ++blkI)
		m_header.StackSize += m_blocks[blkI].DecompressedSize;
	for (size_t blkI = locator.FirstBlockIndices.Runtime, i_ = blkI + locator.BlockCount.Runtime; blkI < i_; ++blkI)
		m_header.RuntimeSize += m_blocks[blkI].DecompressedSize;
	for (size_t lodI = 0; lodI < 3; ++lodI) {
		for (size_t blkI = locator.FirstBlockIndices.Vertex[lodI], i_ = blkI + locator.BlockCount.Vertex[lodI]; blkI < i_; ++blkI)
			m_header.VertexSize[lodI] += m_blocks[blkI].DecompressedSize;
		for (size_t blkI = locator.FirstBlockIndices.Index[lodI], i_ = blkI + locator.BlockCount.Index[lodI]; blkI < i_; ++blkI)
			m_header.IndexSize[lodI] += m_blocks[blkI].DecompressedSize;
		m_header.VertexOffset[lodI] = locator.BlockCount.Vertex[lodI] ? static_cast<uint32_t>(sizeof m_header + m_blocks[locator.FirstBlockIndices.Vertex[lodI]].RequestOffsetPastHeader) : 0;
		m_header.IndexOffset[lodI] = locator.BlockCount.Index[lodI] ? static_cast<uint32_t>(sizeof m_header + m_blocks[locator.FirstBlockIndices.Index[lodI]].RequestOffsetPastHeader) : 0;
	}
}

std::streamsize xivres::model_unpacker::read(std::streamoff offset, void* buf, std::streamsize length) {
	if (!length)
		return 0;

	block_decoder info(buf, length, offset);
	info.forward_copy(util::span_cast<uint8_t>(1, &m_header));
	if (info.complete() || m_blocks.empty())
		return info.filled();

	auto it = std::ranges::upper_bound(m_blocks, info.current_offset(), {}, &block_info::RequestOffsetPastHeader);
	if (it != m_blocks.begin())
		--it;

	const auto itEnd = std::ranges::upper_bound(it, m_blocks.end(), static_cast<uint32_t>(offset + length), {}, &block_info::RequestOffsetPastHeader);
	info.multithreaded(std::cmp_greater_equal(std::distance(it, itEnd), MinBlockCountForMultithreadedDecompression));

	const auto preloadFrom = static_cast<std::streamoff>(it->BlockOffset);
	const auto preloadTo = static_cast<std::streamoff>(itEnd == m_blocks.end() ? m_blocks.back().BlockOffset + m_blocks.back().PaddedChunkSize: itEnd->BlockOffset);

	auto pooledPreload = *m_preloads;
	if (!pooledPreload)
		pooledPreload.emplace();
	auto& preload = *pooledPreload;
	preload.resize(preloadTo - preloadFrom);
	util::thread_pool::pool::current().release_working_status([&] { m_stream->read_fully(preloadFrom, std::span(preload)); });

	for (; it != m_blocks.end(); ++it) {
		if (info.skip_to(it->RequestOffsetPastHeader + sizeof m_header))
			break;
		if (info.forward_sqblock(std::span(preload).subspan(it->BlockOffset - preloadFrom, it->PaddedChunkSize)))
			break;
	}
	
	info.skip_to(size());
	return info.filled();
}
