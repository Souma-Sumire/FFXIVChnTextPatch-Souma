#ifndef XIVRES_BINARYPACKEDFILESTREAM_H_
#define XIVRES_BINARYPACKEDFILESTREAM_H_

#include "packed_stream.h"
#include "util.zlib_wrapper.h"

namespace xivres {
	class standard_passthrough_packer : public passthrough_packer<packed::type::standard> {
		std::vector<uint8_t> m_header;
		std::mutex m_mtx;

	public:
		standard_passthrough_packer(std::shared_ptr<const stream> strm);

		[[nodiscard]] std::streamsize size() override;

	protected:
		void ensure_initialized() override;

		std::streamsize translate_read(std::streamoff offset, void* buf, std::streamsize length) override;
	};

	class standard_compressing_packer : public compressing_packer {
	public:
		static constexpr auto Type = packed::type::standard;
		using compressing_packer::compressing_packer;

		[[nodiscard]] std::unique_ptr<stream> pack() override;
	};
}

#endif
