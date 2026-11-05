#include "../include/xivres/texture.h"

#include <stdexcept>

size_t xivres::texture::calc_raw_data_length(format_type type, size_t width, size_t height, size_t depth, size_t mipmapIndex /*= 0*/) {
	width = (std::max<size_t>)(1, width >> mipmapIndex);
	height = (std::max<size_t>)(1, height >> mipmapIndex);
	depth = (std::max<size_t>)(1, depth >> mipmapIndex);
	switch (type) {
		case formats::L8:
		case formats::A8:
			return width * height * depth;

		case formats::B4G4R4A4:
		case formats::B5G5R5A1:
			return width * height * depth * 2;

		case formats::B8G8R8A8:
		case formats::B8G8R8X8:
		case formats::R32F:
		case formats::R16G16F:
			return width * height * depth * 4;

		case formats::R16G16B16A16F:
		case formats::R32G32F:
			return width * height * depth * 8;

		case formats::R32G32B32A32F:
			return width * height * depth * 16;

		case formats::BC1:
			return depth * std::max<size_t>(1, (width + 3) / 4) * std::max<size_t>(1, (height + 3) / 4) * 8;

		case formats::BC2:
		case formats::BC3:
		case formats::BC5:
		case formats::BC7:
			return depth * std::max<size_t>(1, (width + 3) / 4) * std::max<size_t>(1, (height + 3) / 4) * 16;

		case formats::D16:
		case formats::Unknown:
		default:
			throw std::invalid_argument("Unsupported type");
	}
}

size_t xivres::texture::calc_raw_data_length(const header& header, size_t mipmapIndex /*= 0*/) {
	return calc_raw_data_length(header.Type, header.Width, header.Height, header.Depth, mipmapIndex);
}
