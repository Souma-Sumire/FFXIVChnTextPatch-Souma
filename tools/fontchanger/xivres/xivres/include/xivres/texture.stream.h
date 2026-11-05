#ifndef XIVRES_TEXTURESTREAM_H_
#define XIVRES_TEXTURESTREAM_H_

#include "common.h"
#include "texture.h"
#include "texture.mipmap_stream.h"

namespace xivres::texture {
	class stream : public default_base_stream {
		header m_header;
		std::vector<std::vector<std::shared_ptr<mipmap_stream>>> m_repeats;
		std::vector<uint32_t> m_mipmapOffsets;
		uint32_t m_repeatedUnitSize;

	public:
		stream(const std::shared_ptr<xivres::stream>& strm);

		stream(format_type type, size_t width, size_t height, size_t depth = 1, size_t mipmapCount = 1, size_t repeatCount = 1);

		void set_mipmap(size_t mipmapIndex, size_t repeatIndex, std::shared_ptr<mipmap_stream> mipmap);

		void resize(size_t mipmapCount, size_t repeatCount);

		[[nodiscard]] std::streamsize size() const override;

		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override;

		[[nodiscard]] format_type type() const {
			return m_header.Type;
		}

		[[nodiscard]] uint16_t width() const {
			return m_header.Width;
		}

		[[nodiscard]] uint16_t height() const {
			return m_header.Height;
		}

		[[nodiscard]] uint16_t depth() const {
			return m_header.Depth;
		}

		[[nodiscard]] uint16_t mipmap_count() const {
			return m_header.MipmapCount;
		}

		[[nodiscard]] uint16_t repeat_count() const {
			return static_cast<uint16_t>(m_repeats.size());
		}

		[[nodiscard]] size_t calc_repeat_unit_size(size_t mipmapCount) const {
			size_t res = 0;
			for (size_t i = 0; i < mipmapCount; ++i)
				res += static_cast<uint32_t>(align(calc_raw_data_length(m_header, i)).Alloc);
			return res;
		}

		[[nodiscard]] std::shared_ptr<mipmap_stream> mipmap_at(size_t repeatIndex, size_t mipmapIndex) const {
			return m_repeats.at(repeatIndex).at(mipmapIndex);
		}
	};
}

#endif
