#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "xivres/texture.mipmap_stream.h"
#include "xivres/util.pixel_formats.h"

namespace App::WicImage {
	// Encodes 32-bit BGRA pixels, with straight alpha, as a PNG file.
	[[nodiscard]] std::vector<uint8_t> EncodePng(int width, int height, std::span<const xivres::util::b8g8r8a8> pixels);

	void SavePng(int width, int height, std::span<const xivres::util::b8g8r8a8> pixels, const std::filesystem::path& path);

	void SavePng(const xivres::texture::memory_mipmap_stream& mipmap, const std::filesystem::path& path);
}
