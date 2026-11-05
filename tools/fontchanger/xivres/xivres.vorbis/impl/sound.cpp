#include "../include/xivres/sound.h"

#include <deque>
#include <format>
#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>

#include "../include/xivres/common.h"
#include "../include/xivres/util.on_dtor.h"
#include "../include/xivres/util.thread_pool.h"

static constexpr auto ReadBufferSize = 64 * 1024;
static constexpr auto FragmentedBufferSize = 256 * 1024;

// https://github.com/xiph/vorbis/blob/master/examples/decoder_example.c
xivres::sound::reader::sound_item::audio_info xivres::sound::reader::sound_item::get_ogg_decoded() const {
	return decode_ogg(get_ogg_file());
}

xivres::sound::reader::sound_item::audio_info xivres::sound::reader::sound_item::decode_ogg(const std::vector<uint8_t>& oggf) {
	audio_info result{};
	size_t totalDecodedSize = 0;

	ogg_sync_state   oy; /* sync and verify incoming physical bitstream */
	ogg_stream_state os; /* take physical pages, weld into a logical
							stream of packets */
	ogg_page         og; /* one Ogg bitstream page. Vorbis packets are inside */
	ogg_packet       op; /* one raw packet of data for decode */

	vorbis_info      vi; /* struct that stores all the static vorbis bitstream
							settings */
	vorbis_comment   vc; /* struct that stores all the bitstream user comments */
	vorbis_dsp_state vd; /* central working state for the packet->PCM decoder */
	vorbis_block     vb; /* local working space for packet->PCM decode */

	ogg_sync_init(&oy);
	const auto oyCleanup = util::on_dtor([&oy] { ogg_sync_clear(&oy); });

	auto feedMore = [&, in = std::span(oggf)]() mutable {
		const auto bytes = (std::min)(static_cast<int>(in.size()), ReadBufferSize);
		if (bytes) {
			char* buffer = ogg_sync_buffer(&oy, bytes);
			memcpy(buffer, &in[0], bytes);
			in = in.subspan(bytes);
			ogg_sync_wrote(&oy, bytes);
		}
		return !!bytes;
	};

	std::deque<std::vector<uint8_t>> decoded;

	while (feedMore()) {
		if (ogg_sync_pageout(&oy, &og) != 1)
			continue;

		vorbis_info_init(&vi);
		const auto viCleanup = util::on_dtor([&vi] { vorbis_info_clear(&vi); });

		vorbis_comment_init(&vc);
		const auto vcCleanup = util::on_dtor([&vc] { vorbis_comment_clear(&vc); });

		ogg_stream_init(&os, ogg_page_serialno(&og));
		const auto osCleanup = util::on_dtor([&os] { ogg_stream_clear(&os); });

		if (const auto res = ogg_stream_pagein(&os, &og); res < 0)
			throw bad_data_error(std::format("ogg_stream_pagein error: {}", res));

		if (const auto res = ogg_stream_packetout(&os, &op); res < 0)
			throw bad_data_error(std::format("ogg_stream_packetout error: {}", res));

		if (const auto res = vorbis_synthesis_headerin(&vi, &vc, &op); res < 0)
			throw bad_data_error(std::format("vorbis_synthesis_headerin error: {}", res));

		for (int i = 0, limit = 2; i < limit;) {
			while (i < 2) {
				if (const auto res = ogg_sync_pageout(&oy, &og); res == 1) {
					ogg_stream_pagein(&os, &og);
					while (i < limit) {
						if (const auto res = ogg_stream_packetout(&os, &op); res < 0)
							throw bad_data_error(std::format("ogg_stream_packetout error: {}", res));
						else if (res == 0)
							break;

						if (const auto res = vorbis_synthesis_headerin(&vi, &vc, &op); res < 0)
							throw bad_data_error(std::format("vorbis_synthesis_headerin error: {}", res));
						i++;
					}
				} else if (res == 0)
					break;
			}

			if (!feedMore() && i < limit)
				throw bad_data_error("EOF encountered before finding all Vorbis headers");
		}

		result.Channels = vi.channels;
		result.SamplingRate = vi.rate;
		for (auto comments = vc.user_comments; *comments; comments++) {
			if (_strnicmp(*comments, "LoopStart=", 10) == 0)
				result.LoopStartBlockIndex = std::strtoul(*comments + 10, nullptr, 10);
			else if (_strnicmp(*comments, "LoopEnd=", 8) == 0)
				result.LoopEndBlockIndex = std::strtoul(*comments + 8, nullptr, 10);
		}

		if (const auto res = vorbis_synthesis_init(&vd, &vi); res != 0)
			throw bad_data_error(std::format("vorbis_synthesis_init error: {}", res));
		const auto vdCleanup = util::on_dtor([&vd] { vorbis_dsp_clear(&vd); });

		if (const auto res = vorbis_block_init(&vd, &vb); res != 0)
			throw bad_data_error(std::format("vorbis_block_init error: {}", res));
		const auto vbCleanup = util::on_dtor([&vb] { vorbis_block_clear(&vb); });

		while (true) {
			if (const auto res = ogg_sync_pageout(&oy, &og); res < 0)
				throw bad_data_error(std::format("ogg_sync_pageout error: {}", res));
			else if (res == 0) {
				if (!feedMore())
					break;
				continue;
			}

			if (const auto res = ogg_stream_pagein(&os, &og); res < 0)
				throw bad_data_error(std::format("ogg_stream_pagein error: {}", res));

			while (true) {
				if (const auto res = ogg_stream_packetout(&os, &op); res < 0)
					throw bad_data_error(std::format("ogg_stream_packetout error: {}", res));
				else if (res == 0)
					break;

				if (const auto res = vorbis_synthesis(&vb, &op); res < 0)
					throw bad_data_error(std::format("vorbis_synthesis error: {}", res));

				if (const auto res = vorbis_synthesis_blockin(&vd, &vb); res < 0)
					throw bad_data_error(std::format("vorbis_synthesis_blockin error: {}", res));

				float** ppSamples;
				int nSamples = vorbis_synthesis_pcmout(&vd, &ppSamples);
				if (!nSamples)
					continue;

				const auto newDataSize = sizeof(float) * nSamples * vi.channels;
				totalDecodedSize += newDataSize;

				if (decoded.empty() || decoded.back().size() + newDataSize > decoded.back().capacity()) {
					decoded.emplace_back();
					decoded.back().reserve(FragmentedBufferSize);
				}
				decoded.back().resize(decoded.back().size() + newDataSize);

				const auto floatView = std::span(reinterpret_cast<float*>(decoded.back().data() + decoded.back().size() - newDataSize), decoded.back().size() / sizeof(float));
				for (int i = 0; i < nSamples; i++) {
					for (int j = 0; j < vi.channels; j++)
						floatView[static_cast<size_t>(1) * i * vi.channels + j] = ppSamples[j][i];
				}

				if (const auto res = vorbis_synthesis_read(&vd, nSamples); res < 0)
					throw bad_data_error(std::format("vorbis_synthesis_read error: {}", res));
			}
		}
	}

	result.Data = std::move(decoded.front());
	decoded.pop_front();
	result.Data.reserve(totalDecodedSize);
	while (!decoded.empty()) {
		result.Data.insert(result.Data.end(), decoded.front().begin(), decoded.front().end());
		decoded.pop_front();
	}
	return result;
}

