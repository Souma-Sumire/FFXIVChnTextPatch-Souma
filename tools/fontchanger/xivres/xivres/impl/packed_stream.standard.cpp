#include "../include/xivres/packed_stream.standard.h"

#include <ranges>

#include "../include/xivres/util.thread_pool.h"
#include "../include/xivres/util.zlib_wrapper.h"

xivres::standard_passthrough_packer::standard_passthrough_packer(std::shared_ptr<const stream> strm)
	: passthrough_packer(std::move(strm)) {
}

std::streamsize xivres::standard_passthrough_packer::size() {
	ensure_initialized();
	return static_cast<std::streamsize>(reinterpret_cast<const packed::file_header*>(m_header.data())->occupied_size());
}

void xivres::standard_passthrough_packer::ensure_initialized() {
	if (!m_header.empty())
		return;

	const auto lock = std::scoped_lock(m_mtx);

	if (!m_header.empty())
		return;

	const auto size = m_stream->size();
	const auto blockAlignment = align<uint32_t>(static_cast<uint32_t>(size), packed::MaxBlockDataSize);
	const auto headerAlignment = align(sizeof(packed::file_header) + blockAlignment.Count * sizeof(packed::standard_block_locator));

	m_header.resize(headerAlignment.Alloc);
	auto& header = *reinterpret_cast<packed::file_header*>(m_header.data());
	const auto locators = util::span_cast<packed::standard_block_locator>(m_header, sizeof header, blockAlignment.Count);

	header = {
		.HeaderSize = static_cast<uint32_t>(headerAlignment),
		.Type = packed::type::standard,
		.DecompressedSize = static_cast<uint32_t>(size),
		.AllocatedSpaceUnitCount = {},
		.OccupiedSpaceUnitCount = {},
		.BlockCountOrVersion = blockAlignment.Count,
	};
	header.set_space_units((static_cast<size_t>(blockAlignment.Count) - 1) * packed::MaxBlockSize + sizeof(packed::block_header) + blockAlignment.Last);

	blockAlignment.iterate_chunks([&](uint32_t index, uint32_t /*offset*/, uint32_t blockSize) {
		locators[index].Offset = index == 0 ? 0 : locators[index - 1].Offset + locators[index - 1].BlockSize;
		locators[index].BlockSize = static_cast<uint16_t>(align(sizeof(packed::block_header) + blockSize));
		locators[index].DecompressedDataSize = static_cast<uint16_t>(blockSize);
	});
}

std::streamsize xivres::standard_passthrough_packer::translate_read(std::streamoff offset, void* buf, std::streamsize length) {
	if (!length)
		return 0;

	const auto& header = *reinterpret_cast<const packed::file_header*>(m_header.data());

	auto relativeOffset = static_cast<uint64_t>(offset);
	auto out = std::span(static_cast<char*>(buf), static_cast<size_t>(length));

	if (relativeOffset < m_header.size()) {
		const auto src = std::span(m_header).subspan(static_cast<size_t>(relativeOffset));
		const auto available = (std::min)(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty()) return length;
	} else
		relativeOffset -= m_header.size();

	const auto blockAlignment = align<uint32_t>(static_cast<uint32_t>(m_stream->size()), packed::MaxBlockDataSize);
	if (static_cast<uint32_t>(relativeOffset) < header.OccupiedSpaceUnitCount * EntryAlignment) {
		const auto i = relativeOffset / packed::MaxBlockSize;
		relativeOffset -= i * packed::MaxBlockSize;

		blockAlignment.iterate_chunks_breakable([&](uint32_t, uint32_t blockOffset, uint32_t size) {
			if (relativeOffset < sizeof(packed::block_header)) {
				const auto blockHeader = packed::block_header{
					.HeaderSize = sizeof(packed::block_header),
					.Version = 0,
					.CompressedSize = packed::block_header::CompressedSizeNotCompressed,
					.DecompressedSize = size,
				};
				const auto src = util::span_cast<uint8_t>(1, &blockHeader).subspan(static_cast<size_t>(relativeOffset));
				const auto available = (std::min)(out.size_bytes(), src.size_bytes());
				std::copy_n(src.begin(), available, out.begin());
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty()) return false;
			} else
				relativeOffset -= sizeof(packed::block_header);

			if (relativeOffset < size) {
				const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(size - relativeOffset));
				m_stream->read_fully(static_cast<std::streamoff>(blockOffset + relativeOffset), out.data(), static_cast<std::streamsize>(available));
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty()) return false;
			} else
				relativeOffset -= size;

			if (const auto pad = align(sizeof(packed::block_header) + size).Pad; relativeOffset < pad) {
				const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(pad - relativeOffset));
				std::fill_n(out.begin(), available, 0);
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty()) return false;
			} else
				relativeOffset -= pad;

			return true;
		}, 0, static_cast<uint32_t>(i));
	}

	return static_cast<std::streamsize>(length - out.size_bytes());
}

