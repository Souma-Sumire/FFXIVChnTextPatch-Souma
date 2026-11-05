#ifndef XIVRES_TEXTUREPACKEDFILESTREAMDECODER_H_
#define XIVRES_TEXTUREPACKEDFILESTREAMDECODER_H_

#include <limits>

#include "unpacked_stream.h"

namespace xivres {
	class texture_unpacker : public base_unpacker {
		struct subblock_info {
			uint32_t RequestOffset;
			uint32_t BlockOffset;
			uint16_t BlockSize;
			uint16_t DecompressedSize;

			[[nodiscard]] uint32_t request_offset_end() const {
				return RequestOffset + DecompressedSize;
			}

			operator bool() const {
				return RequestOffset != (std::numeric_limits<uint32_t>::max)();
			}
		};

		struct block_info {
			uint32_t DecompressedSize{};
			std::vector<subblock_info> Subblocks;

			[[nodiscard]] uint32_t request_offset_begin() const {
				return Subblocks.front().RequestOffset;
			}

			[[nodiscard]] uint32_t request_offset_end() const {
				return Subblocks.front().RequestOffset + DecompressedSize;
			}
		};

		std::vector<uint8_t> m_head;
		std::vector<block_info> m_blocks;

	public:
		texture_unpacker(const packed::file_header& header, std::shared_ptr<const packed_stream> strm);

		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) override;
	};
}

#endif
