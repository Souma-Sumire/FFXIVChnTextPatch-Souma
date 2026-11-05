#include "../include/xivres/packed_stream.texture.h"

#include <ranges>

#include "../include/xivres/texture.h"
#include "../include/xivres/util.thread_pool.h"
#include "../include/xivres/util.zlib_wrapper.h"

std::streamsize xivres::texture_passthrough_packer::size() {
	ensure_initialized();
	const auto blockCount = MaxMipmapCountPerTexture + align<uint64_t>(m_stream->size(), packed::MaxBlockDataSize).Count;

	std::streamsize size = 0;

	// packed::file_header packedFileHeader;
	size += sizeof(packed::file_header);

	// packed::mipmap_block_locator lodBlocks[mipmapCount];
	size += MaxMipmapCountPerTexture * sizeof(packed::mipmap_block_locator);

	// uint16_t subBlockSizes[blockCount];
	size += static_cast<std::streamsize>(blockCount * sizeof(uint16_t));

	// Align block
	size = align(size);

	// texture::header textureHeader;
	size += sizeof(texture::header);

	// Mipmap offsets
	size += static_cast<std::streamsize>(blockCount * sizeof(uint16_t));

	// Just to be safe, align block
	size = align(size);

	// PackedBlock blocksOfMaximumSize[blockCount];
	size += static_cast<std::streamsize>(blockCount * packed::MaxBlockSize);

	return size;
}

