#ifndef XIVRES_TEXTURE_H_
#define XIVRES_TEXTURE_H_

#include "common.h"
#include "util.byte_order.h"

// https://github.com/NotAdam/Lumina/blob/master/src/Lumina/Data/Files/TexFile.cs
namespace xivres::texture {
	enum class attribute : uint32_t {
		DiscardPerFrame = 0x1,
		DiscardPerMap = 0x2,
		Managed = 0x4,
		UserManaged = 0x8,
		CpuRead = 0x10,
		LocationMain = 0x20,
		NoGpuRead = 0x40,
		AlignedSize = 0x80,
		EdgeCulling = 0x100,
		LocationOnion = 0x200,
		ReadWrite = 0x400,
		Immutable = 0x800,
		TextureRenderTarget = 0x100000,
		TextureDepthStencil = 0x200000,
		TextureType1D = 0x400000,
		TextureType2D = 0x800000,
		TextureType3D = 0x1000000,
		TextureTypeCube = 0x2000000,
		TextureTypeMask = 0x3C00000,
		TextureSwizzle = 0x4000000,
		TextureNoTiled = 0x8000000,
		TextureNoSwizzle = 0x80000000,
	};

	inline attribute operator|(attribute l, attribute r) {
		return static_cast<attribute>(static_cast<uint32_t>(l) | static_cast<uint32_t>(r));
	}

	inline attribute operator&(attribute l, attribute r) {
		return static_cast<attribute>(static_cast<uint32_t>(l) & static_cast<uint32_t>(r));
	}

	inline bool operator!(attribute lss) {
		return static_cast<uint32_t>(lss) == 0;
	}

	union format_type {
		enum class types : uint32_t {
			Integer = 1,
			Float = 2,
			Dxt = 3,
			Bc123 = 3,
			DepthStencil = 4,
			Special = 5,
			Bc57 = 6,
		};
		
		uint32_t Value;

		struct {
			uint32_t Enum : 4;
			uint32_t Bpp : 4;
			uint32_t Component : 4;
			types Type : 4;
		};

		constexpr format_type(): Value(0) {}
		constexpr format_type(format_type&&) noexcept = default;
		constexpr format_type(const format_type&) noexcept = default;
		constexpr format_type& operator=(format_type&&) noexcept = default;
		constexpr format_type& operator=(const format_type&) noexcept = default;
		constexpr format_type(uint32_t v): Value(v) {}
		constexpr ~format_type() = default;

		constexpr operator uint32_t() const {
			return Value;
		}

		constexpr format_type& operator=(uint32_t v) {
			Value = v;
			return *this;
		}

		std::strong_ordering operator<=>(const format_type& r) const {
			return Value <=> r.Value;
		}
	};

	namespace formats {
		static constexpr format_type Unknown = 0;

		// Integer types
		static constexpr format_type L8 = 0x1130;
		static constexpr format_type A8 = 0x1131;
		static constexpr format_type B4G4R4A4 = 0x1440;
		static constexpr format_type B5G5R5A1 = 0x1441;
		static constexpr format_type B8G8R8A8 = 0x1450;
		static constexpr format_type B8G8R8X8 = 0x1451;

		// Floating point types
		static constexpr format_type R32F = 0x2150;
		static constexpr format_type R16G16F = 0x2250;
		static constexpr format_type R32G32F = 0x2260;
		static constexpr format_type R16G16B16A16F = 0x2460;
		static constexpr format_type R32G32B32A32F = 0x2470;

		// Block compression types (DX9 names)
		static constexpr format_type DXT1 = 0x3420;
		static constexpr format_type DXT3 = 0x3430;
		static constexpr format_type DXT5 = 0x3431;
		static constexpr format_type ATI2 = 0x6230;

		// Block compression types (DX11 names)
		static constexpr format_type BC1 = 0x3420;
		static constexpr format_type BC2 = 0x3430;
		static constexpr format_type BC3 = 0x3431;
		static constexpr format_type BC5 = 0x6230;
		static constexpr format_type BC7 = 0x6432;

		// Depth stencil types
		static constexpr format_type D16 = 0x4140;
		static constexpr format_type D24S8 = 0x4250;

		// Special types
		static constexpr format_type Null = 0x5100;
		static constexpr format_type Shadow16 = 0x5140;
		static constexpr format_type Shadow24 = 0x5150;
	}

	struct header {
		LE<attribute> Attribute;
		LE<format_type> Type;
		LE<uint16_t> Width;
		LE<uint16_t> Height;
		LE<uint16_t> Depth;
		LE<uint16_t> MipmapCount;
		LE<uint32_t> LodOffsets[3];

		[[nodiscard]] bool has_attribute(attribute a) const {
			return !!(Attribute & a);
		}

		[[nodiscard]] uint32_t header_and_mipmap_offsets_size() const {
			return std::max<uint32_t>(80, static_cast<uint32_t>(sizeof header + MipmapCount * sizeof uint32_t));
		}
	};

	[[nodiscard]] size_t calc_raw_data_length(format_type type, size_t width, size_t height, size_t depth, size_t mipmapIndex = 0);

	[[nodiscard]] size_t calc_raw_data_length(const header& header, size_t mipmapIndex = 0);
}

#endif
