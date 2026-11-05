#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#include <utility>

#include "../include/xivres/stream.h"
#include "../include/xivres/util.thread_pool.h"

void xivres::stream::read_fully(std::streamoff offset, void* buf, std::streamsize length) const {
	if (read(offset, buf, length) != length) {
#ifdef _WINDOWS_
		if (IsDebuggerPresent()) {
			__debugbreak();
			static_cast<void>(read(offset, buf, length));
		}
#endif
		throw std::runtime_error("Reached end of stream before reading all of the requested data.");
	}
}

std::unique_ptr<xivres::stream> xivres::default_base_stream::substream(std::streamoff offset, std::streamsize length) const {
	return std::make_unique<partial_view_stream>(shared_from_this(), offset, length);
}

xivres::partial_view_stream::partial_view_stream(std::shared_ptr<const stream> strm, std::streamoff offset, std::streamsize length)
	: m_streamSharedPtr(std::move(strm))
	, m_stream(*m_streamSharedPtr)
	, m_offset(offset)
	, m_size((std::min)(length, m_stream.size() < offset ? 0 : m_stream.size() - offset)) {

}

xivres::partial_view_stream::partial_view_stream(const stream& strm, std::streamoff offset, std::streamsize length)
	: m_stream(strm)
	, m_offset(offset)
	, m_size((std::min)(length, m_stream.size() < offset ? 0 : m_stream.size() - offset)) {

}

xivres::partial_view_stream::partial_view_stream(const partial_view_stream&) = default;

std::streamsize xivres::partial_view_stream::size() const {
	return m_size;
}

std::streamsize xivres::partial_view_stream::read(std::streamoff offset, void* buf, std::streamsize length) const {
	if (offset >= m_size)
		return 0;
	length = (std::min)(length, m_size - offset);
	return m_stream.read(m_offset + offset, buf, length);
}

std::unique_ptr<xivres::stream> xivres::partial_view_stream::substream(std::streamoff offset, std::streamsize length) const {
	return std::make_unique<partial_view_stream>(m_streamSharedPtr, m_offset + offset, (std::min)(length, m_size));
}

xivres::stream_source xivres::partial_view_stream::source_impl() const {
	auto result = m_stream.source();
	result.Offset += m_offset;
	return result;
}

#ifdef _WIN32
struct xivres::file_stream::data {
	const std::filesystem::path m_path;
	const HANDLE m_hFile;
	mutable util::thread_pool::object_pool<std::shared_ptr<void>> m_hDummyEvents;

	data(std::filesystem::path path)
		: m_path(std::move(path))
		, m_hFile(CreateFileW(m_path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr)) {
		if (m_hFile == INVALID_HANDLE_VALUE)
			throw std::system_error(std::error_code(static_cast<int>(GetLastError()), std::system_category()));
	}

	data(data&&) = delete;
	data(const data&) = delete;
	data& operator=(data&&) = delete;
	data& operator=(const data&) = delete;

	~data() {
		CloseHandle(m_hFile);
	}

	[[nodiscard]] std::streamsize size() const {
		LARGE_INTEGER fs{};
		GetFileSizeEx(m_hFile, &fs);
		return fs.QuadPart;
	}

	std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const {
		constexpr int64_t ChunkSize = 0x10000000L;
		if (length > ChunkSize) {
			size_t totalRead = 0;
			for (std::streamoff i = 0; i < length; i += ChunkSize) {
				const auto toRead = static_cast<DWORD>((std::min<int64_t>)(ChunkSize, length - i));
				const auto r = read(offset + i, static_cast<char*>(buf) + i, toRead);
				totalRead += r;
				if (std::cmp_not_equal(r, toRead))
					break;
			}
			return static_cast<std::streamsize>(totalRead);

		} else {
			auto hDummyEvent = *m_hDummyEvents;
			if (!hDummyEvent) {
				const auto handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
				if (handle == nullptr)
					throw std::system_error(std::error_code(static_cast<int>(GetLastError()), std::system_category()));
				hDummyEvent.emplace(handle, [](HANDLE h) { CloseHandle(h); });
			}
			DWORD readLength = 0;
			OVERLAPPED ov{};
			ov.hEvent = hDummyEvent->get();
			ov.Offset = static_cast<DWORD>(offset);
			ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
			if (!ReadFile(m_hFile, buf, static_cast<DWORD>(length), &readLength, &ov)) {
				const auto err = GetLastError();
				if (err != ERROR_HANDLE_EOF)
					throw std::system_error(std::error_code(static_cast<int>(err), std::system_category()));
			}
			return readLength;
		}
	}
};

#else

struct xivres::file_stream::data {
	const std::filesystem::path m_path;
	mutable std::mutex m_mutex;
	mutable std::vector<std::ifstream> m_streams;

	class PooledObject {
		const file_stream& m_parent;
		mutable std::ifstream m_stream;

