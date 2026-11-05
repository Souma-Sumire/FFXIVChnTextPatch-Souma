#ifndef XIVRES_TEXTUREPACKEDFILESTREAM_H_
#define XIVRES_TEXTUREPACKEDFILESTREAM_H_

#include "packed_stream.h"

namespace xivres {
	class texture_passthrough_packer : public passthrough_packer<packed::type::texture> {
		static constexpr auto MaxMipmapCountPerTexture = 16;

		std::mutex m_mtx;

		std::vector<uint8_t> m_mergedHeader;
		std::vector<packed::mipmap_block_locator> m_blockLocators;
		std::vector<uint32_t> m_mipmapOffsets;
		std::vector<uint32_t> m_mipmapSizes;
		size_t m_repeatCount{};

	public:
		using passthrough_packer::passthrough_packer;

		[[nodiscard]] std::streamsize size() override;

	protected:
		void ensure_initialized() override;

		std::streamsize translate_read(std::streamoff offset, void* buf, std::streamsize length) override;
	};

	class texture_compressing_packer : public compressing_packer {
	public:
		static constexpr auto Type = packed::type::texture;
		using compressing_packer::compressing_packer;
		
		[[nodiscard]] std::unique_ptr<stream> pack() override;
	};
}

#endif
