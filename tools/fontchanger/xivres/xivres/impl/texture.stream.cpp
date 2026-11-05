#include "../include/xivres/texture.stream.h"

xivres::texture::stream::stream(const std::shared_ptr<xivres::stream>& strm)
	: m_header(strm->read_fully<header>(0))
	, m_repeats(0)
	, m_repeatedUnitSize(0) {
	const auto mipmapLocators = strm->read_vector<uint32_t>(sizeof m_header, m_header.MipmapCount);
	const auto repeatUnitSize = calc_repeat_unit_size(m_header.MipmapCount);
	resize(mipmapLocators.size(), (static_cast<size_t>(strm->size()) - mipmapLocators[0] + repeatUnitSize - 1) / repeatUnitSize);
	for (size_t repeatI = 0; repeatI < m_repeats.size(); ++repeatI) {
		for (size_t mipmapI = 0; mipmapI < mipmapLocators.size(); ++mipmapI) {
			const auto mipmapSize = calc_raw_data_length(m_header, mipmapI);
			auto mipmapDataView = std::make_shared<partial_view_stream>(strm, repeatUnitSize * repeatI + mipmapLocators[mipmapI], mipmapSize);
			auto mipmapView = std::make_shared<wrapped_mipmap_stream>(m_header, mipmapI, std::move(mipmapDataView));
			set_mipmap(mipmapI, repeatI, std::move(mipmapView));
		}
	}
}

xivres::texture::stream::stream(format_type type, size_t width, size_t height, size_t depth, size_t mipmapCount, size_t repeatCount)
	: m_header({
		.Attribute = attribute::TextureType2D,
		.Type = type,
		.Width = util::range_check_cast<uint16_t>(width),
		.Height = util::range_check_cast<uint16_t>(height),
		.Depth = util::range_check_cast<uint16_t>(depth),
		.MipmapCount = 0,
		.LodOffsets = {},
	})
	, m_repeats(0)
	, m_repeatedUnitSize(0) {
	resize(mipmapCount, repeatCount);
}

void xivres::texture::stream::set_mipmap(size_t mipmapIndex, size_t repeatIndex, std::shared_ptr<mipmap_stream> mipmap) {
	auto& mipmaps = m_repeats.at(repeatIndex);
	const auto w = std::max<size_t>(1, m_header.Width >> mipmapIndex);
	const auto h = std::max<size_t>(1, m_header.Height >> mipmapIndex);
	const auto l = std::max<size_t>(1, m_header.Depth >> mipmapIndex);

	if (mipmap->Width != w)
		throw std::invalid_argument("invalid mipmap width");
	if (mipmap->Height != h)
		throw std::invalid_argument("invalid mipmap height");
	if (mipmap->Depth != l)
		throw std::invalid_argument("invalid mipmap depths");
	if (mipmap->Type != *m_header.Type)
		throw std::invalid_argument("invalid mipmap type");
	if (mipmap->size() != static_cast<std::streamsize>(calc_raw_data_length(mipmap->Type, w, h, l)))
		throw std::invalid_argument("invalid mipmap size");

	mipmaps.at(mipmapIndex) = std::move(mipmap);
}

void xivres::texture::stream::resize(size_t mipmapCount, size_t repeatCount) {
	if (mipmapCount == 0)
		throw std::invalid_argument("mipmap count must be a positive integer");
	if (repeatCount == 0)
		throw std::invalid_argument("repeat count must be a positive integer");

	m_header.MipmapCount = util::range_check_cast<uint16_t>(mipmapCount);

	m_repeats.resize(repeatCount = util::range_check_cast<uint16_t>(repeatCount));
	for (auto& mipmaps : m_repeats)
		mipmaps.resize(mipmapCount);

	m_mipmapOffsets.clear();
	m_repeatedUnitSize = 0;
	for (size_t i = 0; i < mipmapCount; ++i) {
		m_mipmapOffsets.push_back(m_header.header_and_mipmap_offsets_size() + m_repeatedUnitSize);
		m_repeatedUnitSize += static_cast<uint32_t>(calc_raw_data_length(m_header, i));
	}
}

