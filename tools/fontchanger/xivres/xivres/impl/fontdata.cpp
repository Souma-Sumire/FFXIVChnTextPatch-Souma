#include "../include/xivres/fontdata.h"

#include "../include/xivres/common.h"
#include "../include/xivres/util.h"

xivres::fontdata::stream::stream() {
	std::memcpy(m_fcsv.Signature, header::Signature_Value, sizeof m_fcsv.Signature);
	std::memcpy(m_fthd.Signature, glyph_table_header::Signature_Value, sizeof m_fthd.Signature);
	std::memcpy(m_knhd.Signature, kerning_header::Signature_Value, sizeof m_knhd.Signature);
	m_fcsv.FontTableHeaderOffset = sizeof m_fcsv;
	m_fcsv.KerningHeaderOffset = sizeof m_fcsv + sizeof m_fthd;
}

xivres::fontdata::stream::stream(const xivres::stream& strm, bool strict)
	: m_fcsv(strm.read_fully<header>(0))
	, m_fthd(strm.read_fully<glyph_table_header>(m_fcsv.FontTableHeaderOffset))
	, m_fontTableEntries(strm.read_vector<glyph_entry>(static_cast<std::streamoff>(m_fcsv.FontTableHeaderOffset + sizeof m_fthd), m_fthd.FontTableEntryCount, 0x1000000))
	, m_knhd(strm.read_fully<kerning_header>(m_fcsv.KerningHeaderOffset))
	, m_kerningEntries(strm.read_vector<kerning_entry>(static_cast<std::streamoff>(m_fcsv.KerningHeaderOffset + sizeof m_knhd),(std::min)(m_knhd.EntryCount, m_fthd.KerningEntryCount), 0x1000000)) {
	if (strict) {
		if (0 != std::memcmp(m_fcsv.Signature, header::Signature_Value, sizeof m_fcsv.Signature))
			throw bad_data_error("fcsv.Signature != \"fcsv0100\"");
		if (m_fcsv.FontTableHeaderOffset != sizeof header)
			throw bad_data_error("FontTableHeaderOffset != sizeof header");
		if (!util::all_same_value(m_fcsv.Padding_0x10))
			throw bad_data_error("fcsv.Padding_0x10 != 0");

		if (0 != std::memcmp(m_fthd.Signature, glyph_table_header::Signature_Value, sizeof m_fthd.Signature))
			throw bad_data_error("fthd.Signature != \"fthd\"");
		if (!util::all_same_value(m_fthd.Padding_0x0C))
			throw bad_data_error("fthd.Padding_0x0C != 0");
		if (m_fthd.FontTableEntryCount > 65535)
			throw bad_data_error("fthd.FontTableEntryCount > 65535");
		if (m_fthd.KerningEntryCount > 65535)
			throw bad_data_error("fthd.KerningEntryCount > 65535");

		if (0 != std::memcmp(m_knhd.Signature, kerning_header::Signature_Value, sizeof m_knhd.Signature))
			throw bad_data_error("knhd.Signature != \"knhd\"");
		if (!util::all_same_value(m_knhd.Padding_0x08))
			throw bad_data_error("knhd.Padding_0x08 != 0");

		if (m_knhd.EntryCount != m_fthd.KerningEntryCount)
			throw bad_data_error("knhd.EntryCount != fthd.KerningEntryCount");

		for (const auto& e : m_fontTableEntries) {
			if (e.TextureOffsetX >= 4096 || e.TextureOffsetY >= 4096)
				throw bad_data_error("entry.TextureOffsetX/Y >= 4096");
		}
	}
	std::ranges::sort(m_fontTableEntries, [](const glyph_entry& l, const glyph_entry& r) {
		return l.Utf8Value < r.Utf8Value;
	});
	std::ranges::sort(m_kerningEntries, [](const kerning_entry& l, const kerning_entry& r) {
		if (l.LeftUtf8Value == r.LeftUtf8Value)
			return l.RightUtf8Value < r.RightUtf8Value;
		return l.LeftUtf8Value < r.LeftUtf8Value;
	});
}

std::streamsize xivres::fontdata::stream::size() const {
	return static_cast<std::streamsize>(sizeof m_fcsv
		+ sizeof m_fthd
		+ std::span(m_fontTableEntries).size_bytes()
		+ sizeof m_knhd
		+ std::span(m_kerningEntries).size_bytes());
}

