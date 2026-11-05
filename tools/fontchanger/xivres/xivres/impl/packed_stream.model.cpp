#include "../include/xivres/packed_stream.model.h"

#include <ranges>

#include "../include/xivres/model.h"
#include "../include/xivres/util.thread_pool.h"
#include "../include/xivres/util.zlib_wrapper.h"

xivres::model_passthrough_packer::model_passthrough_packer(std::shared_ptr<const stream> strm)
	: passthrough_packer(std::move(strm)) {}

std::streamsize xivres::model_passthrough_packer::size() {
	const auto blockCount = 11 + align<uint64_t>(m_stream->size(), packed::MaxBlockDataSize).Count;
	const auto headerSize = align(sizeof m_header + blockCount * sizeof m_blockDataSizes[0]).Alloc;
	return static_cast<std::streamsize>(headerSize + blockCount * packed::MaxBlockSize);
}

void xivres::model_passthrough_packer::ensure_initialized() {
	std::unique_lock lock(m_mtx, std::defer_lock);

	if (*m_header.Entry.DecompressedSize)
		return;

	{
		const auto _ = util::thread_pool::pool::instance().release_working_status();
		lock.lock();
	}
	if (*m_header.Entry.DecompressedSize)
		return;

	const auto unpackedHeader = m_stream->read_fully<model::header>(0);

	m_header.Entry.Type = packed::type::model;
	m_header.Entry.DecompressedSize = static_cast<uint32_t>(m_stream->size());
	m_header.Entry.BlockCountOrVersion = unpackedHeader.Version;

	m_header.Model.VertexDeclarationCount = unpackedHeader.VertexDeclarationCount;
	m_header.Model.MaterialCount = unpackedHeader.MaterialCount;
	m_header.Model.LodCount = unpackedHeader.LodCount;
	m_header.Model.EnableIndexBufferStreaming = unpackedHeader.EnableIndexBufferStreaming;
	m_header.Model.EnableEdgeGeometry = unpackedHeader.EnableEdgeGeometry;

	const auto getNextBlockOffset = [&] {
		return m_paddedBlockSizes.empty() ? 0U : m_blockOffsets.back() + m_paddedBlockSizes.back();
	};

	auto baseFileOffset = static_cast<uint32_t>(sizeof unpackedHeader);
	const auto generateSet = [&](const uint32_t size) {
		const auto alignedDecompressedSize = align(size).Alloc;
		const auto alignedBlock = align<uint32_t, uint16_t>(size, packed::MaxBlockDataSize);
		const auto firstBlockOffset = size ? getNextBlockOffset() : 0;
		const auto firstBlockIndex = static_cast<uint16_t>(m_blockOffsets.size());
		alignedBlock.iterate_chunks([&](auto, uint32_t offset, uint32_t blockSize) {
			m_blockOffsets.push_back(getNextBlockOffset());
			m_blockDataSizes.push_back(static_cast<uint16_t>(blockSize));
			m_paddedBlockSizes.push_back(static_cast<uint16_t>(align(sizeof(packed::block_header) + blockSize)));
			m_actualFileOffsets.push_back(offset);
		}, baseFileOffset);
		const auto chunkSize = size ? getNextBlockOffset() - firstBlockOffset : 0;
		baseFileOffset += size;

		return std::make_tuple(
			alignedDecompressedSize,
			alignedBlock.Count,
			firstBlockOffset,
			firstBlockIndex,
			chunkSize
		);
	};

	std::tie(m_header.Model.AlignedDecompressedSizes.Stack,
		m_header.Model.BlockCount.Stack,
		m_header.Model.FirstBlockOffsets.Stack,
		m_header.Model.FirstBlockIndices.Stack,
		m_header.Model.ChunkSizes.Stack) = generateSet(unpackedHeader.StackSize);

	std::tie(m_header.Model.AlignedDecompressedSizes.Runtime,
		m_header.Model.BlockCount.Runtime,
		m_header.Model.FirstBlockOffsets.Runtime,
		m_header.Model.FirstBlockIndices.Runtime,
		m_header.Model.ChunkSizes.Runtime) = generateSet(unpackedHeader.RuntimeSize);

	for (size_t i = 0; i < 3; i++) {
		std::tie(m_header.Model.AlignedDecompressedSizes.Vertex[i],
			m_header.Model.BlockCount.Vertex[i],
			m_header.Model.FirstBlockOffsets.Vertex[i],
			m_header.Model.FirstBlockIndices.Vertex[i],
			m_header.Model.ChunkSizes.Vertex[i]) = generateSet(unpackedHeader.VertexSize[i]);

		std::tie(m_header.Model.AlignedDecompressedSizes.EdgeGeometryVertex[i],
			m_header.Model.BlockCount.EdgeGeometryVertex[i],
			m_header.Model.FirstBlockOffsets.EdgeGeometryVertex[i],
			m_header.Model.FirstBlockIndices.EdgeGeometryVertex[i],
			m_header.Model.ChunkSizes.EdgeGeometryVertex[i]) = generateSet(unpackedHeader.IndexOffset[i] - unpackedHeader.VertexOffset[i] - unpackedHeader.VertexSize[i]);

		std::tie(m_header.Model.AlignedDecompressedSizes.Index[i],
			m_header.Model.BlockCount.Index[i],
			m_header.Model.FirstBlockOffsets.Index[i],
			m_header.Model.FirstBlockIndices.Index[i],
			m_header.Model.ChunkSizes.Index[i]) = generateSet(unpackedHeader.IndexSize[i]);
	}

	if (baseFileOffset > m_header.Entry.DecompressedSize)
		throw std::runtime_error("Bad model file (incomplete data)");

	m_header.Entry.HeaderSize = align(static_cast<uint32_t>(sizeof m_header + std::span(m_blockDataSizes).size_bytes()));
	m_header.Entry.set_space_units(static_cast<size_t>(size()));
}

