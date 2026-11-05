#ifndef XIVRES_EMPTYOROBFUSCATEDPACKEDFILESTREAM_H_
#define XIVRES_EMPTYOROBFUSCATEDPACKEDFILESTREAM_H_

#include "packed_stream.h"

namespace xivres {
	class placeholder_packed_stream : public packed_stream {
		const std::shared_ptr<const stream> m_stream;
		const packed::file_header m_header;

	public:
		placeholder_packed_stream(xivres::path_spec pathSpec);

		placeholder_packed_stream(xivres::path_spec pathSpec, std::shared_ptr<const stream> strm, uint32_t decompressedSize = UINT32_MAX);

		[[nodiscard]] std::streamsize size() const override;

		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override;

		[[nodiscard]] packed::type get_packed_type() const override;

		[[nodiscard]] static const placeholder_packed_stream& instance();
	};
}

#endif
