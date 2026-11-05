#ifndef XIVRES_MODELPACKEDFILESTREAM_H_
#define XIVRES_MODELPACKEDFILESTREAM_H_

#include "packed_stream.h"
#include "util.thread_pool.h"

namespace xivres {
	class model_passthrough_packer : public passthrough_packer<packed::type::model> {
		struct ModelEntryHeader {
			packed::file_header Entry;
			packed::model_block_locator Model;
		} m_header{};
		std::vector<uint32_t> m_blockOffsets;
		std::vector<uint16_t> m_blockDataSizes;
		std::vector<uint16_t> m_paddedBlockSizes;
		std::vector<uint32_t> m_actualFileOffsets;
		std::mutex m_mtx;

	public:
		model_passthrough_packer(std::shared_ptr<const stream> strm);

		[[nodiscard]] std::streamsize size() override;

	protected:
		void ensure_initialized() override;

		std::streamsize translate_read(std::streamoff offset, void* buf, std::streamsize length) override;
	};

	class model_compressing_packer : public compressing_packer {
	public:
		static constexpr auto Type = packed::type::model;
		using compressing_packer::compressing_packer;
		
		[[nodiscard]] std::unique_ptr<stream> pack() override;
	};
}

#endif
