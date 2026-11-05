#include "../include/xivres/texture.mipmap_stream.h"
#include "../include/xivres/texture.stream.h"
#include "../include/xivres/util.dxt.h"

xivres::texture::mipmap_stream::mipmap_stream(size_t width, size_t height, size_t depths, format_type type)
	: Width(static_cast<uint16_t>(width))
	, Height(static_cast<uint16_t>(height))
	, Depth(static_cast<uint16_t>(depths))
	, Type(type)
	, SupposedMipmapLength(static_cast<std::streamsize>(calc_raw_data_length(type, width, height, depths))) {
	if (Width != width || Height != height || Depth != depths)
		throw std::invalid_argument("dimensions can hold only uint16 ranges");
}

std::streamsize xivres::texture::mipmap_stream::size() const {
	return SupposedMipmapLength;
}

std::shared_ptr<xivres::texture::stream> xivres::texture::mipmap_stream::to_single_texture_stream() {
	auto res = std::make_shared<texture::stream>(Type, Width, Height);
	res->set_mipmap(0, 0, std::dynamic_pointer_cast<mipmap_stream>(this->shared_from_this()));
	return res;
}

xivres::texture::wrapped_mipmap_stream::wrapped_mipmap_stream(const header& header, size_t mipmapIndex, std::shared_ptr<const stream> underlying)
	: mipmap_stream(
		(std::max)(1, header.Width >> mipmapIndex),
		(std::max)(1, header.Height >> mipmapIndex),
		(std::max)(1, header.Depth >> mipmapIndex),
		header.Type)
	, m_underlying(std::move(underlying)) {}

xivres::texture::wrapped_mipmap_stream::wrapped_mipmap_stream(size_t width, size_t height, size_t depths, format_type type, std::shared_ptr<const stream> underlying)
	: mipmap_stream(width, height, depths, type)
	, m_underlying(std::move(underlying)) {}

std::streamsize xivres::texture::wrapped_mipmap_stream::read(std::streamoff offset, void* buf, std::streamsize length) const {
	if (offset + length > SupposedMipmapLength)
		length = SupposedMipmapLength - offset;

	const auto read = m_underlying->read(offset, buf, length);
	std::fill_n(static_cast<char*>(buf) + read, length - read, 0);
	return length;
}

xivres::texture::memory_mipmap_stream::memory_mipmap_stream(size_t width, size_t height, size_t depths, format_type type)
	: mipmap_stream(width, height, depths, type)
	, m_data(SupposedMipmapLength) {

}

xivres::texture::memory_mipmap_stream::memory_mipmap_stream(size_t width, size_t height, size_t depths, format_type type, std::vector<uint8_t> data)
	: mipmap_stream(width, height, depths, type)
	, m_data(std::move(data)) {
	m_data.resize(SupposedMipmapLength);
}

std::streamsize xivres::texture::memory_mipmap_stream::read(std::streamoff offset, void* buf, std::streamsize length) const {
	const auto available = (std::min)(static_cast<std::streamsize>(m_data.size() - offset), length);
	std::copy_n(&m_data[static_cast<size_t>(offset)], available, static_cast<char*>(buf));
	return available;
}

