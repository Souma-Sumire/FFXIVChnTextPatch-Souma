#include "../include/xivres/util.bitmap_copy.h"

std::vector<uint8_t> xivres::util::bitmap_copy::create_gamma_table(float gamma) {
	std::vector<uint8_t> res(256);
	for (int i = 0; i < 256; i++)
		res[i] = static_cast<uint8_t>(std::powf(static_cast<float>(i) / 255.f, 1 / gamma) * 255.f);
	return res;
}

void xivres::util::bitmap_copy::to_b8g8r8a8::draw_line_to_rgb_opaque(b8g8r8a8* pTarget, const uint8_t* pSource, size_t nPixelCount) const {
	while (nPixelCount--) {
		const auto opacityScaled = m_gammaTable[*pSource];
		pTarget->R = (m_colorBackground.R * (255 - opacityScaled) + m_colorForeground.R * opacityScaled) / 255;
		pTarget->G = (m_colorBackground.G * (255 - opacityScaled) + m_colorForeground.G * opacityScaled) / 255;
		pTarget->B = (m_colorBackground.B * (255 - opacityScaled) + m_colorForeground.B * opacityScaled) / 255;
		pTarget->A = 255;
		++pTarget;
		pSource += m_nSourceStride;
	}
}

void xivres::util::bitmap_copy::to_b8g8r8a8::draw_line_to_rgb(b8g8r8a8* pTarget, const uint8_t* pSource, size_t nPixelCount) const {
	while (nPixelCount--) {
		const auto opacityScaled = m_gammaTable[*pSource];
		const auto blendedBgColor = b8g8r8a8{
			(m_colorBackground.R * m_colorBackground.A + pTarget->R * (255 - m_colorBackground.A)) / 255,
			(m_colorBackground.G * m_colorBackground.A + pTarget->G * (255 - m_colorBackground.A)) / 255,
			(m_colorBackground.B * m_colorBackground.A + pTarget->B * (255 - m_colorBackground.A)) / 255,
			255 - (255 - m_colorBackground.A) * (255 - pTarget->A) / 255,
		};
		const auto blendedFgColor = b8g8r8a8{
			(m_colorForeground.R * m_colorForeground.A + pTarget->R * (255 - m_colorForeground.A)) / 255,
			(m_colorForeground.G * m_colorForeground.A + pTarget->G * (255 - m_colorForeground.A)) / 255,
			(m_colorForeground.B * m_colorForeground.A + pTarget->B * (255 - m_colorForeground.A)) / 255,
			255 - (255 - m_colorForeground.A) * (255 - pTarget->A) / 255,
		};
		const auto currentColor = b8g8r8a8{
			(blendedBgColor.R * (255 - opacityScaled) + blendedFgColor.R * opacityScaled) / 255,
			(blendedBgColor.G * (255 - opacityScaled) + blendedFgColor.G * opacityScaled) / 255,
			(blendedBgColor.B * (255 - opacityScaled) + blendedFgColor.B * opacityScaled) / 255,
			(blendedBgColor.A * (255 - opacityScaled) + blendedFgColor.A * opacityScaled) / 255,
		};
		const auto blendedDestColor = b8g8r8a8{
			(pTarget->R * pTarget->A + currentColor.R * (255 - pTarget->A)) / 255,
			(pTarget->G * pTarget->A + currentColor.G * (255 - pTarget->A)) / 255,
			(pTarget->B * pTarget->A + currentColor.B * (255 - pTarget->A)) / 255,
			255 - (255 - pTarget->A) * (255 - currentColor.A) / 255,
		};
		pTarget->R = (blendedDestColor.R * (255 - currentColor.A) + currentColor.R * currentColor.A) / 255;
		pTarget->G = (blendedDestColor.G * (255 - currentColor.A) + currentColor.G * currentColor.A) / 255;
		pTarget->B = (blendedDestColor.B * (255 - currentColor.A) + currentColor.B * currentColor.A) / 255;
		pTarget->A = blendedDestColor.A;
		++pTarget;
		pSource += m_nSourceStride;
	}
}