void xivres::texture_passthrough_packer::ensure_initialized() {
	if (!m_mergedHeader.empty())
		return;

	const auto lock = std::scoped_lock(m_mtx);
	if (!m_mergedHeader.empty())
		return;

	std::vector<uint16_t> subBlockSizes;
	std::vector<uint8_t> textureHeaderAndMipmapOffsets;

	auto entryHeader = packed::file_header{
		.HeaderSize = sizeof(packed::file_header),
		.Type = packed::type::texture,
		.DecompressedSize = static_cast<uint32_t>(m_stream->size()),
		.AllocatedSpaceUnitCount = {},
		.OccupiedSpaceUnitCount = {},
		.BlockCountOrVersion = {},
	};

	textureHeaderAndMipmapOffsets.resize(sizeof(texture::header));
	m_stream->read_fully(0, std::span(textureHeaderAndMipmapOffsets));

	const auto mipmapCount = *reinterpret_cast<const texture::header*>(textureHeaderAndMipmapOffsets.data())->MipmapCount;
	textureHeaderAndMipmapOffsets.resize(sizeof(texture::header) + mipmapCount * sizeof uint32_t);
	m_stream->read_fully(sizeof(texture::header), util::span_cast<uint32_t>(textureHeaderAndMipmapOffsets, sizeof(texture::header), mipmapCount));

	const auto firstBlockOffset = *reinterpret_cast<const uint32_t*>(&textureHeaderAndMipmapOffsets[sizeof(texture::header)]);
	textureHeaderAndMipmapOffsets.resize(firstBlockOffset);
	const auto mipmapOffsetsSpan = util::span_cast<uint32_t>(textureHeaderAndMipmapOffsets, sizeof(texture::header), mipmapCount);
	m_mipmapOffsets.insert(m_mipmapOffsets.begin(), mipmapOffsetsSpan.begin(), mipmapOffsetsSpan.end());
	m_stream->read_fully(static_cast<std::streamoff>(sizeof(texture::header) + mipmapOffsetsSpan.size_bytes()), std::span(textureHeaderAndMipmapOffsets).subspan(sizeof(texture::header) + mipmapOffsetsSpan.size_bytes()));
	const auto& texHeader = *reinterpret_cast<const texture::header*>(textureHeaderAndMipmapOffsets.data());

	m_mipmapSizes.resize(m_mipmapOffsets.size());
	for (size_t i = 0; i < m_mipmapOffsets.size(); ++i)
		m_mipmapSizes[i] = static_cast<uint32_t>(calc_raw_data_length(texHeader, i));

	// Actual data exists but the mipmap offset array after texture header does not bother to refer
	// to the ones after the first set of mipmaps?
	// For example: if there are mipmaps of 4x4, 2x2, 1x1, 4x4, 2x2, 1x2, 4x4, 2x2, and 1x1,
	// then it will record mipmap offsets only up to the first occurrence of 1x1.
	m_repeatCount = ((m_mipmapOffsets.size() < 2 ? static_cast<size_t>(entryHeader.DecompressedSize) : m_mipmapOffsets[1]) - m_mipmapOffsets[0]) / calc_raw_data_length(texHeader, 0);

	auto blockOffsetCounter = firstBlockOffset;
	for (size_t mipmapIndex = 0; mipmapIndex < mipmapCount; ++mipmapIndex) {
		const auto mipmapSize = m_mipmapSizes[mipmapIndex];
		const auto mipmapOffset = m_mipmapOffsets[mipmapIndex];

		for (uint32_t repeatIndex = 0; repeatIndex < m_repeatCount; repeatIndex++) {
			const auto repeatedUnitOffset = mipmapOffset + mipmapSize * repeatIndex;

			const auto blockAlignment = align<uint32_t>(mipmapSize, packed::MaxBlockDataSize);
			packed::mipmap_block_locator loc;
			loc.CompressedOffset = blockOffsetCounter;
			loc.CompressedSize = 0;
			loc.DecompressedSize = mipmapSize;
			loc.FirstBlockIndex = m_blockLocators.empty() ? 0 : m_blockLocators.back().FirstBlockIndex + m_blockLocators.back().BlockCount;
			loc.BlockCount = blockAlignment.Count;

			blockAlignment.iterate_chunks([&](uint32_t, uint32_t, uint32_t length) {
				const auto alignmentInfo = align(sizeof(packed::block_header) + length);

				m_size += alignmentInfo.Alloc;
				subBlockSizes.push_back(static_cast<uint16_t>(alignmentInfo.Alloc));
				blockOffsetCounter += subBlockSizes.back();
				loc.CompressedSize += subBlockSizes.back();

			}, repeatedUnitOffset);

			m_blockLocators.emplace_back(loc);
		}
	}

	entryHeader.BlockCountOrVersion = static_cast<uint32_t>(m_blockLocators.size());
	entryHeader.HeaderSize = static_cast<uint32_t>(align(
		sizeof entryHeader +
		std::span(m_blockLocators).size_bytes() +
		std::span(subBlockSizes).size_bytes()));
	entryHeader.set_space_units(textureHeaderAndMipmapOffsets.size() + m_size);

	m_mergedHeader.reserve(entryHeader.HeaderSize + m_blockLocators.front().CompressedOffset);
	m_mergedHeader.insert(m_mergedHeader.end(),
		reinterpret_cast<char*>(&entryHeader),
		reinterpret_cast<char*>(&entryHeader + 1));
	m_mergedHeader.insert(m_mergedHeader.end(),
		reinterpret_cast<char*>(&m_blockLocators.front()),
		reinterpret_cast<char*>(&m_blockLocators.back() + 1));
	m_mergedHeader.insert(m_mergedHeader.end(),
		reinterpret_cast<char*>(&subBlockSizes.front()),
		reinterpret_cast<char*>(&subBlockSizes.back() + 1));
	m_mergedHeader.resize(entryHeader.HeaderSize);
	m_mergedHeader.insert(m_mergedHeader.end(),
		textureHeaderAndMipmapOffsets.begin(),
		textureHeaderAndMipmapOffsets.end());
	m_mergedHeader.resize(entryHeader.HeaderSize + m_blockLocators.front().CompressedOffset);

	m_size += m_mergedHeader.size();
}

