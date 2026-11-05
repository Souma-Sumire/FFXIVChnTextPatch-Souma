#include "../include/xivres/util.dxt.h"

void xivres::util::DecompressBlockDXT1(uint32_t x, uint32_t y, uint32_t width, const uint8_t* blockStorage, b8g8r8a8* image) {
	const uint16_t color0 = *reinterpret_cast<const uint16_t*>(blockStorage);
	const uint16_t color1 = *reinterpret_cast<const uint16_t*>(blockStorage + 2);

	uint32_t temp = (color0 >> 11) * 255U + 16U;
	const auto r0 = static_cast<uint8_t>((temp / 32 + temp) / 32);
	temp = ((color0 & 0x07E0U) >> 5) * 255U + 32U;
	const auto g0 = static_cast<uint8_t>((temp / 64 + temp) / 64);
	temp = (color0 & 0x001FU) * 255U + 16U;
	const auto b0 = static_cast<uint8_t>((temp / 32 + temp) / 32);

	temp = (color1 >> 11) * 255U + 16U;
	const auto r1 = static_cast<uint8_t>((temp / 32 + temp) / 32);
	temp = ((color1 & 0x07E0U) >> 5) * 255U + 32U;
	const auto g1 = static_cast<uint8_t>((temp / 64 + temp) / 64);
	temp = (color1 & 0x001FU) * 255U + 16U;
	const auto b1 = static_cast<uint8_t>((temp / 32 + temp) / 32);

	const uint32_t code = *reinterpret_cast<const uint32_t*>(blockStorage + 4);

	for (int j = 0; j < 4; j++) {
		for (int i = 0; i < 4; i++) {
			b8g8r8a8 finalColor;
			const auto positionCode = static_cast<uint8_t>((code >> 2 * (4 * j + i)) & 0x03);

			if (color0 > color1) {
				switch (positionCode) {
					case 0:
						finalColor = b8g8r8a8(r0, g0, b0, 255);
						break;
					case 1:
						finalColor = b8g8r8a8(r1, g1, b1, 255);
						break;
					case 2:
						finalColor = b8g8r8a8((2 * r0 + r1) / 3, (2 * g0 + g1) / 3, (2 * b0 + b1) / 3, 255);
						break;
					case 3:
					default:
						finalColor = b8g8r8a8((r0 + 2 * r1) / 3, (g0 + 2 * g1) / 3, (b0 + 2 * b1) / 3, 255);
						break;
				}
			} else {
				switch (positionCode) {
					case 0:
						finalColor = b8g8r8a8(r0, g0, b0, 255);
						break;
					case 1:
						finalColor = b8g8r8a8(r1, g1, b1, 255);
						break;
					case 2:
						finalColor = b8g8r8a8((r0 + r1) / 2, (g0 + g1) / 2, (b0 + b1) / 2, 255);
						break;
					case 3:
					default:
						finalColor = b8g8r8a8(0, 0, 0, 255);
						break;
				}
			}

			if (x + i < width)
				image[(y + j) * width + (x + i)] = finalColor;
		}
	}
}

void xivres::util::BlockDecompressImageDXT1(uint32_t width, uint32_t height, const uint8_t* blockStorage, b8g8r8a8* image) {
	const uint32_t blockCountX = (width + 3) / 4;
	const uint32_t blockCountY = (height + 3) / 4;

	for (uint32_t j = 0; j < blockCountY; j++) {
		for (uint32_t i = 0; i < blockCountX; i++) DecompressBlockDXT1(i * 4, j * 4, width, blockStorage + static_cast<size_t>(i) * 8, image);
		blockStorage += static_cast<size_t>(blockCountX) * 8;
	}
}