void xivres::util::bitmap_copy::to_b8g8r8a8::copy(int srcX1, int srcY1, int srcX2, int srcY2, int targetX1, int targetY1) {
	auto destPtrBegin = &m_pTarget[(m_nTargetVerticalDirection == bitmap_vertical_direction::TopRowFirst ? targetY1 : m_nTargetHeight - targetY1 - 1) * m_nTargetWidth + targetX1];
	const auto destPtrDelta = m_nTargetWidth * static_cast<int>(m_nTargetVerticalDirection);

	auto srcPtrBegin = &m_pSource[m_nSourceStride * ((m_nSourceVerticalDirection == bitmap_vertical_direction::TopRowFirst ? srcY1 : m_nSourceHeight - srcY1 - 1) * m_nSourceWidth + srcX1)];
	const auto srcPtrDelta = m_nSourceStride * m_nSourceWidth * static_cast<int>(m_nSourceVerticalDirection);

	const auto nCols = srcX2 - srcX1;
	auto nRemainingRows = srcY2 - srcY1;

	if (m_colorForeground.A == 255 && m_colorBackground.A == 255) {
		while (nRemainingRows--) {
			draw_line_to_rgb_opaque(destPtrBegin, srcPtrBegin, nCols);
			destPtrBegin += destPtrDelta;
			srcPtrBegin += srcPtrDelta;
		}

	} else if (m_colorForeground.A == 255 && m_colorBackground.A == 0) {
		while (nRemainingRows--) {
			draw_line_to_rgb_binary_opacity<true>(destPtrBegin, srcPtrBegin, nCols);
			destPtrBegin += destPtrDelta;
			srcPtrBegin += srcPtrDelta;
		}

	} else if (m_colorForeground.A == 0 && m_colorBackground.A == 255) {
		while (nRemainingRows--) {
			draw_line_to_rgb_binary_opacity<false>(destPtrBegin, srcPtrBegin, nCols);
			destPtrBegin += destPtrDelta;
			srcPtrBegin += srcPtrDelta;
		}

	} else if (m_colorForeground.A != 0 || m_colorBackground.A != 0) {
		while (nRemainingRows--) {
			draw_line_to_rgb(destPtrBegin, srcPtrBegin, nCols);
			destPtrBegin += destPtrDelta;
			srcPtrBegin += srcPtrDelta;
		}
	}
}

xivres::util::bitmap_copy::to_b8g8r8a8& xivres::util::bitmap_copy::to_b8g8r8a8::back_color(b8g8r8a8 color) {
	m_colorBackground = color;
	return *this;
}

xivres::util::bitmap_copy::to_b8g8r8a8& xivres::util::bitmap_copy::to_b8g8r8a8::fore_color(b8g8r8a8 color) {
	m_colorForeground = color;
	return *this;
}

xivres::util::bitmap_copy::to_b8g8r8a8& xivres::util::bitmap_copy::to_b8g8r8a8::gamma_table(std::span<const uint8_t> gammaTable) {
	m_gammaTable = gammaTable;
	return *this;
}

xivres::util::bitmap_copy::to_b8g8r8a8& xivres::util::bitmap_copy::to_b8g8r8a8::to(b8g8r8a8* pBuf, size_t width, size_t height, bitmap_vertical_direction verticalDirection) {
	m_pTarget = pBuf;
	m_nTargetWidth = width;
	m_nTargetHeight = height;
	m_nTargetVerticalDirection = verticalDirection;
	return *this;
}

xivres::util::bitmap_copy::to_b8g8r8a8& xivres::util::bitmap_copy::to_b8g8r8a8::from(const void* pBuf, size_t width, size_t height, size_t stride, bitmap_vertical_direction verticalDirection) {
	m_pSource = static_cast<const uint8_t*>(pBuf);
	m_nSourceWidth = width;
	m_nSourceHeight = height;
	m_nSourceStride = stride;
	m_nSourceVerticalDirection = verticalDirection;
	return *this;
}

void xivres::util::bitmap_copy::to_l8::draw_line_to_l8_opaque(uint8_t* pTarget, const uint8_t* pSource, size_t regionWidth) const {
	while (regionWidth--) {
		*pTarget = m_gammaTable[*pSource];
		pTarget += m_nTargetStride;
		pSource += m_nSourceStride;
	}
}

void xivres::util::bitmap_copy::to_l8::draw_line_to_l8(uint8_t* pTarget, const uint8_t* pSource, size_t regionWidth) const {
	while (regionWidth--) {
		const auto opacityScaled = m_gammaTable[*pSource];
		const auto blendedBgColor = (1 * m_colorBackground * m_opacityBackground + 1 * *pTarget * (255 - m_opacityBackground)) / 255;
		const auto blendedFgColor = (1 * m_colorForeground * m_opacityForeground + 1 * *pTarget * (255 - m_opacityForeground)) / 255;
		*pTarget = static_cast<uint8_t>((blendedBgColor * (255 - opacityScaled) + blendedFgColor * opacityScaled) / 255);
		pTarget += m_nTargetStride;
		pSource += m_nSourceStride;
	}
}

