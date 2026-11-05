#ifndef XIVRES_Fontdata_H_
#define XIVRES_Fontdata_H_

#include "stream.h"
#include "util.byte_order.h"
#include "util.unicode.h"

namespace xivres::fontdata {
	struct header {
		static constexpr char Signature_Value[8] = {
			'f', 'c', 's', 'v', '0', '1', '0', '0',
		};

		char Signature[8]{};
		LE<uint32_t> FontTableHeaderOffset;
		LE<uint32_t> KerningHeaderOffset;
		uint8_t Padding_0x10[0x10]{};
	};

	static_assert(sizeof header == 0x20);

	struct glyph_table_header {
		static constexpr char Signature_Value[4] = {
			'f', 't', 'h', 'd',
		};

		char Signature[4]{};
		LE<uint32_t> FontTableEntryCount;
		LE<uint32_t> KerningEntryCount;
		uint8_t Padding_0x0C[4]{};
		LE<uint16_t> TextureWidth;
		LE<uint16_t> TextureHeight;
		LE<float> Size{0.f};
		LE<uint32_t> LineHeight;
		LE<uint32_t> Ascent;
	};

	static_assert(sizeof glyph_table_header == 0x20);

	struct glyph_entry {
		static constexpr size_t ChannelMap[4]{2, 1, 0, 3};

		LE<uint32_t> Utf8Value;
		LE<uint16_t> ShiftJisValue;
		LE<uint16_t> TextureIndex;
		LE<uint16_t> TextureOffsetX;
		LE<uint16_t> TextureOffsetY;
		LE<uint8_t> BoundingWidth;
		LE<uint8_t> BoundingHeight;
		LE<int8_t> NextOffsetX;
		LE<int8_t> CurrentOffsetY;

		[[nodiscard]] char32_t codepoint() const {
			return util::unicode::u8uint32_to_u32(Utf8Value);
		}

		char32_t codepoint(char32_t newValue) {
			Utf8Value = util::unicode::u32_to_u8uint32(newValue);
			ShiftJisValue = util::unicode::u32_to_sjisuint16(newValue);
			return newValue;
		}

		[[nodiscard]] uint16_t texture_file_index() const {
			return *TextureIndex >> 2;
		}

		[[nodiscard]] uint16_t texture_plane_index() const {
			return *TextureIndex & 3;
		}

		auto operator<=>(const glyph_entry& r) const {
			return *Utf8Value <=> *r.Utf8Value;
		}

		auto operator<=>(uint32_t r) const {
			return *Utf8Value <=> r;
		}
	};

	static_assert(sizeof glyph_entry == 0x10);

	struct kerning_header {
		static constexpr char Signature_Value[4] = {
			'k', 'n', 'h', 'd',
		};

		char Signature[4]{};
		LE<uint32_t> EntryCount;
		uint8_t Padding_0x08[8]{};
	};

	static_assert(sizeof kerning_header == 0x10);

	struct kerning_entry {
		LE<uint32_t> LeftUtf8Value;
		LE<uint32_t> RightUtf8Value;
		LE<uint16_t> LeftShiftJisValue;
		LE<uint16_t> RightShiftJisValue;
		LE<int32_t> RightOffset;

		[[nodiscard]] char32_t left() const {
			return util::unicode::u8uint32_to_u32(LeftUtf8Value);
		}

		char32_t left(char32_t newValue) {
			LeftUtf8Value = util::unicode::u32_to_u8uint32(newValue);
			LeftShiftJisValue = util::unicode::u32_to_sjisuint16(newValue);
			return newValue;
		}

		[[nodiscard]] char32_t right() const {
			return util::unicode::u8uint32_to_u32(RightUtf8Value);
		}

		char32_t right(char32_t newValue) {
			RightUtf8Value = util::unicode::u32_to_u8uint32(newValue);
			RightShiftJisValue = util::unicode::u32_to_sjisuint16(newValue);
			return newValue;
		}

		auto operator<=>(const kerning_entry& r) const {
			if (const auto v = *LeftUtf8Value <=> *r.LeftUtf8Value; v != std::strong_ordering::equal)
				return v;
			return *RightUtf8Value <=> *r.RightUtf8Value;
		}
	};

	static_assert(sizeof kerning_entry == 0x10);

	class stream : public default_base_stream {
		header m_fcsv;
		glyph_table_header m_fthd;
		std::vector<glyph_entry> m_fontTableEntries;
		kerning_header m_knhd;
		std::vector<kerning_entry> m_kerningEntries;

	public:
		stream();
		stream(const xivres::stream& strm, bool strict = false);

		[[nodiscard]] std::streamsize size() const override;
		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override;

		[[nodiscard]] const glyph_entry* get_glyph(char32_t c) const;
		[[nodiscard]] int get_kerning(char32_t l, char32_t r) const;

		[[nodiscard]] const std::vector<glyph_entry>& get_glyphs() const;
		[[nodiscard]] const std::vector<kerning_entry>& get_kernings() const;

		void reserve_glyphs(size_t count);
		void reserve_kernings(size_t count);

		void add_glyph(char32_t c, uint16_t textureIndex, uint16_t textureOffsetX, uint16_t textureOffsetY, uint8_t boundingWidth, uint8_t boundingHeight, int8_t nextOffsetX, int8_t currentOffsetY);
		void add_glyph(const glyph_entry& entry);

		void add_kerning(char32_t l, char32_t r, int rightOffset, bool cumulative = false);
		void add_kerning(const kerning_entry& entry, bool cumulative = false);

		[[nodiscard]] uint16_t texture_width() const { return m_fthd.TextureWidth; }
		void texture_width(uint16_t v) { m_fthd.TextureWidth = v; }

		[[nodiscard]] uint16_t texture_height() const { return m_fthd.TextureHeight; }
		void texture_height(uint16_t v) { m_fthd.TextureHeight = v; }

		[[nodiscard]] float font_size() const { return m_fthd.Size; }
		void font_size(float v) { m_fthd.Size = v; }

		[[nodiscard]] uint32_t line_height() const { return m_fthd.LineHeight; }
		void line_height(uint32_t v) { m_fthd.LineHeight = v; }

		[[nodiscard]] uint32_t ascent() const { return m_fthd.Ascent; }
		void ascent(uint32_t v) { m_fthd.Ascent = v; }
	};
}

#endif