std::streamsize xivres::model_passthrough_packer::translate_read(std::streamoff offset, void* buf, std::streamsize length) {
	if (!length)
		return 0;

	auto relativeOffset = static_cast<uint64_t>(offset);
	auto out = std::span(static_cast<char*>(buf), static_cast<size_t>(length));

	if (relativeOffset < sizeof m_header) {
		const auto src = util::span_cast<char>(1, &m_header).subspan(static_cast<size_t>(relativeOffset));
		const auto available = (std::min)(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty()) return length;
	} else
		relativeOffset -= sizeof m_header;

	if (const auto srcTyped = std::span(m_paddedBlockSizes);
		relativeOffset < srcTyped.size_bytes()) {
		const auto src = util::span_cast<char>(srcTyped).subspan(static_cast<size_t>(relativeOffset));
		const auto available = (std::min)(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty()) return length;
	} else
		relativeOffset -= srcTyped.size_bytes();

	if (const auto padBeforeBlocks = align(sizeof ModelEntryHeader + std::span(m_paddedBlockSizes).size_bytes()).Pad;
		relativeOffset < padBeforeBlocks) {
		const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(padBeforeBlocks - relativeOffset));
		std::fill_n(out.begin(), available, 0);
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty()) return length;
	} else
		relativeOffset -= padBeforeBlocks;

	auto it = std::ranges::lower_bound(m_blockOffsets,
		static_cast<uint32_t>(relativeOffset),
		[&](uint32_t l, uint32_t r) {
		return l < r;
	});

	if (it == m_blockOffsets.end())
		--it;
	while (*it > relativeOffset) {
		if (it == m_blockOffsets.begin()) {
			const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(*it - relativeOffset));
			std::fill_n(out.begin(), available, 0);
			out = out.subspan(available);
			relativeOffset = 0;

			if (out.empty()) return length;
			break;
		} else
			--it;
	}

	relativeOffset -= *it;

	for (auto i = it - m_blockOffsets.begin(); it != m_blockOffsets.end(); ++it, ++i) {
		if (relativeOffset < sizeof(packed::block_header)) {
			const auto header = packed::block_header{
				.HeaderSize = sizeof(packed::block_header),
				.Version = 0,
				.CompressedSize = packed::block_header::CompressedSizeNotCompressed,
				.DecompressedSize = m_blockDataSizes[i],
			};
			const auto src = util::span_cast<uint8_t>(1, &header).subspan(static_cast<size_t>(relativeOffset));
			const auto available = (std::min)(out.size_bytes(), src.size_bytes());
			std::copy_n(src.begin(), available, out.begin());
			out = out.subspan(available);
			relativeOffset = 0;

			if (out.empty()) return length;
		} else
			relativeOffset -= sizeof(packed::block_header);

		if (relativeOffset < m_blockDataSizes[i]) {
			const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(m_blockDataSizes[i] - relativeOffset));
			m_stream->read_fully(static_cast<std::streamoff>(m_actualFileOffsets[i] + relativeOffset), out.data(), static_cast<std::streamsize>(available));
			out = out.subspan(available);
			relativeOffset = 0;

			if (out.empty()) return length;
		} else
			relativeOffset -= m_blockDataSizes[i];

		if (const auto padSize = m_paddedBlockSizes[i] - m_blockDataSizes[i] - sizeof(packed::block_header);
			relativeOffset < padSize) {
			const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(padSize - relativeOffset));
			std::fill_n(out.begin(), available, 0);
			out = out.subspan(available);
			relativeOffset = 0;

			if (out.empty()) return length;
		} else
			relativeOffset -= padSize;
	}

	if (!out.empty()) {
		const auto actualDataSize = m_header.Entry.HeaderSize + (m_paddedBlockSizes.empty() ? 0 : m_blockOffsets.back() + m_paddedBlockSizes.back());
		const auto endPadSize = static_cast<uint64_t>(size() - actualDataSize);
		if (relativeOffset < endPadSize) {
			const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(endPadSize - relativeOffset));
			std::fill_n(out.begin(), available, 0);
			out = out.subspan(available);
		}
	}

	return static_cast<std::streamsize>(length - out.size_bytes());
}