void xivres::util::bitmap_copy::to_l8::copy(int srcX1, int srcY1, int srcX2, int srcY2, int targetX1, int targetY1) {
	auto destPtrBegin = &m_pTarget[m_nTargetStride * ((m_nTargetVerticalDirection == bitmap_vertical_direction::TopRowFirst ? targetY1 : m_nTargetHeight - targetY1 - 1) * m_nTargetWidth + targetX1)];
	const auto destPtrDelta = m_nTargetStride * m_nTargetWidth * static_cast<int>(m_nTargetVerticalDirection);

	auto srcPtrBegin = &m_pSource[m_nSourceStride * ((m_nSourceVerticalDirection == bitmap_vertical_direction::TopRowFirst ? srcY1 : m_nSourceHeight - srcY1 - 1) * m_nSourceWidth + srcX1)];
	const auto srcPtrDelta = m_nSourceStride * m_nSourceWidth * static_cast<int>(m_nSourceVerticalDirection);

	const auto nCols = srcX2 - srcX1;
	auto nRemainingRows = srcY2 - srcY1;

	if (m_opacityForeground == 255 && m_opacityBackground == 255) {
		while (nRemainingRows--) {
			draw_line_to_l8_opaque(destPtrBegin, srcPtrBegin, nCols);
			destPtrBegin += destPtrDelta;
			srcPtrBegin += srcPtrDelta;
		}

	} else if (m_opacityForeground == 255 && m_opacityBackground == 0) {
		while (nRemainingRows--) {
			draw_line_to_l8_binary_opacity<true>(destPtrBegin, srcPtrBegin, nCols);
			destPtrBegin += destPtrDelta;
			srcPtrBegin += srcPtrDelta;
		}

	} else if (m_opacityForeground == 0 && m_opacityBackground == 255) {
		while (nRemainingRows--) {
			draw_line_to_l8_binary_opacity<false>(destPtrBegin, srcPtrBegin, nCols);
			destPtrBegin += destPtrDelta;
			srcPtrBegin += srcPtrDelta;
		}

	} else if (m_opacityForeground != 0 || m_opacityBackground != 0) {
		while (nRemainingRows--) {
			draw_line_to_l8(destPtrBegin, srcPtrBegin, nCols);
			destPtrBegin += destPtrDelta;
			srcPtrBegin += srcPtrDelta;
		}
	}
}

xivres::util::bitmap_copy::to_l8& xivres::util::bitmap_copy::to_l8::back_opacity(uint8_t opacity) {
	m_opacityBackground = opacity;
	return *this;
}

xivres::util::bitmap_copy::to_l8& xivres::util::bitmap_copy::to_l8::fore_opacity(uint8_t opacity) {
	m_opacityForeground = opacity;
	return *this;
}

xivres::util::bitmap_copy::to_l8& xivres::util::bitmap_copy::to_l8::back_color(uint8_t color) {
	m_colorBackground = color;
	return *this;
}

xivres::util::bitmap_copy::to_l8& xivres::util::bitmap_copy::to_l8::fore_color(uint8_t color) {
	m_colorForeground = color;
	return *this;
}

xivres::util::bitmap_copy::to_l8& xivres::util::bitmap_copy::to_l8::gamma_table(std::span<const uint8_t> gammaTable) {
	m_gammaTable = gammaTable;
	return *this;
}

xivres::util::bitmap_copy::to_l8& xivres::util::bitmap_copy::to_l8::to(uint8_t* pBuf, size_t width, size_t height, size_t stride, bitmap_vertical_direction verticalDirection) {
	m_pTarget = pBuf;
	m_nTargetWidth = width;
	m_nTargetHeight = height;
	m_nTargetStride = stride;
	m_nTargetVerticalDirection = verticalDirection;
	return *this;
}

xivres::util::bitmap_copy::to_l8& xivres::util::bitmap_copy::to_l8::from(const void* pBuf, size_t width, size_t height, size_t stride, bitmap_vertical_direction verticalDirection) {
	m_pSource = static_cast<const uint8_t*>(pBuf);
	m_nSourceWidth = width;
	m_nSourceHeight = height;
	m_nSourceStride = stride;
	m_nSourceVerticalDirection = verticalDirection;
	return *this;
}
