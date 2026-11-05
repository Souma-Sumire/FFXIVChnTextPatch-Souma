#ifndef XIVRES_EMPTYOROBFUSCATEDPACKEDFILESTREAMDECODER_H_
#define XIVRES_EMPTYOROBFUSCATEDPACKEDFILESTREAMDECODER_H_

#include "unpacked_stream.h"
#include "util.zlib_wrapper.h"

namespace xivres {
	class placeholder_unpacker : public base_unpacker {
		std::optional<util::zlib_inflater> m_inflater;
		std::shared_ptr<partial_view_stream> m_partialView;
		std::optional<stream_as_packed_stream> m_provider;

	public:
		placeholder_unpacker(const packed::file_header& header, std::shared_ptr<const packed_stream> strm, std::span<uint8_t> headerRewrite = {});

		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) override;
	};
}

#endif
