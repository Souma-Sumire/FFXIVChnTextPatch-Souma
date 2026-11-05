#pragma once

#include "util.pixel_formats.h"
#include "stream.h"
#include "texture.h"

namespace xivres::texture {
	class stream;

	class mipmap_stream : public default_base_stream {
	public:
		const uint16_t Width;
		const uint16_t Height;
		const uint16_t Depth;
		const format_type Type;
		const std::streamsize SupposedMipmapLength;

		mipmap_stream(size_t width, size_t height, size_t depths, format_type type);

		[[nodiscard]] std::streamsize size() const override;

		[[nodiscard]] std::shared_ptr<texture::stream> to_single_texture_stream();
	};

	class wrapped_mipmap_stream : public mipmap_stream {
		std::shared_ptr<const stream> m_underlying;

	public:
		wrapped_mipmap_stream(const header& header, size_t mipmapIndex, std::shared_ptr<const stream> underlying);

		wrapped_mipmap_stream(size_t width, size_t height, size_t depths, format_type type, std::shared_ptr<const stream> underlying);

		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override;
	};

	class memory_mipmap_stream : public mipmap_stream {
		std::vector<uint8_t> m_data;

	public:
		memory_mipmap_stream(size_t width, size_t height, size_t depths, format_type type);

		memory_mipmap_stream(size_t width, size_t height, size_t depths, format_type type, std::vector<uint8_t> data);

		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override;

		template<typename T>
		[[nodiscard]] auto as_span() {
			return util::span_cast<T>(m_data);
		}

		template<typename T>
		[[nodiscard]] auto as_span() const {
			return util::span_cast<const T>(m_data);
		}

		[[nodiscard]] static std::shared_ptr<memory_mipmap_stream> as_argb8888(const mipmap_stream& strm);

		[[nodiscard]] static std::shared_ptr<const mipmap_stream> as_argb8888_view(std::shared_ptr<const mipmap_stream> strm);
	};
}