void xivres::util::DecompressBlockDXT5(uint32_t x, uint32_t y, uint32_t width, const uint8_t* blockStorage, b8g8r8a8* image) {
	const uint8_t alpha0 = blockStorage[0];
	const uint8_t alpha1 = blockStorage[1];

	const uint8_t* bits = blockStorage + 2;
	const uint32_t alphaCode1 = static_cast<uint32_t>(bits[2]) | (static_cast<uint32_t>(bits[3]) << 8) | (static_cast<uint32_t>(bits[4]) << 16) | (static_cast<uint32_t>(bits[5]) << 24);
	const auto alphaCode2 = static_cast<uint16_t>(bits[0] | (bits[1] << 8));

	const uint16_t color0 = *reinterpret_cast<const uint16_t*>(blockStorage + 8);
	const uint16_t color1 = *reinterpret_cast<const uint16_t*>(blockStorage + 10);

	uint32_t temp = (color0 >> 11) * 255U + 16U;
	const auto r0 = static_cast<uint8_t>((temp / 32 + temp) / 32);
	temp = ((color0 & 0x07E0U) >> 5) * 255U + 32U;
	const auto g0 = static_cast<uint8_t>((temp / 64 + temp) / 64);
	temp = (color0 & 0x001FU) * 255U + 16U;
	const auto b0 = static_cast<uint8_t>((temp / 32 + temp) / 32);

	temp = (color1 >> 11) * 255U + 16U;
	const auto r1 = static_cast<uint8_t>((temp / 32 + temp) / 32);
	temp = ((color1 & 0x07E0U) >> 5) * 255U + 32U;
	const auto g1 = static_cast<uint8_t>((temp / 64 + temp) / 64);
	temp = (color1 & 0x001FU) * 255U + 16U;
	const auto b1 = static_cast<uint8_t>((temp / 32 + temp) / 32);

	const uint32_t code = *reinterpret_cast<const uint32_t*>(blockStorage + 12);

	for (int j = 0; j < 4; j++) {
		for (int i = 0; i < 4; i++) {
			const int alphaCodeIndex = 3 * (4 * j + i);
			int alphaCode;

			if (alphaCodeIndex <= 12) {
				alphaCode = (alphaCode2 >> alphaCodeIndex) & 0x07;
			} else if (alphaCodeIndex == 15) {
				alphaCode = static_cast<int>((alphaCode2 >> 15) | ((alphaCode1 << 1) & 0x06));
			} else // alphaCodeIndex >= 18 && alphaCodeIndex <= 45
			{
				alphaCode = static_cast<int>((alphaCode1 >> (alphaCodeIndex - 16)) & 0x07);
			}

			uint8_t finalAlpha;
			if (alphaCode == 0) {
				finalAlpha = alpha0;
			} else if (alphaCode == 1) {
				finalAlpha = alpha1;
			} else {
				if (alpha0 > alpha1) {
					finalAlpha = static_cast<uint8_t>(((8 - alphaCode) * alpha0 + (alphaCode - 1) * alpha1) / 7);
				} else {
					if (alphaCode == 6)
						finalAlpha = 0;
					else if (alphaCode == 7)
						finalAlpha = 255;
					else
						finalAlpha = static_cast<uint8_t>(((6 - alphaCode) * alpha0 + (alphaCode - 1) * alpha1) / 5);
				}
			}

			const auto colorCode = static_cast<uint8_t>((code >> 2 * (4 * j + i)) & 0x03);

			b8g8r8a8 finalColor;
			switch (colorCode) {
				case 0:
					finalColor = b8g8r8a8(r0, g0, b0, finalAlpha);
					break;
				case 1:
					finalColor = b8g8r8a8(r1, g1, b1, finalAlpha);
					break;
				case 2:
					finalColor = b8g8r8a8((2 * r0 + r1) / 3, (2 * g0 + g1) / 3, (2 * b0 + b1) / 3, finalAlpha);
					break;
				case 3:
				default:
					finalColor = b8g8r8a8((r0 + 2 * r1) / 3, (g0 + 2 * g1) / 3, (b0 + 2 * b1) / 3, finalAlpha);
					break;
			}

			if (x + i < width)
				image[(y + j) * width + (x + i)] = finalColor;
		}
	}
}

void xivres::util::BlockDecompressImageDXT5(uint32_t width, uint32_t height, const uint8_t* blockStorage, b8g8r8a8* image) {
	const uint32_t blockCountX = (width + 3) / 4;
	const uint32_t blockCountY = (height + 3) / 4;

	for (uint32_t j = 0; j < blockCountY; j++) {
		for (uint32_t i = 0; i < blockCountX; i++) DecompressBlockDXT5(i * 4, j * 4, width, blockStorage + static_cast<size_t>(i) * 16, image);
		blockStorage += static_cast<size_t>(blockCountX) * 16;
	}
}