std::streamsize xivres::texture::stream::size() const {
	return static_cast<std::streamsize>(m_header.header_and_mipmap_offsets_size() + m_repeats.size() * m_repeatedUnitSize);
}

std::streamsize xivres::texture::stream::read(std::streamoff offset, void* buf, std::streamsize length) const {
	if (!length)
		return 0;

	auto relativeOffset = offset;
	auto out = std::span(static_cast<char*>(buf), static_cast<size_t>(length));

	if (std::cmp_less(relativeOffset, sizeof m_header)) {
		const auto src = util::span_cast<uint8_t>(1, &m_header).subspan(static_cast<size_t>(relativeOffset));
		const auto available = (std::min)(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty())
			return length;
	} else
		relativeOffset -= static_cast<std::streamoff>(sizeof m_header);

	if (const auto srcTyped = std::span(m_mipmapOffsets);
		std::cmp_less(relativeOffset, srcTyped.size_bytes())) {
		const auto src = util::span_cast<uint8_t>(srcTyped).subspan(static_cast<size_t>(relativeOffset));
		const auto available = (std::min)(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty())
			return length;
	} else
		relativeOffset -= static_cast<std::streamoff>(srcTyped.size_bytes());

	if (const auto padSize = static_cast<std::streamoff>(m_header.header_and_mipmap_offsets_size() - sizeof m_header - std::span(m_mipmapOffsets).size_bytes());
		relativeOffset < padSize) {
		const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(padSize - relativeOffset));
		std::fill_n(out.begin(), available, 0);
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty())
			return length;
	} else {
		relativeOffset -= padSize;
	}

	if (m_repeats.empty())
		return static_cast<std::streamsize>(length - out.size_bytes());

	const auto beginningRepeatIndex = static_cast<size_t>(relativeOffset / m_repeatedUnitSize);
	relativeOffset %= m_repeatedUnitSize;

	relativeOffset += m_header.header_and_mipmap_offsets_size();

	auto it = std::ranges::lower_bound(m_mipmapOffsets,
		static_cast<uint32_t>(relativeOffset),
		[&](uint32_t l, uint32_t r) {
			return l < r;
		});

	if (it == m_mipmapOffsets.end() || std::cmp_greater(*it, relativeOffset))
		--it;

	relativeOffset -= *it;

	for (auto repeatIndex = beginningRepeatIndex; repeatIndex < m_repeats.size(); ++repeatIndex) {
		for (auto mipmapIndex = it - m_mipmapOffsets.begin(); it != m_mipmapOffsets.end(); ++it, ++mipmapIndex) {
			std::streamoff padSize;
			if (const auto& mipmap = m_repeats[repeatIndex][mipmapIndex]) {
				const auto mipmapSize = mipmap->size();
				padSize = align(mipmapSize).Pad;

				if (relativeOffset < mipmapSize) {
					const auto available = static_cast<size_t>((std::min<uint64_t>)(out.size_bytes(), mipmapSize - relativeOffset));
					mipmap->read_fully(relativeOffset, out.data(), static_cast<std::streamsize>(available));
					out = out.subspan(available);
					relativeOffset = 0;

					if (out.empty())
						return length;
				} else {
					relativeOffset -= mipmapSize;
				}

			} else {
				padSize = static_cast<std::streamoff>(align(calc_raw_data_length(m_header, mipmapIndex)).Alloc);
			}

			if (relativeOffset < padSize) {
				const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(padSize - relativeOffset));
				std::fill_n(out.begin(), available, 0);
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty())
					return length;
			} else
				relativeOffset -= padSize;
		}
		it = m_mipmapOffsets.begin();
	}

	return static_cast<std::streamsize>(length - out.size_bytes());
}