	public:
		PooledObject(const file_stream& parent)
			: m_parent(parent) {
			const auto lock = std::scoped_lock(parent.m_mutex);
			if (parent.m_streams.empty())
				m_stream = std::ifstream(parent.m_path, std::ios::binary);
			else {
				m_stream = std::move(parent.m_streams.back());
				parent.m_streams.pop_back();
			}
		}

		~PooledObject() {
			const auto lock = std::scoped_lock(m_parent.m_mutex);
			m_parent.m_streams.emplace_back(std::move(m_stream));
		}

		std::ifstream* operator->() const {
			return &m_stream;
		}
	};


	xivres::file_stream::file_stream(std::filesystem::path path)
		: m_path(std::move(path)) {
		m_streams.emplace_back(m_path, std::ios::binary);
	}

	std::streamsize xivres::file_stream::size() const {
		PooledObject strm(*this);
		strm->seekg(0, std::ios::end);
		return strm->tellg();
	}

	std::streamsize xivres::file_stream::read(std::streamoff offset, void* buf, std::streamsize length) const {
		PooledObject strm(*this);
		strm->seekg(offset, std::ios::beg);
		strm->read(static_cast<char*>(buf), length);
		return strm->gcount();
	}
};

#endif

xivres::file_stream::file_stream() = default;
xivres::file_stream::file_stream(file_stream&&) noexcept = default;
xivres::file_stream& xivres::file_stream::operator=(file_stream&&) noexcept = default;
xivres::file_stream::~file_stream() = default;

xivres::file_stream::file_stream(std::filesystem::path path)
	: m_data(std::make_unique<data>(std::move(path))) {
}

std::streamsize xivres::file_stream::size() const { return m_data->size(); }
std::streamsize xivres::file_stream::read(std::streamoff offset, void* buf, std::streamsize length) const { return m_data->read(offset, buf, length); }
xivres::stream_source xivres::file_stream::source_impl() const { return {m_data ? m_data->m_path : std::filesystem::path()}; }

xivres::memory_stream& xivres::memory_stream::operator=(const memory_stream& r) {
	if (r.owns_data()) {
		m_buffer = r.m_buffer;
		m_view = std::span(m_buffer);
	} else {
		m_buffer.clear();
		m_view = r.m_view;
	}
	return *this;
}

xivres::memory_stream& xivres::memory_stream::operator=(memory_stream&& r) noexcept {
	m_buffer = std::move(r.m_buffer);
	m_view = r.m_view;
	r.m_view = {};
	return *this;
}

xivres::memory_stream& xivres::memory_stream::operator=(const std::vector<uint8_t>& buf) {
	m_buffer = buf;
	m_view = std::span(m_buffer);
	return *this;
}

xivres::memory_stream& xivres::memory_stream::operator=(std::vector<uint8_t>&& buf) noexcept {
	m_buffer = std::move(buf);
	m_view = std::span(m_buffer);
	return *this;
}

xivres::memory_stream::memory_stream(std::span<const uint8_t> view)
	: m_view(view) {
}

xivres::memory_stream::memory_stream(memory_stream&& r) noexcept {
	swap(*this, r);
}

xivres::memory_stream::memory_stream(const memory_stream& r)
	: default_base_stream(r) {
	m_buffer = r.m_buffer;
	if (r.owns_data())
		m_view = {m_buffer};
	else
		m_view = r.m_view;
}

xivres::memory_stream::memory_stream(const stream& r)
	: m_buffer(static_cast<size_t>(r.size()))
	, m_view(std::span(m_buffer)) {
	r.read_fully(0, std::span(m_buffer));
}

xivres::memory_stream::memory_stream(std::vector<uint8_t> buffer)
	: m_buffer(std::move(buffer))
	, m_view(m_buffer) {
}

xivres::memory_stream::memory_stream(std::span<const char> view)
	: memory_stream(view.empty() ? std::span<const uint8_t>() : std::span(reinterpret_cast<const uint8_t*>(view.data()), view.size())) {
}

std::streamsize xivres::memory_stream::size() const {
	return static_cast<std::streamsize>(m_view.size());
}

std::streamsize xivres::memory_stream::read(std::streamoff offset, void* buf, std::streamsize length) const {
	if (std::cmp_greater_equal(offset, m_view.size()))
		return 0;
	if (offset + length > static_cast<std::streamoff>(m_view.size()))
		length = static_cast<std::streamsize>(m_view.size() - offset);
	std::copy_n(&m_view[static_cast<size_t>(offset)], static_cast<size_t>(length), static_cast<char*>(buf));
	return length;
}

bool xivres::memory_stream::owns_data() const {
	return !m_buffer.empty() && m_view.data() == m_buffer.data();
}

std::span<const uint8_t> xivres::memory_stream::as_span(std::streamoff offset, std::streamsize length) const {
	return m_view.subspan(static_cast<size_t>(offset), static_cast<size_t>(length));
}

void xivres::swap(memory_stream& l, memory_stream& r) noexcept {
	std::swap(l.m_buffer, r.m_buffer);
	std::swap(l.m_view, r.m_view);
}