std::streamsize xivres::fontdata::stream::read(std::streamoff offset, void* buf, std::streamsize length) const {
	if (!length)
		return 0;

	auto relativeOffset = offset;
	auto out = std::span(static_cast<char*>(buf), static_cast<size_t>(length));

	if (std::cmp_less(relativeOffset, sizeof m_fcsv)) {
		const auto src = util::span_cast<char>(1, &m_fcsv).subspan(static_cast<size_t>(relativeOffset));
		const auto available = (std::min)(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty())
			return length;
	} else
		relativeOffset -= sizeof m_fcsv;

	if (std::cmp_less(relativeOffset, sizeof m_fthd)) {
		const auto src = util::span_cast<char>(1, &m_fthd).subspan(static_cast<size_t>(relativeOffset));
		const auto available = (std::min)(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty())
			return length;
	} else
		relativeOffset -= sizeof m_fthd;

	if (const auto srcTyped = std::span(m_fontTableEntries);
		std::cmp_less(relativeOffset, srcTyped.size_bytes())) {
		const auto src = util::span_cast<char>(srcTyped).subspan(static_cast<size_t>(relativeOffset));
		const auto available = (std::min)(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty())
			return length;
	} else
		relativeOffset -= static_cast<std::streamoff>(srcTyped.size_bytes());

	if (std::cmp_less(relativeOffset, sizeof m_knhd)) {
		const auto src = util::span_cast<char>(1, &m_knhd).subspan(static_cast<size_t>(relativeOffset));
		const auto available = (std::min)(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty())
			return length;
	} else
		relativeOffset -= sizeof m_knhd;

	if (const auto srcTyped = std::span(m_kerningEntries);
		std::cmp_less(relativeOffset, srcTyped.size_bytes())) {
		const auto src = util::span_cast<char>(srcTyped).subspan(static_cast<size_t>(relativeOffset));
		const auto available = (std::min)(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);

		if (out.empty())
			return length;
	}

	return static_cast<std::streamsize>(length - out.size_bytes());
}

void xivres::fontdata::stream::add_kerning(const kerning_entry& entry, bool cumulative) {
	auto it = m_kerningEntries.end();

	if (m_kerningEntries.empty() || m_kerningEntries.back() < entry)
		void();
	else if (entry < m_kerningEntries.front())
		it = m_kerningEntries.begin();
	else
		it = std::ranges::lower_bound(m_kerningEntries, std::make_pair(*entry.LeftUtf8Value, *entry.RightUtf8Value), {}, [](const kerning_entry& e) {
			return std::make_pair(*e.LeftUtf8Value, *e.RightUtf8Value);
		});

	if (it != m_kerningEntries.end() && it->LeftUtf8Value == entry.LeftUtf8Value && it->RightUtf8Value == entry.RightUtf8Value) {
		if (entry.RightOffset)
			it->RightOffset = entry.RightOffset + (cumulative ? *it->RightOffset : 0);
		else
			m_kerningEntries.erase(it);
	} else if (entry.RightOffset) {
		if (m_kerningEntries.size() >= 65535)
			throw std::runtime_error("The game supports up to 65535 kerning pairs.");
		m_kerningEntries.insert(it, entry);
	}

	m_fthd.KerningEntryCount = m_knhd.EntryCount = static_cast<uint32_t>(m_kerningEntries.size());
}

void xivres::fontdata::stream::add_kerning(char32_t l, char32_t r, int rightOffset, bool cumulative) {
	auto entry = kerning_entry();
	entry.left(l);
	entry.right(r);
	entry.RightOffset = rightOffset;

	add_kerning(entry, cumulative);
}

void xivres::fontdata::stream::add_glyph(const glyph_entry& entry) {
	if (entry.TextureOffsetX >= 4096 || entry.TextureOffsetY >= 4096)
		throw std::invalid_argument("Texture Offset X and Y must be lesser than 4096.");
	if (entry.TextureIndex >= 256)
		throw std::invalid_argument("Texture Index cannot be bigger than 255.");

	auto it = m_fontTableEntries.end();

	if (m_fontTableEntries.empty() || m_fontTableEntries.back() < entry)
		void();
	else if (entry < m_fontTableEntries.front())
		it = m_fontTableEntries.begin();
	else
		it = std::ranges::lower_bound(m_fontTableEntries, *entry.Utf8Value, {}, [](const glyph_entry& e) { return *e.Utf8Value; });

	if (it == m_fontTableEntries.end() || it->Utf8Value != entry.Utf8Value) {
		if (m_fontTableEntries.size() >= 65535 || (entry.Utf8Value == 0x20 && m_fontTableEntries.size() >= 65534))
			throw std::runtime_error("The game supports up to 65535 characters.");
		it = m_fontTableEntries.insert(it, entry);
		m_fcsv.KerningHeaderOffset += sizeof entry;
		m_fthd.FontTableEntryCount += 1;
	} else
		*it = entry;

	// Initial space repeats.
	if (entry.Utf8Value == 0x20) {
		++it;
		if (it == m_fontTableEntries.end() || it->Utf8Value != 0x20) {
			m_fontTableEntries.insert(it, entry);
			m_fcsv.KerningHeaderOffset += sizeof entry;
			m_fthd.FontTableEntryCount += 1;
		} else {
			*it = entry;
		}
	}
}