xivres::sound::writer::sound_item xivres::sound::writer::sound_item::make_from_ogg_encode(
	size_t channels,
	size_t samplingRate,
	size_t loopStartBlockIndex,
	size_t loopEndBlockIndex,
	const linear_reader<uint8_t>& floatSamplesReader,
	const std::function<bool(size_t)>& progressCallback,
	std::span<const uint32_t> markIndices,
	float baseQuality
) {
	vorbis_info vi{};
	vorbis_dsp_state vd{};
	vorbis_block vb{};
	ogg_stream_state os{};
	ogg_page og{};
	ogg_packet op{};
	util::on_dtor::multi oggCleanup;

	std::vector<uint8_t> headerBuffer;
	std::deque<std::vector<uint8_t>> dataBuffers;
	std::vector<uint32_t> oggDataSeekTable;
	uint32_t dataBufferTotalSize = 0;
	uint32_t loopStartOffset = 0;
	uint32_t loopEndOffset = 0;

	vorbis_info_init(&vi);
	if (const auto res = vorbis_encode_init_vbr(&vi, static_cast<long>(channels), static_cast<long>(samplingRate), baseQuality))
		throw std::runtime_error(std::format("vorbis_encode_init_vbr: {}", res));
	oggCleanup += [&vi] { vorbis_info_clear(&vi); };

	if (const auto res = vorbis_analysis_init(&vd, &vi))
		throw std::runtime_error(std::format("vorbis_analysis_init: {}", res));
	oggCleanup += [&vd] { vorbis_dsp_clear(&vd); };

	if (const auto res = vorbis_block_init(&vd, &vb))
		throw std::runtime_error(std::format("vorbis_block_init: {}", res));
	oggCleanup += [&vb] { vorbis_block_clear(&vb); };

	if (const auto res = ogg_stream_init(&os, 0))
		throw std::runtime_error(std::format("ogg_stream_init: {}", res));
	oggCleanup += [&os] { ogg_stream_clear(&os); };

	{
		vorbis_comment vc{};
		vorbis_comment_init(&vc);
		const auto vcCleanup = util::on_dtor([&vc] { vorbis_comment_clear(&vc); });
		if (loopStartBlockIndex || loopEndBlockIndex) {
			vorbis_comment_add_tag(&vc, "LoopStart", std::format("{}", loopStartBlockIndex).c_str());
			vorbis_comment_add_tag(&vc, "LoopEnd", std::format("{}", loopEndBlockIndex).c_str());
		}

		ogg_packet header{};
		ogg_packet headerComments{};
		ogg_packet headerCode{};
		vorbis_analysis_headerout(&vd, &vc, &header, &headerComments, &headerCode);
		ogg_stream_packetin(&os, &header);
		ogg_stream_packetin(&os, &headerComments);
		ogg_stream_packetin(&os, &headerCode);

		headerBuffer.reserve(8192);
		while (true) {
			if (const auto res = ogg_stream_flush_fill(&os, &og, 0); res < 0)
				throw std::runtime_error(std::format("ogg_stream_flush_fill: {}", res));
			else if (res == 0)
				break;

			headerBuffer.insert(headerBuffer.end(), og.header, og.header + og.header_len);
			headerBuffer.insert(headerBuffer.end(), og.body, og.body + og.body_len);
		}
	}

	for (size_t currentBlockIndex = 0; ;) {
		size_t currentBlockCount = ReadBufferSize / channels;
		if (loopStartBlockIndex || loopEndBlockIndex) {
			if (currentBlockIndex < loopStartBlockIndex && loopStartBlockIndex <= currentBlockIndex + currentBlockCount)
				currentBlockCount = loopStartBlockIndex - currentBlockIndex;
			else if (currentBlockIndex + currentBlockCount >= loopEndBlockIndex)
				currentBlockCount = loopEndBlockIndex - currentBlockIndex;
		}

		std::span<uint8_t> data = floatSamplesReader(sizeof(float) * channels * currentBlockCount, false);
		std::span<float> dataf;
		if (!data.empty())
			dataf = { reinterpret_cast<float*>(&data[0]), data.size() / sizeof(float) };

		currentBlockCount = dataf.size() / channels;
		currentBlockIndex += currentBlockCount;

		if (data.empty()) {
			if (const auto res = vorbis_analysis_wrote(&vd, 0); res < 0)
				throw std::runtime_error(std::format("vorbis_analysis_wrote: {}", res));
		} else {
			if (const auto buf = vorbis_analysis_buffer(&vd, static_cast<int>(currentBlockCount))) {
				for (size_t i = 0; i < channels; i++) {
					for (size_t j = 0; j < currentBlockCount; j++) {
						buf[i][j] = dataf[j * channels + i];
					}
				}
			} else
				throw std::runtime_error("vorbis_analysis_buffer fail");

			if (const auto res = vorbis_analysis_wrote(&vd, static_cast<int>(currentBlockCount)); res < 0)
				throw std::runtime_error(std::format("vorbis_analysis_wrote: {}", res));
		}

		while (!ogg_page_eos(&og)) {
			if (const auto res = vorbis_analysis_blockout(&vd, &vb); res < 0)
				throw std::runtime_error(std::format("vorbis_analysis_blockout: {}", res));
			else if (res == 0)
				break;

			if (const auto res = vorbis_analysis(&vb, nullptr); res < 0)
				throw std::runtime_error(std::format("vorbis_analysis: {}", res));
			if (const auto res = vorbis_bitrate_addblock(&vb); res < 0)
				throw std::runtime_error(std::format("vorbis_bitrate_addblock: {}", res));

			while (!ogg_page_eos(&og)) {
				if (const auto res = vorbis_bitrate_flushpacket(&vd, &op); res < 0)
					throw std::runtime_error(std::format("vorbis_bitrate_flushpacket: {}", res));
				else if (res == 0)
					break;

				if (const auto res = ogg_stream_packetin(&os, &op); res < 0)
					throw std::runtime_error(std::format("ogg_stream_packetin: {}", res));

				while (!ogg_page_eos(&og)) {
					if (const auto res = ogg_stream_pageout_fill(&os, &og, 65307);
						res < 0)
						throw std::runtime_error(std::format("ogg_stream_pageout_fill(65307): {}", res));
					else if (res == 0)
						break;

					oggDataSeekTable.push_back(dataBufferTotalSize);

					const auto blockSize = og.header_len + og.body_len;
					dataBufferTotalSize += blockSize;

					if (dataBuffers.empty() || dataBuffers.back().size() + blockSize > dataBuffers.back().capacity()) {
						dataBuffers.emplace_back();
						dataBuffers.back().reserve(FragmentedBufferSize);
					}
					dataBuffers.back().insert(dataBuffers.back().end(), og.header, og.header + og.header_len);
					dataBuffers.back().insert(dataBuffers.back().end(), og.body, og.body + og.body_len);
				}
			}
		}

		if ((loopStartBlockIndex || loopEndBlockIndex) && currentBlockIndex == loopStartBlockIndex) {
			while (!ogg_page_eos(&og)) {
				if (const auto res = ogg_stream_flush_fill(&os, &og, 0);
					res < 0)
					throw std::runtime_error(std::format("ogg_stream_pageout_fill(0): {}", res));
				else if (res == 0)
					break;

				oggDataSeekTable.push_back(dataBufferTotalSize);

				const auto blockSize = og.header_len + og.body_len;
				dataBufferTotalSize += blockSize;

				if (dataBuffers.empty() || dataBuffers.back().size() + blockSize > dataBuffers.back().capacity()) {
					dataBuffers.emplace_back();
					dataBuffers.back().reserve(1048576);
				}
				dataBuffers.back().insert(dataBuffers.back().end(), og.header, og.header + og.header_len);
				dataBuffers.back().insert(dataBuffers.back().end(), og.body, og.body + og.body_len);
			}

			loopStartOffset = dataBufferTotalSize;
		}

		if (progressCallback && !progressCallback(currentBlockIndex))
			return {};
		if (data.empty())
			break;
	}

	if (loopEndBlockIndex && !loopEndOffset)
		loopEndOffset = dataBufferTotalSize;

	std::vector<uint8_t> dataBuffer = std::move(dataBuffers[0]);
	dataBuffers.pop_front();
	dataBuffer.reserve(dataBufferTotalSize);
	while (!dataBuffers.empty()) {
		dataBuffer.insert(dataBuffer.end(), dataBuffers.front().begin(), dataBuffers.front().end());
		dataBuffers.pop_front();
	}

	auto soundEntry = make_from_ogg(
		std::move(headerBuffer), std::move(dataBuffer),
		static_cast<uint32_t>(channels),
		static_cast<uint32_t>(samplingRate),
		static_cast<uint32_t>(loopStartOffset),
		static_cast<uint32_t>(loopEndOffset),
		std::span(oggDataSeekTable)
	);

	if (!markIndices.empty())
		soundEntry.set_mark_chunks(static_cast<uint32_t>(loopStartBlockIndex), static_cast<uint32_t>(loopEndBlockIndex), markIndices);
	return soundEntry;
}