std::streamsize xivres::texture_passthrough_packer::translate_read(std::streamoff offset, void* buf, std::streamsize length) {
	if (!length)
		return 0;

	auto relativeOffset = static_cast<uint64_t>(offset);
	auto out = std::span(static_cast<char*>(buf), static_cast<size_t>(length));

	// 1. Read headers and locators
	if (relativeOffset < m_mergedHeader.size()) {
		const auto src = std::span(m_mergedHeader)
			.subspan(static_cast<size_t>(relativeOffset));
		const auto available = (std::min)(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty()) return length;
	} else
		relativeOffset -= m_mergedHeader.size();

	// 2. Read data blocks
	if (relativeOffset < m_size - m_mergedHeader.size()) {

		// 1. Find the first LOD block
		relativeOffset += m_blockLocators[0].CompressedOffset;
		auto it = std::ranges::lower_bound(m_blockLocators,
			packed::mipmap_block_locator{
				.CompressedOffset = static_cast<uint32_t>(relativeOffset),
				.CompressedSize = {},
				.DecompressedSize = {},
				.FirstBlockIndex = {},
				.BlockCount = {},
			},
			[&](const auto& l, const auto& r) { return l.CompressedOffset < r.CompressedOffset; });
		if (it == m_blockLocators.end() || relativeOffset < it->CompressedOffset)
			--it;
		relativeOffset -= it->CompressedOffset;

		// 2. Iterate through LOD block headers
		for (; it != m_blockLocators.end(); ++it) {
			const auto blockIndex = it - m_blockLocators.begin();
			const auto mipmapIndex = blockIndex / m_repeatCount;
			const auto repeatIndex = blockIndex % m_repeatCount;
			const auto mipmapSize = m_mipmapSizes[mipmapIndex];
			const auto mipmapOffset = m_mipmapOffsets[mipmapIndex];
			const auto repeatUnitOffset = mipmapOffset + mipmapSize * repeatIndex;

			auto j = relativeOffset / packed::MaxBlockSize;
			auto dataOffset = repeatUnitOffset + j * packed::MaxBlockDataSize;
			relativeOffset -= j * packed::MaxBlockSize;

			// Iterate through packed blocks belonging to current LOD block
			for (; j < it->BlockCount; ++j, dataOffset += packed::MaxBlockDataSize) {
				const auto decompressedSize = j == it->BlockCount - 1 ? mipmapSize % packed::MaxBlockDataSize : packed::MaxBlockDataSize;
				const auto pad = align(sizeof(packed::block_header) + decompressedSize).Pad;

				// 1. Read packed block header
				if (relativeOffset < sizeof(packed::block_header)) {
					const auto header = packed::block_header{
						.HeaderSize = sizeof(packed::block_header),
						.Version = 0,
						.CompressedSize = packed::block_header::CompressedSizeNotCompressed,
						.DecompressedSize = decompressedSize,
					};
					const auto src = util::span_cast<uint8_t>(1, &header).subspan(static_cast<size_t>(relativeOffset));
					const auto available = (std::min)(out.size_bytes(), src.size_bytes());
					std::copy_n(src.begin(), available, out.begin());
					out = out.subspan(available);
					relativeOffset = 0;

					if (out.empty()) return length;
				} else
					relativeOffset -= sizeof(packed::block_header);

				// 2. Read packed block data
				if (relativeOffset < decompressedSize) {
					const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(decompressedSize - relativeOffset));
					const auto actuallyRead = static_cast<size_t>(m_stream->read(static_cast<std::streamoff>(dataOffset + relativeOffset), out.data(), static_cast<std::streamsize>(available)));
					if (actuallyRead < available)
						std::ranges::fill(out.subspan(actuallyRead, available - actuallyRead), 0);
					out = out.subspan(available);
					relativeOffset = 0;

					if (out.empty()) return length;
				} else
					relativeOffset -= decompressedSize;

				// 3. Fill padding with zero
				if (relativeOffset < pad) {
					const auto available = (std::min)(out.size_bytes(), pad);
					std::fill_n(out.data(), available, 0);
					out = out.subspan(available);
					relativeOffset = 0;

					if (out.empty()) return length;
				} else {
					relativeOffset -= pad;
				}
			}
		}
	} else
		relativeOffset -= m_size - m_mergedHeader.size();

	// 3. Fill remainder with zero
	if (const auto endPadSize = static_cast<uint64_t>(size() - m_size); relativeOffset < endPadSize) {
		const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(endPadSize - relativeOffset));
		std::fill_n(out.begin(), available, 0);
		out = out.subspan(available);
	}

	return static_cast<std::streamsize>(length - out.size_bytes());
}