std::unique_ptr<xivres::stream> xivres::model_compressing_packer::pack() {
	const auto unpackedHeader = unpacked().read_fully<model::header>(0);

	std::vector<std::vector<block_data>> blockDataList;
	blockDataList.reserve(11);

	if (!multithreaded()) {
		auto baseFileOffset = static_cast<uint32_t>(sizeof unpackedHeader);
		const auto generateSet = [&](const uint32_t setSize) {
			const auto alignedBlock = align<uint32_t, uint16_t>(setSize, packed::MaxBlockDataSize);

			auto& setBlockDataList = blockDataList.emplace_back();
			setBlockDataList.resize(alignedBlock.Count);

			alignedBlock.iterate_chunks_breakable([&](size_t blockIndex, uint32_t offset, uint32_t length) {
				compress_block(offset, length, setBlockDataList[blockIndex]);
				return !cancelled();
			}, baseFileOffset);

			baseFileOffset += setSize;
		};

		generateSet(unpackedHeader.StackSize);
		generateSet(unpackedHeader.RuntimeSize);

		for (size_t i = 0; i < 3; i++) {
			generateSet(unpackedHeader.VertexSize[i]);
			generateSet(unpackedHeader.IndexOffset[i] - unpackedHeader.VertexOffset[i] - unpackedHeader.VertexSize[i]);
			generateSet(unpackedHeader.IndexSize[i]);
		}

	} else {
		util::thread_pool::task_waiter waiter;
		preload();

		auto baseFileOffset = static_cast<uint32_t>(sizeof unpackedHeader);
		const auto generateSet = [&](const uint32_t setSize) {
			const auto alignedBlock = align<uint32_t, uint16_t>(setSize, packed::MaxBlockDataSize);

			auto& setBlockDataList = blockDataList.emplace_back();
			setBlockDataList.resize(alignedBlock.Count);

			alignedBlock.iterate_chunks_breakable([&](size_t blockIndex, uint32_t offset, uint32_t length) {
				waiter.submit([this, offset, length, &blockData = setBlockDataList[blockIndex]](auto& task) {
					if (task.cancelled())
						cancel();
					if (!cancelled())
						compress_block(offset, length, blockData);
				});

				return !cancelled();
			}, baseFileOffset);

			baseFileOffset += setSize;
		};

		generateSet(unpackedHeader.StackSize);
		generateSet(unpackedHeader.RuntimeSize);

		for (size_t i = 0; i < 3; i++) {
			generateSet(unpackedHeader.VertexSize[i]);
			generateSet(unpackedHeader.IndexOffset[i] - unpackedHeader.VertexOffset[i] - unpackedHeader.VertexSize[i]);
			generateSet(unpackedHeader.IndexSize[i]);
		}

		if (!cancelled())
			waiter.wait_all();
	}

	if (cancelled())
		return nullptr;

	size_t totalBlockCount = 0, entryBodyLength = 0;
	for (const auto& set : blockDataList) {
		for (const auto& blockData : set) {
			totalBlockCount++;
			entryBodyLength += align(sizeof(packed::block_header) + blockData.Data.size());
		}
	}
	const auto entryHeaderLength = static_cast<uint16_t>(align(0
		+ sizeof(packed::file_header)
		+ sizeof(packed::model_block_locator)
		+ sizeof(uint16_t) * totalBlockCount
	));

	std::vector<uint8_t> result(entryHeaderLength + entryBodyLength);

	auto& entryHeader = *reinterpret_cast<packed::file_header*>(result.data());
	entryHeader.Type = packed::type::model;
	entryHeader.DecompressedSize = static_cast<uint32_t>(unpacked().size());
	entryHeader.BlockCountOrVersion = static_cast<uint32_t>(unpackedHeader.Version);
	entryHeader.HeaderSize = entryHeaderLength;
	entryHeader.set_space_units(entryBodyLength);

	auto& modelHeader = *reinterpret_cast<packed::model_block_locator*>(&entryHeader + 1);
	modelHeader.VertexDeclarationCount = unpackedHeader.VertexDeclarationCount;
	modelHeader.MaterialCount = unpackedHeader.MaterialCount;
	modelHeader.LodCount = unpackedHeader.LodCount;
	modelHeader.EnableIndexBufferStreaming = unpackedHeader.EnableIndexBufferStreaming;
	modelHeader.EnableEdgeGeometry = unpackedHeader.EnableEdgeGeometry;

	const auto paddedBlockSizes = util::span_cast<uint16_t>(result, sizeof entryHeader + sizeof modelHeader, totalBlockCount);

	{
		uint16_t totalBlockIndex = 0;
		uint32_t nextBlockOffset = 0;

		auto resultDataPtr = result.begin() + entryHeaderLength;
		auto baseFileOffset = static_cast<uint32_t>(sizeof unpackedHeader);
		const auto generateSet = [&](size_t setIndex, const uint32_t size) {
			const auto alignedDecompressedSize = align(size).Alloc;
			const auto alignedBlock = align<uint32_t, uint16_t>(size, packed::MaxBlockDataSize);
			const auto firstBlockOffset = size ? nextBlockOffset : 0;
			const auto firstBlockIndex = totalBlockIndex;

			alignedBlock.iterate_chunks([&](size_t blockIndex, uint32_t /*offset*/, uint32_t length) {
				if (cancelled())
					return false;
				auto& blockData = blockDataList[setIndex][blockIndex];

				auto& header = *reinterpret_cast<packed::block_header*>(&*resultDataPtr);
				header.HeaderSize = sizeof(packed::block_header);
				header.Version = 0;
				header.CompressedSize = blockData.Deflated ? static_cast<uint32_t>(blockData.Data.size()) : packed::block_header::CompressedSizeNotCompressed;
				header.DecompressedSize = length;

				std::ranges::copy(blockData.Data, resultDataPtr + sizeof header);

				const auto alloc = static_cast<uint16_t>(align(sizeof header + blockData.Data.size()));

				paddedBlockSizes[totalBlockIndex++] = alloc;
				nextBlockOffset += alloc;

				resultDataPtr += alloc;

				std::vector<uint8_t>().swap(blockData.Data);
				return true;
			}, baseFileOffset);

			const auto chunkSize = size ? nextBlockOffset - firstBlockOffset : 0;
			baseFileOffset += size;

			return std::make_tuple(
				alignedDecompressedSize,
				alignedBlock.Count,
				firstBlockOffset,
				firstBlockIndex,
				chunkSize
			);
		};

		size_t setIndexCounter = 0;
		std::tie(modelHeader.AlignedDecompressedSizes.Stack,
			modelHeader.BlockCount.Stack,
			modelHeader.FirstBlockOffsets.Stack,
			modelHeader.FirstBlockIndices.Stack,
			modelHeader.ChunkSizes.Stack) = generateSet(setIndexCounter++, unpackedHeader.StackSize);

		std::tie(modelHeader.AlignedDecompressedSizes.Runtime,
			modelHeader.BlockCount.Runtime,
			modelHeader.FirstBlockOffsets.Runtime,
			modelHeader.FirstBlockIndices.Runtime,
			modelHeader.ChunkSizes.Runtime) = generateSet(setIndexCounter++, unpackedHeader.RuntimeSize);

		for (size_t i = 0; i < 3; i++) {
			if (cancelled())
				return {};

			std::tie(modelHeader.AlignedDecompressedSizes.Vertex[i],
				modelHeader.BlockCount.Vertex[i],
				modelHeader.FirstBlockOffsets.Vertex[i],
				modelHeader.FirstBlockIndices.Vertex[i],
				modelHeader.ChunkSizes.Vertex[i]) = generateSet(setIndexCounter++, unpackedHeader.VertexSize[i]);

			std::tie(modelHeader.AlignedDecompressedSizes.EdgeGeometryVertex[i],
				modelHeader.BlockCount.EdgeGeometryVertex[i],
				modelHeader.FirstBlockOffsets.EdgeGeometryVertex[i],
				modelHeader.FirstBlockIndices.EdgeGeometryVertex[i],
				modelHeader.ChunkSizes.EdgeGeometryVertex[i]) = generateSet(setIndexCounter++, unpackedHeader.IndexOffset[i] - unpackedHeader.VertexOffset[i] - unpackedHeader.VertexSize[i]);

			std::tie(modelHeader.AlignedDecompressedSizes.Index[i],
				modelHeader.BlockCount.Index[i],
				modelHeader.FirstBlockOffsets.Index[i],
				modelHeader.FirstBlockIndices.Index[i],
				modelHeader.ChunkSizes.Index[i]) = generateSet(setIndexCounter++, unpackedHeader.IndexSize[i]);
		}
	}

	return std::make_unique<memory_stream>(std::move(result));
}