void xivres::fontdata::stream::add_glyph(char32_t c, uint16_t textureIndex, uint16_t textureOffsetX, uint16_t textureOffsetY, uint8_t boundingWidth, uint8_t boundingHeight, int8_t nextOffsetX, int8_t currentOffsetY) {
	if (textureOffsetX >= 4096 || textureOffsetY >= 4096)
		throw std::invalid_argument("Texture Offset X and Y must be lesser than 4096.");
	if (textureIndex >= 256)
		throw std::invalid_argument("Texture Index cannot be bigger than 255.");

	const auto val = util::unicode::u32_to_u8uint32(c);

	auto it = std::ranges::lower_bound(m_fontTableEntries, val, {}, [](const glyph_entry& e) { return *e.Utf8Value; });

	if (it == m_fontTableEntries.end() || it->Utf8Value != val) {
		if (m_fontTableEntries.size() >= 65535 || (c == 0x20 && m_fontTableEntries.size() >= 65534))
			throw std::runtime_error("The game supports up to 65535 characters.");
		auto entry = glyph_entry();
		entry.Utf8Value = val;
		entry.ShiftJisValue = util::unicode::u32_to_sjisuint16(c);
		it = m_fontTableEntries.insert(it, entry);
		m_fcsv.KerningHeaderOffset += sizeof entry;
		m_fthd.FontTableEntryCount += 1;
	}

	it->TextureIndex = textureIndex;
	it->TextureOffsetX = textureOffsetX;
	it->TextureOffsetY = textureOffsetY;
	it->BoundingWidth = boundingWidth;
	it->BoundingHeight = boundingHeight;
	it->NextOffsetX = nextOffsetX;
	it->CurrentOffsetY = currentOffsetY;

	// Initial space repeats.
	if (c == 0x20) {
		const auto& entry = *it;
		++it;
		if (it == m_fontTableEntries.end() || it->Utf8Value != 0x20) {
			m_fontTableEntries.insert(it, entry);
			m_fcsv.KerningHeaderOffset += sizeof entry;
			m_fthd.FontTableEntryCount += 1;
		} else {
			*it = entry;
		}
	}
}

const std::vector<xivres::fontdata::glyph_entry>& xivres::fontdata::stream::get_glyphs() const {
	return m_fontTableEntries;
}

const std::vector<xivres::fontdata::kerning_entry>& xivres::fontdata::stream::get_kernings() const {
	return m_kerningEntries;
}

void xivres::fontdata::stream::reserve_glyphs(size_t count) {
	m_fontTableEntries.reserve(count);
}

void xivres::fontdata::stream::reserve_kernings(size_t count) {
	m_kerningEntries.reserve(count);
}

int xivres::fontdata::stream::get_kerning(char32_t l, char32_t r) const {
	const auto pair = std::make_pair(util::unicode::u32_to_u8uint32(l), util::unicode::u32_to_u8uint32(r));
	const auto it = std::ranges::lower_bound(m_kerningEntries, pair, {}, [](const kerning_entry& e) {
		return std::make_pair(*e.LeftUtf8Value, *e.RightUtf8Value);
	});
	if (it == m_kerningEntries.end() || it->LeftUtf8Value != pair.first || it->RightUtf8Value != pair.second)
		return 0;
	return it->RightOffset;
}

const xivres::fontdata::glyph_entry* xivres::fontdata::stream::get_glyph(char32_t c) const {
	const auto val = util::unicode::u32_to_u8uint32(c);
	const auto it = std::ranges::lower_bound(m_fontTableEntries, val, {}, [](const glyph_entry& e) { return *e.Utf8Value; });
	if (it == m_fontTableEntries.end() || it->Utf8Value != val)
		return nullptr;
	return &*it;
}