std::unique_ptr<xivres::stream> xivres::texture_compressing_packer::pack() {
	std::vector<uint8_t> textureHeaderAndMipmapOffsets;

	const auto rawStreamSize = static_cast<uint32_t>(unpacked().size());

	textureHeaderAndMipmapOffsets.resize(sizeof(texture::header));
	unpacked().read_fully(0, std::span(textureHeaderAndMipmapOffsets));

	const auto mipmapCount = *reinterpret_cast<const texture::header*>(textureHeaderAndMipmapOffsets.data())->MipmapCount;
	textureHeaderAndMipmapOffsets.resize(sizeof(texture::header) + mipmapCount * sizeof uint32_t);
	unpacked().read_fully(sizeof(texture::header), util::span_cast<uint32_t>(textureHeaderAndMipmapOffsets, sizeof(texture::header), mipmapCount));

	const auto firstBlockOffset = *reinterpret_cast<const uint32_t*>(&textureHeaderAndMipmapOffsets[sizeof(texture::header)]);
	textureHeaderAndMipmapOffsets.resize(firstBlockOffset);
	const auto mipmapOffsets = util::span_cast<uint32_t>(textureHeaderAndMipmapOffsets, sizeof(texture::header), mipmapCount);
	unpacked().read_fully(static_cast<std::streamoff>(sizeof(texture::header) + mipmapOffsets.size_bytes()), std::span(textureHeaderAndMipmapOffsets).subspan(sizeof(texture::header) + mipmapOffsets.size_bytes()));
	const auto& texHeader = *reinterpret_cast<const texture::header*>(textureHeaderAndMipmapOffsets.data());

	if (cancelled())
		return nullptr;

	std::vector<uint32_t> mipmapSizes(mipmapOffsets.size());
	for (size_t i = 0; i < mipmapOffsets.size(); ++i)
		mipmapSizes[i] = static_cast<uint32_t>(calc_raw_data_length(texHeader, i));

	// Actual data exists but the mipmap offset array after texture header does not bother to refer
	// to the ones after the first set of mipmaps?
	// For example: if there are mipmaps of 4x4, 2x2, 1x1, 4x4, 2x2, 1x2, 4x4, 2x2, and 1x1,
	// then it will record mipmap offsets only up to the first occurrence of 1x1.
	const auto repeatCount = ((mipmapOffsets.size() < 2 ? static_cast<size_t>(rawStreamSize) : mipmapOffsets[1]) - mipmapOffsets[0]) / texture::calc_raw_data_length(texHeader, 0);

	std::vector<std::vector<std::vector<block_data>>> blockDataList(mipmapSizes.size());

	if (!multithreaded()) {
		for (size_t mipmapIndex = 0; mipmapIndex < mipmapOffsets.size(); ++mipmapIndex) {
			if (cancelled())
				return nullptr;

			const auto mipmapSize = mipmapSizes[mipmapIndex];
			const auto mipmapOffset = mipmapOffsets[mipmapIndex];
			blockDataList[mipmapIndex].resize(repeatCount);

			for (uint32_t repeatIndex = 0; repeatIndex < repeatCount; repeatIndex++) {
				if (cancelled())
					return nullptr;

				const auto repeatedUnitOffset = mipmapOffset + mipmapSize * repeatIndex;

				const auto blockAlignment = align<uint32_t>(mipmapSize, packed::MaxBlockDataSize);
				auto& blockDataVector = blockDataList[mipmapIndex][repeatIndex];
				blockDataVector.resize(blockAlignment.Count);

				blockAlignment.iterate_chunks_breakable([&](const uint32_t index, const uint32_t offset, const uint32_t length) {
					compress_block(offset, length, blockDataVector[index]);
					return !cancelled();
				}, repeatedUnitOffset);
			}
		}

	} else {
		util::thread_pool::task_waiter waiter;
		preload();

		for (size_t mipmapIndex = 0; mipmapIndex < mipmapOffsets.size(); ++mipmapIndex) {
			if (cancelled())
				return nullptr;

			const auto mipmapSize = mipmapSizes[mipmapIndex];
			const auto mipmapOffset = mipmapOffsets[mipmapIndex];
			blockDataList[mipmapIndex].resize(repeatCount);

			for (uint32_t repeatIndex = 0; repeatIndex < repeatCount; repeatIndex++) {
				if (cancelled())
					return nullptr;

				const auto repeatedUnitOffset = mipmapOffset + mipmapSize * repeatIndex;

				const auto blockAlignment = align<uint32_t>(mipmapSize, packed::MaxBlockDataSize);
				auto& blockDataVector = blockDataList[mipmapIndex][repeatIndex];
				blockDataVector.resize(blockAlignment.Count);

				blockAlignment.iterate_chunks_breakable([&](const uint32_t index, const uint32_t offset, const uint32_t length) {
					waiter.submit([this, offset, length, &blockData = blockDataVector[index]](auto& task) {
						if (task.cancelled())
							cancel();
						if (!cancelled())
							compress_block(offset, length, blockData);
					});

					return !cancelled();
				}, repeatedUnitOffset);
			}
		}

		if (!cancelled())
			waiter.wait_all();
	}

	if (cancelled())
		return nullptr;

	size_t entryBodyLength = textureHeaderAndMipmapOffsets.size();
	size_t subBlockCount = 0;
	for (auto& repeatedItem : blockDataList) {
		for (auto& mipmapItem : repeatedItem) {
			if (mipmapItem.empty())
				mipmapItem.emplace_back(false, 1, std::vector<uint8_t>(1));
			for (const auto& block : mipmapItem)
				entryBodyLength += align(sizeof(packed::block_header) + block.Data.size());
			subBlockCount += mipmapItem.size();
		}
	}
	entryBodyLength = align(entryBodyLength);

	const auto blockLocatorCount = mipmapCount * repeatCount;
	const auto entryHeaderLength = static_cast<uint16_t>(align(0
		+ sizeof(packed::file_header)
		+ blockLocatorCount * sizeof(packed::mipmap_block_locator)
		+ subBlockCount * sizeof(uint16_t)
	));

	std::vector<uint8_t> result(entryHeaderLength + entryBodyLength);

	auto& entryHeader = *reinterpret_cast<packed::file_header*>(result.data());
	entryHeader.Type = packed::type::texture;
	entryHeader.DecompressedSize = rawStreamSize;
	entryHeader.BlockCountOrVersion = static_cast<uint32_t>(blockLocatorCount);
	entryHeader.HeaderSize = entryHeaderLength;
	entryHeader.set_space_units(entryBodyLength);

	const auto blockLocators = util::span_cast<packed::mipmap_block_locator>(result, sizeof entryHeader, blockLocatorCount);
	const auto subBlockSizes = util::span_cast<uint16_t>(result, sizeof entryHeader + blockLocators.size_bytes(), subBlockCount);
	auto resultDataPtr = result.begin() + entryHeaderLength;
	resultDataPtr = std::ranges::copy(textureHeaderAndMipmapOffsets, resultDataPtr).out;

	auto blockOffsetCounter = static_cast<uint32_t>(std::span(textureHeaderAndMipmapOffsets).size_bytes());
	for (size_t mipmapIndex = 0, subBlockCounter = 0, blockLocatorIndexCounter = 0; mipmapIndex < mipmapOffsets.size(); ++mipmapIndex) {
		if (cancelled())
			return nullptr;

		for (uint32_t repeatIndex = 0; repeatIndex < repeatCount; repeatIndex++) {
			if (cancelled())
				return nullptr;

			auto& loc = blockLocators[blockLocatorIndexCounter++];
			loc.CompressedOffset = blockOffsetCounter;
			loc.CompressedSize = 0;
			loc.DecompressedSize = mipmapSizes[mipmapIndex];
			loc.FirstBlockIndex = blockLocatorIndexCounter == 1 ? 0 : blockLocators[blockLocatorIndexCounter - 2].FirstBlockIndex + blockLocators[blockLocatorIndexCounter - 2].BlockCount;
			loc.BlockCount = static_cast<uint32_t>(blockDataList[mipmapIndex][repeatIndex].size());

			for (auto& blockData : blockDataList[mipmapIndex][repeatIndex]) {
				if (cancelled())
					return {};

				auto& header = *reinterpret_cast<packed::block_header*>(&*resultDataPtr);
				header.HeaderSize = sizeof(packed::block_header);
				header.Version = 0;
				header.CompressedSize = blockData.Deflated ? static_cast<uint32_t>(blockData.Data.size()) : packed::block_header::CompressedSizeNotCompressed;
				header.DecompressedSize = blockData.DecompressedSize;

				std::ranges::copy(blockData.Data, resultDataPtr + sizeof header);

				const auto& subBlockSize = subBlockSizes[subBlockCounter++] = static_cast<uint16_t>(align(sizeof header + blockData.Data.size()));
				blockOffsetCounter += subBlockSize;
				loc.CompressedSize += subBlockSize;
				resultDataPtr += subBlockSize;

				std::vector<uint8_t>().swap(blockData.Data);
			}
		}
	}

	return std::make_unique<memory_stream>(std::move(result));
}
