#ifndef XIVRES_INTERNAL_DXT_H_
#define XIVRES_INTERNAL_DXT_H_

#include "util.pixel_formats.h"

// From https://github.com/Benjamin-Dobell/s3tc-dxt-decompression/blob/master/s3tc.h
#pragma warning(push, 0)
namespace xivres::util {
	// void DecompressBlockDXT1(): Decompresses one block of a DXT1 texture and stores the resulting pixels at the appropriate offset in 'image'.
	//
	// uint32_t x:                     x-coordinate of the first pixel in the block.
	// uint32_t y:                     y-coordinate of the first pixel in the block.
	// uint32_t width:                 width of the texture being decompressed.
	// uint32_t height:                height of the texture being decompressed.
	// const uint8_t *blockStorage:   pointer to the block to decompress.
	// uint32_t *image:                pointer to image where the decompressed pixel data should be stored.
	void DecompressBlockDXT1(uint32_t x, uint32_t y, uint32_t width, const uint8_t* blockStorage, b8g8r8a8* image);

	// void BlockDecompressImageDXT1(): Decompresses all the blocks of a DXT1 compressed texture and stores the resulting pixels in 'image'.
	//
	// uint32_t width:                 Texture width.
	// uint32_t height:                Texture height.
	// const uint8_t *blockStorage:   pointer to compressed DXT1 blocks.
	// uint32_t *image:                pointer to the image where the decompressed pixels will be stored.
	void BlockDecompressImageDXT1(uint32_t width, uint32_t height, const uint8_t* blockStorage, b8g8r8a8* image);

	// void DecompressBlockDXT5(): Decompresses one block of a DXT5 texture and stores the resulting pixels at the appropriate offset in 'image'.
	//
	// uint32_t x:                     x-coordinate of the first pixel in the block.
	// uint32_t y:                     y-coordinate of the first pixel in the block.
	// uint32_t width:                 width of the texture being decompressed.
	// uint32_t height:                height of the texture being decompressed.
	// const uint8_t *blockStorage:   pointer to the block to decompress.
	// uint32_t *image:                pointer to image where the decompressed pixel data should be stored.
	void DecompressBlockDXT5(uint32_t x, uint32_t y, uint32_t width, const uint8_t* blockStorage, b8g8r8a8* image);

	// void BlockDecompressImageDXT5(): Decompresses all the blocks of a DXT5 compressed texture and stores the resulting pixels in 'image'.
	//
	// uint32_t width:                 Texture width.
	// uint32_t height:                Texture height.
	// const uint8_t *blockStorage:   pointer to compressed DXT5 blocks.
	// uint32_t *image:                pointer to the image where the decompressed pixels will be stored.
	void BlockDecompressImageDXT5(uint32_t width, uint32_t height, const uint8_t* blockStorage, b8g8r8a8* image);
}
#pragma warning(pop)

#endif
