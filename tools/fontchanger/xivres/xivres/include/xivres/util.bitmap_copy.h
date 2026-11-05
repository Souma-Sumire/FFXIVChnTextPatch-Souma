#ifndef XIVRES_INTERNAL_BitmapCopy_H_
#define XIVRES_INTERNAL_BitmapCopy_H_

#include <cinttypes>
#include <span>
#include <vector>

#include "util.pixel_formats.h"

namespace xivres::util {
	enum class bitmap_vertical_direction : int {
		TopRowFirst = 1,
		Undefined = 0,
		BottomRowFirst = -1,
	};

	namespace bitmap_copy {
		std::vector<uint8_t> create_gamma_table(float gamma);

		class to_b8g8r8a8 {
			std::span<const uint8_t> m_gammaTable;

			b8g8r8a8 m_colorForeground = b8g8r8a8(0, 0, 0, 255);
			b8g8r8a8 m_colorBackground = b8g8r8a8(0, 0, 0, 0);

			const uint8_t* m_pSource = nullptr;
			size_t m_nSourceWidth = 0;
			size_t m_nSourceHeight = 0;
			size_t m_nSourceStride = 0;
			bitmap_vertical_direction m_nSourceVerticalDirection = bitmap_vertical_direction::Undefined;

			b8g8r8a8* m_pTarget = nullptr;
			size_t m_nTargetWidth = 0;
			size_t m_nTargetHeight = 0;
			bitmap_vertical_direction m_nTargetVerticalDirection = bitmap_vertical_direction::Undefined;

		public:
			to_b8g8r8a8& from(const void* pBuf, size_t width, size_t height, size_t stride, bitmap_vertical_direction verticalDirection);

			to_b8g8r8a8& to(b8g8r8a8* pBuf, size_t width, size_t height, bitmap_vertical_direction verticalDirection);

			to_b8g8r8a8& gamma_table(std::span<const uint8_t> gammaTable);

			to_b8g8r8a8& fore_color(b8g8r8a8 color);

			to_b8g8r8a8& back_color(b8g8r8a8 color);

			void copy(int srcX1, int srcY1, int srcX2, int srcY2, int targetX1, int targetY1);

		private:
			void draw_line_to_rgb(b8g8r8a8* pTarget, const uint8_t* pSource, size_t nPixelCount) const;

			void draw_line_to_rgb_opaque(b8g8r8a8* pTarget, const uint8_t* pSource, size_t nPixelCount) const;

			template<bool ColorIsForeground>
			void draw_line_to_rgb_binary_opacity(b8g8r8a8* pTarget, const uint8_t* pSource, size_t nPixelCount) const {
				const auto color = ColorIsForeground ? m_colorForeground : m_colorBackground;
				while (nPixelCount--) {
					const auto opacityScaled = m_gammaTable[*pSource];
					if (const auto opacity = 255 * (ColorIsForeground ? opacityScaled : 255 - opacityScaled) / 255) {
						const auto blendedDestColor = b8g8r8a8{
							(pTarget->R * pTarget->A + color.R * (255 - pTarget->A)) / 255,
							(pTarget->G * pTarget->A + color.G * (255 - pTarget->A)) / 255,
							(pTarget->B * pTarget->A + color.B * (255 - pTarget->A)) / 255,
							255 - (255 - pTarget->A) * (255 - opacity) / 255,
						};
						pTarget->R = (blendedDestColor.R * (255 - opacity) + color.R * opacity) / 255;
						pTarget->G = (blendedDestColor.G * (255 - opacity) + color.G * opacity) / 255;
						pTarget->B = (blendedDestColor.B * (255 - opacity) + color.B * opacity) / 255;
						pTarget->A = blendedDestColor.A;
					}
					++pTarget;
					pSource += m_nSourceStride;
				}
			}
		};

		class to_l8 {
			std::span<const uint8_t> m_gammaTable;

			uint8_t m_colorForeground = 255;
			uint8_t m_colorBackground = 0;
			uint8_t m_opacityForeground = 255;
			uint8_t m_opacityBackground = 0;

			const uint8_t* m_pSource = nullptr;
			size_t m_nSourceWidth = 0;
			size_t m_nSourceHeight = 0;
			size_t m_nSourceStride = 0;
			bitmap_vertical_direction m_nSourceVerticalDirection = bitmap_vertical_direction::Undefined;

			uint8_t* m_pTarget = nullptr;
			size_t m_nTargetWidth = 0;
			size_t m_nTargetHeight = 0;
			size_t m_nTargetStride = 0;
			bitmap_vertical_direction m_nTargetVerticalDirection = bitmap_vertical_direction::Undefined;

		public:
			to_l8& from(const void* pBuf, size_t width, size_t height, size_t stride, bitmap_vertical_direction verticalDirection);

			to_l8& to(uint8_t* pBuf, size_t width, size_t height, size_t stride, bitmap_vertical_direction verticalDirection);

			to_l8& gamma_table(std::span<const uint8_t> gammaTable);

			to_l8& fore_color(uint8_t color);

			to_l8& back_color(uint8_t color);

			to_l8& fore_opacity(uint8_t opacity);

			to_l8& back_opacity(uint8_t opacity);

			void copy(int srcX1, int srcY1, int srcX2, int srcY2, int targetX1, int targetY1);

		private:
			void draw_line_to_l8(uint8_t* pTarget, const uint8_t* pSource, size_t regionWidth) const;

			void draw_line_to_l8_opaque(uint8_t* pTarget, const uint8_t* pSource, size_t regionWidth) const;

			template<bool ColorIsForeground>
			void draw_line_to_l8_binary_opacity(uint8_t* pTarget, const uint8_t* pSource, size_t regionWidth) const {
				const auto color = ColorIsForeground ? m_colorForeground : m_colorBackground;
				while (regionWidth--) {
					const auto opacityScaled = m_gammaTable[*pSource];
					const auto opacityScaled2 = ColorIsForeground ? opacityScaled : 255 - opacityScaled;
					*pTarget = static_cast<uint8_t>((*pTarget * (255 - opacityScaled2) + 1 * color * opacityScaled2) / 255);
					pTarget += m_nTargetStride;
					pSource += m_nSourceStride;
				}
			}
		};
	}
}

#endif