xivres::sound::writer::sound_item xivres::sound::writer::sound_item::make_from_ogg(const linear_reader<uint8_t>& reader) {
	ogg_sync_state oy{};
	ogg_sync_init(&oy);
	const auto oyCleanup = util::on_dtor([&oy] { ogg_sync_clear(&oy); });

	vorbis_info vi{};
	vorbis_info_init(&vi);
	const auto viCleanup = util::on_dtor([&vi] { vorbis_info_clear(&vi); });

	vorbis_comment vc{};
	vorbis_comment_init(&vc);
	const auto vcCleanup = util::on_dtor([&vc] { vorbis_comment_clear(&vc); });

	ogg_stream_state os{};
	util::on_dtor osCleanup;
	
	constexpr uint32_t NotFound = UINT32_MAX;

	std::vector<uint8_t> header;
	std::vector<uint8_t> data;
	std::vector<uint32_t> seekTable;
	std::vector<uint32_t> seekTableSamples;
	uint32_t loopStartSample = 0, loopEndSample = 0;
	uint32_t loopStartOffset = NotFound, loopEndOffset = NotFound;
	ogg_page og{};
	ogg_packet op{};
	for (size_t packetIndex = 0, pageIndex = 0; ; ) {
		const auto read = reader(4096, false);
		if (read.empty())
			break;
		if (const auto buffer = ogg_sync_buffer(&oy, static_cast<long>(read.size())))
			memcpy(buffer, read.data(), read.size());
		else
			throw std::runtime_error("ogg_sync_buffer failed");
		if (0 != ogg_sync_wrote(&oy, static_cast<long>(read.size())))
			throw std::runtime_error("ogg_sync_wrote failed");

		for (;; ++pageIndex) {
			if (auto r = ogg_sync_pageout(&oy, &og); r == -1)
				throw std::invalid_argument("ogg_sync_pageout failed");
			else if (r == 0)
				break;

			if (pageIndex == 0) {
				if (0 != ogg_stream_init(&os, ogg_page_serialno(&og)))
					throw std::runtime_error("ogg_stream_init failed");
				osCleanup = [&os] { ogg_stream_clear(&os); };
			}

			if (0 != ogg_stream_pagein(&os, &og))
				throw std::runtime_error("ogg_stream_pagein failed");

			if (packetIndex < 3) {
				header.insert(header.end(), og.header, og.header + og.header_len);
				header.insert(header.end(), og.body, og.body + og.body_len);
			} else {
				const auto sampleIndexAtEndOfPage = static_cast<uint32_t>(ogg_page_granulepos(&og));
				if (loopStartSample && loopStartOffset == NotFound && loopStartSample <= sampleIndexAtEndOfPage)
					loopStartOffset = static_cast<uint32_t>(data.size());

				seekTable.push_back(static_cast<uint32_t>(data.size()));
				seekTableSamples.push_back(sampleIndexAtEndOfPage);
				data.insert(data.end(), og.header, og.header + og.header_len);
				data.insert(data.end(), og.body, og.body + og.body_len);

				if (loopEndSample && loopEndOffset == NotFound && loopEndSample < sampleIndexAtEndOfPage)
					loopEndOffset = static_cast<uint32_t>(data.size());
			}

			for (;; ++packetIndex) {
				if (auto r = ogg_stream_packetout(&os, &op); r == -1)
					throw std::runtime_error("ogg_stream_packetout failed");
				else if (r == 0)
					break;

				if (packetIndex < 3) {
					if (const auto res = vorbis_synthesis_headerin(&vi, &vc, &op))
						throw std::runtime_error(std::format("vorbis_synthesis_headerin failed: {}", res));
					if (packetIndex == 2) {
						for (auto comments = vc.user_comments; *comments; comments++) {
							if (_strnicmp(*comments, "LoopStart=", 10) == 0)
								loopStartSample = std::strtoul(*comments + 10, nullptr, 10);
							else if (_strnicmp(*comments, "LoopEnd=", 8) == 0)
								loopEndSample = std::strtoul(*comments + 8, nullptr, 10);
						}
					}
				}
			}

			if (ogg_page_eos(&og)) {
				if (loopEndSample && loopEndOffset == NotFound)
					loopEndOffset = static_cast<uint32_t>(data.size());

				return make_from_ogg(
					std::move(header), std::move(data),
					static_cast<uint32_t>(vi.channels), static_cast<uint32_t>(vi.rate),
					loopStartOffset == NotFound ? 0 : loopStartOffset,
					loopEndOffset == NotFound ? 0 : loopEndOffset,
					std::span(seekTable)
				);
			}
		}
	}

	throw std::invalid_argument("ogg: eos not found");
}