std::unique_ptr<xivres::stream> xivres::standard_compressing_packer::pack() {
	const auto rawStreamSize = static_cast<uint32_t>(unpacked().size());

	const auto blockAlignment = align<uint32_t>(rawStreamSize, packed::MaxBlockDataSize);
	std::vector<block_data> blockDataList(blockAlignment.Count);

	if (!multithreaded()) {
		blockAlignment.iterate_chunks_breakable([&](const uint32_t index, const uint32_t offset, const uint32_t length) {
			compress_block(offset, length, blockDataList[index]);
			return !cancelled();
		});
		
	} else {
		util::thread_pool::task_waiter waiter;
		preload();

		blockAlignment.iterate_chunks_breakable([&](const uint32_t index, const uint32_t offset, const uint32_t length) {
			waiter.submit([this, offset, length, &blockData = blockDataList[index]](auto& task) {
				if (task.cancelled())
					cancel();
				if (!cancelled())
					compress_block(offset, length, blockData);
			});

			return !cancelled();
		});

		if (!cancelled())
			waiter.wait_all();
	}

	if (cancelled())
		return nullptr;

	const auto entryHeaderLength = static_cast<uint16_t>(align(0
		+ sizeof(packed::file_header)
		+ sizeof(packed::standard_block_locator) * blockAlignment.Count
	));
	size_t entryBodyLength = 0;
	for (const auto& blockData : blockDataList)
		entryBodyLength += align(sizeof(packed::block_header) + blockData.Data.size());

	std::vector<uint8_t> result(entryHeaderLength + entryBodyLength);

	auto& entryHeader = *reinterpret_cast<packed::file_header*>(result.data());
	entryHeader.Type = packed::type::standard;
	entryHeader.DecompressedSize = rawStreamSize;
	entryHeader.BlockCountOrVersion = blockAlignment.Count;
	entryHeader.HeaderSize = entryHeaderLength;
	entryHeader.set_space_units(entryBodyLength);

	const auto locators = util::span_cast<packed::standard_block_locator>(result, sizeof entryHeader, blockAlignment.Count);
	auto resultDataPtr = result.begin() + entryHeaderLength;

	blockAlignment.iterate_chunks_breakable([&](const uint32_t index, const uint32_t /*offset*/, const uint32_t length) {
		if (cancelled())
			return false;
		auto& blockData = blockDataList[index];

		auto& header = *reinterpret_cast<packed::block_header*>(&*resultDataPtr);
		header.HeaderSize = sizeof(packed::block_header);
		header.Version = 0;
		header.CompressedSize = blockData.Deflated ? static_cast<uint32_t>(blockData.Data.size()) : packed::block_header::CompressedSizeNotCompressed;
		header.DecompressedSize = length;

		std::ranges::copy(blockData.Data, resultDataPtr + sizeof header);

		locators[index].Offset = index == 0 ? 0 : locators[index - 1].BlockSize + locators[index - 1].Offset;
		locators[index].BlockSize = static_cast<uint16_t>(align(sizeof header + blockData.Data.size()));
		locators[index].DecompressedDataSize = static_cast<uint16_t>(length);

		resultDataPtr += locators[index].BlockSize;

		std::vector<uint8_t>().swap(blockData.Data);
		return true;
	});

	return std::make_unique<memory_stream>(std::move(result));
}
