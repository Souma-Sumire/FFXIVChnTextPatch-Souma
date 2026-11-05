#include "../include/xivres/packed_stream.hotswap.h"

#include <utility>

xivres::hotswap_packed_stream::hotswap_packed_stream(const xivres::path_spec& pathSpec, uint32_t reservedSize, std::shared_ptr<const packed_stream> strm)
	: packed_stream(pathSpec)
	, m_reservedSize(align(reservedSize))
	, m_baseStream(std::move(strm)) {
	if (m_baseStream && m_baseStream->size() > m_reservedSize)
		throw std::invalid_argument("Provided strm requires more space than reserved size");
}

std::shared_ptr<const xivres::packed_stream> xivres::hotswap_packed_stream::swap_stream(std::shared_ptr<const packed_stream> newStream) {
	if (newStream && newStream->size() > m_reservedSize)
		throw std::invalid_argument("Provided strm requires more space than reserved size");
	const auto lock = std::scoped_lock(m_streamMtx);
	auto oldStream{ std::move(m_stream) };
	m_stream = std::move(newStream);
	return oldStream;
}

std::shared_ptr<const xivres::packed_stream> xivres::hotswap_packed_stream::current_stream() const {
	const auto lock = std::scoped_lock(m_streamMtx);
	return m_stream ? m_stream : m_baseStream;
}

std::shared_ptr<const xivres::packed_stream> xivres::hotswap_packed_stream::base_stream() const {
	return m_baseStream;
}

std::streamsize xivres::hotswap_packed_stream::size() const {
	return m_reservedSize;
}

std::streamsize xivres::hotswap_packed_stream::read(std::streamoff offset, void* buf, std::streamsize length) const {
	if (std::cmp_greater_equal(offset, m_reservedSize))
		return 0;
	if (offset + length > m_reservedSize)
		length = m_reservedSize - offset;

	auto target = std::span(static_cast<uint8_t*>(buf), static_cast<size_t>(length));
	const auto current = current_stream();
	const auto& underlyingStream = current ? *current : placeholder_packed_stream::instance();
	const auto underlyingStreamLength = underlyingStream.size();
	const auto dataLength = offset < underlyingStreamLength ? (std::min)(length, underlyingStreamLength - offset) : 0;

	if (offset < underlyingStreamLength) {
		const auto dataTarget = target.subspan(0, static_cast<size_t>(dataLength));
		const auto readLength = static_cast<size_t>(underlyingStream.read(offset, dataTarget.data(), static_cast<std::streamsize>(dataTarget.size_bytes())));
		if (readLength != dataTarget.size_bytes())
			throw std::logic_error("HotSwappableEntryProvider underlying data read fail");
		target = target.subspan(readLength);
	}
	std::ranges::fill(target, 0);
	return length;
}

void xivres::hotswap_packed_stream::hold_until(std::chrono::steady_clock::time_point until) const {
	if (const auto current = current_stream())
		current->hold_until(until);
}

xivres::packed::type xivres::hotswap_packed_stream::get_packed_type() const {
	const auto current = current_stream();
	return current ? current->get_packed_type() : placeholder_packed_stream::instance().get_packed_type();
}

xivres::stream_source xivres::hotswap_packed_stream::source_impl() const {
	const auto current = current_stream();
	return current ? current->source() : stream_source{};
}