std::shared_ptr<xivres::texture::memory_mipmap_stream> xivres::texture::memory_mipmap_stream::as_argb8888(const mipmap_stream& strm) {
	const auto pixelCount = calc_raw_data_length(formats::L8, strm.Width, strm.Height, strm.Depth);
	const auto cbSource = (std::min)(static_cast<size_t>(strm.size()), calc_raw_data_length(strm.Type, strm.Width, strm.Height, strm.Depth));

	std::vector<uint8_t> result(pixelCount * sizeof(util::b8g8r8a8));
	const auto b8g8r8a8view = util::span_cast<util::b8g8r8a8>(result);
	uint32_t pos = 0, read = 0;
	uint8_t buf8[8192];
	switch (strm.Type) {
		case formats::L8:
		{
			if (cbSource < pixelCount)
				throw std::runtime_error("Truncated data detected");
			while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
				strm.read_fully(read, buf8, len);
				read += len;
				for (size_t i = 0; i < len; ++pos, ++i)
					b8g8r8a8view[pos] = util::b8g8r8a8(buf8[i], buf8[i], buf8[i], 255);
			}
			break;
		}

		case formats::A8:
		{
			if (cbSource < pixelCount)
				throw std::runtime_error("Truncated data detected");
			while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
				strm.read_fully(read, buf8, len);
				read += len;
				for (size_t i = 0; i < len; ++pos, ++i)
					b8g8r8a8view[pos] = util::b8g8r8a8(255, 255, 255, buf8[i]);
			}
			break;
		}

		case formats::B4G4R4A4:
		{
			if (cbSource < pixelCount * sizeof(util::b4g4r4a4))
				throw std::runtime_error("Truncated data detected");
			const auto view = util::span_cast<util::b4g4r4a4>(buf8);
			while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
				strm.read_fully(read, buf8, len);
				read += len;
				for (size_t i = 0, count = len / sizeof(util::b4g4r4a4); i < count; ++pos, ++i)
					b8g8r8a8view[pos].set_components_from(view[i]);
			}
			break;
		}

		case formats::B5G5R5A1:
		{
			if (cbSource < pixelCount * sizeof(util::b5g5r5a1))
				throw std::runtime_error("Truncated data detected");
			const auto view = util::span_cast<util::b5g5r5a1>(buf8);
			while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
				strm.read_fully(read, buf8, len);
				read += len;
				for (size_t i = 0, count = len / sizeof(util::b5g5r5a1); i < count; ++pos, ++i)
					b8g8r8a8view[pos].set_components_from(view[i]);
			}
			break;
		}

		case formats::B8G8R8A8:
			if (cbSource < pixelCount * sizeof(util::b8g8r8a8))
				throw std::runtime_error("Truncated data detected");
			strm.read_fully(0, std::span(b8g8r8a8view));
			break;

		case formats::B8G8R8X8:
			if (cbSource < pixelCount * sizeof(util::b8g8r8a8))
				throw std::runtime_error("Truncated data detected");
			strm.read_fully(0, std::span(b8g8r8a8view));
			for (auto& item : b8g8r8a8view) {
				item = util::b8g8r8a8(item.R, item.G, item.B, 0xFF);
			}
			break;

		case formats::R16G16B16A16F:
		{
			if (cbSource < pixelCount * sizeof(util::r16g16b16a16f))
				throw std::runtime_error("Truncated data detected");
			const auto view = util::span_cast<util::r16g16b16a16f>(buf8);
			while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
				strm.read_fully(read, buf8, len);
				read += len;
				for (size_t i = 0, count = len / sizeof(util::r16g16b16a16f); i < count; ++pos, ++i)
					b8g8r8a8view[pos].set_components_from(view[i]);
			}
			break;
		}

		case formats::R32G32B32A32F:
		{
			if (cbSource < pixelCount * sizeof(util::r32g32b32a32f))
				throw std::runtime_error("Truncated data detected");
			const auto view = util::span_cast<util::r32g32b32a32f>(buf8);
			while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
				strm.read_fully(read, buf8, len);
				read += len;
				for (size_t i = 0, count = len / sizeof(util::r32g32b32a32f); i < count; ++pos, ++i)
					b8g8r8a8view[pos].set_components_from(view[i]);
			}
			break;
		}

		case formats::DXT1:
		{
			if (cbSource * 2 < pixelCount)
				throw std::runtime_error("Truncated data detected");
			while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
				strm.read_fully(read, buf8, len);
				read += len;
				for (size_t i = 0, count = len; i < count; i += 8, pos += 8) {
					DecompressBlockDXT1(
						pos / 2 % strm.Width,
						pos / 2 / strm.Width * 4,
						strm.Width, &buf8[i], b8g8r8a8view.data());
				}
			}
			break;
		}

		case formats::DXT3:
		{
			if (cbSource * 4 < pixelCount)
				throw std::runtime_error("Truncated data detected");
			while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
				strm.read_fully(read, buf8, len);
				read += len;
				for (size_t i = 0, count = len; i < count; i += 16, pos += 16) {
					const auto x = pos / 4 % strm.Width;
					const auto y = pos / 4 / strm.Width * 4;
					DecompressBlockDXT1(
						static_cast<uint32_t>(x),
						static_cast<uint32_t>(y),
						strm.Width, &buf8[i + 8], b8g8r8a8view.data());
					for (size_t dy = 0; dy < 4; dy += 1) {
						for (size_t dx = 0; dx < 4; dx += 2) {
							auto& native1 = b8g8r8a8view[(y + dy) * strm.Width + x + dx];
							native1 = util::b8g8r8a8(native1.R, native1.G, native1.B, 17 * (buf8[i + dy * 2 + dx / 2] & 0xF));

							auto& native2 = b8g8r8a8view[(y + dy) * strm.Width + x + dx + 1];
							native2 = util::b8g8r8a8(native2.R, native2.G, native2.B, 17 * (buf8[i + dy * 2 + dx / 2] >> 4));
						}
					}
				}
			}
			break;
		}

		case formats::DXT5:
		{
			if (cbSource * 4 < pixelCount)
				throw std::runtime_error("Truncated data detected");
			while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
				strm.read_fully(read, buf8, len);
				read += len;
				for (size_t i = 0, count = len; i < count; i += 16, pos += 16) {
					DecompressBlockDXT5(
						pos / 4 % strm.Width,
						pos / 4 / strm.Width * 4,
						strm.Width, &buf8[i], b8g8r8a8view.data());
				}
			}
			break;
		}

		case formats::Unknown:
		default:
			throw std::runtime_error("Unsupported type");
	}

	return std::make_shared<memory_mipmap_stream>(strm.Width, strm.Height, strm.Depth, formats::B8G8R8A8, std::move(result));
}

std::shared_ptr<const xivres::texture::mipmap_stream> xivres::texture::memory_mipmap_stream::as_argb8888_view(std::shared_ptr<const mipmap_stream> strm) {
	if (strm->Type == formats::B8G8R8A8)
		return std::make_shared<wrapped_mipmap_stream>(strm->Width, strm->Height, strm->Depth, strm->Type, std::move(strm));

	return as_argb8888(*strm);
}
