#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winioctl.h>

#include <mutex>
#include <system_error>

#include "../include/xivres/stream.oplocking.h"

namespace {
	constexpr int OpenRetries = 20;
	constexpr DWORD OpenRetryIntervalMs = 50;

	std::system_error last_error(DWORD error = GetLastError()) {
		return {std::error_code(static_cast<int>(error), std::system_category())};
	}
}

struct xivres::oplocking_file_stream::data {
	const std::filesystem::path Path;
	const bool AcceptChanges;

	mutable std::mutex Mtx;
	HANDLE File = INVALID_HANDLE_VALUE;
	bool Refused = false;

	// What the file was when last opened, to tell whether it changed while given up.
	bool Known = false;
	std::streamsize Size = 0;
	FILETIME WriteTime{};
	uint64_t Generation = 0;

	std::chrono::steady_clock::time_point LastAccess{};
	std::chrono::steady_clock::time_point HoldUntil{};

	PTP_TIMER IdleTimer = nullptr;
	PTP_WAIT BreakWait = nullptr;
	HANDLE OplockEvent = nullptr;
	OVERLAPPED OplockOv{};
	REQUEST_OPLOCK_INPUT_BUFFER OplockIn{};
	REQUEST_OPLOCK_OUTPUT_BUFFER OplockOut{};
	bool OplockPending = false;

	data(std::filesystem::path path, bool acceptChanges)
		: Path(std::move(path))
		, AcceptChanges(acceptChanges) {
		OplockEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!OplockEvent)
			throw last_error();

		IdleTimer = CreateThreadpoolTimer(&on_idle_callback, this, nullptr);
		BreakWait = CreateThreadpoolWait(&on_break_callback, this, nullptr);
		if (!IdleTimer || !BreakWait) {
			const auto error = GetLastError();
			destroy_threadpool_objects();
			throw last_error(error);
		}
	}

	~data() {
		// No callback may run once this is gone, and none can be scheduled anew without a read.
		SetThreadpoolTimer(IdleTimer, nullptr, 0, 0);
		WaitForThreadpoolTimerCallbacks(IdleTimer, TRUE);
		SetThreadpoolWait(BreakWait, nullptr, nullptr);
		WaitForThreadpoolWaitCallbacks(BreakWait, TRUE);

		{
			const auto lock = std::scoped_lock(Mtx);
			close_locked();
		}

		destroy_threadpool_objects();
	}

	void destroy_threadpool_objects() {
		if (IdleTimer)
			CloseThreadpoolTimer(IdleTimer);
		if (BreakWait)
			CloseThreadpoolWait(BreakWait);
		if (OplockEvent)
			CloseHandle(OplockEvent);
	}

	static void CALLBACK on_idle_callback(PTP_CALLBACK_INSTANCE, void* context, PTP_TIMER) {
		static_cast<data*>(context)->on_idle();
	}

	static void CALLBACK on_break_callback(PTP_CALLBACK_INSTANCE, void* context, PTP_WAIT, TP_WAIT_RESULT) {
		static_cast<data*>(context)->on_break();
	}

	/// \returns When the file may be given up to others.
	[[nodiscard]] std::chrono::steady_clock::time_point idle_at() const {
		return (std::max)(LastAccess + IdleDelay, HoldUntil);
	}

	void schedule_idle_locked() {
		const auto wait = std::chrono::duration_cast<std::chrono::duration<int64_t, std::ratio<1, 10000000>>>(idle_at() - std::chrono::steady_clock::now());
		const auto due = static_cast<ULONGLONG>(-(std::max<int64_t>)(1, wait.count()));
		FILETIME dueTime{
			.dwLowDateTime = static_cast<DWORD>(due),
			.dwHighDateTime = static_cast<DWORD>(due >> 32),
		};
		SetThreadpoolTimer(IdleTimer, &dueTime, 0, 0);
	}

	void touch_locked() {
		LastAccess = std::chrono::steady_clock::now();
		schedule_idle_locked();
	}

	void on_idle() {
		const auto lock = std::scoped_lock(Mtx);
		if (File == INVALID_HANDLE_VALUE || OplockPending)
			return;

		// Read or held again since this was scheduled.
		if (std::chrono::steady_clock::now() < idle_at()) {
			schedule_idle_locked();
			return;
		}

		ResetEvent(OplockEvent);
		OplockOv = {};
		OplockOv.hEvent = OplockEvent;
		OplockIn = {
			.StructureVersion = REQUEST_OPLOCK_CURRENT_VERSION,
			.StructureLength = sizeof OplockIn,
			.RequestedOplockLevel = OPLOCK_LEVEL_CACHE_READ | OPLOCK_LEVEL_CACHE_HANDLE,
			.Flags = REQUEST_OPLOCK_INPUT_FLAG_REQUEST,
		};
		OplockOut = {
			.StructureVersion = REQUEST_OPLOCK_CURRENT_VERSION,
			.StructureLength = sizeof OplockOut,
			.OriginalOplockLevel = 0,
			.NewOplockLevel = 0,
			.Flags = 0,
			.AccessMode = 0,
			.ShareMode = 0,
		};
		if (!DeviceIoControl(File, FSCTL_REQUEST_OPLOCK, &OplockIn, sizeof OplockIn, &OplockOut, sizeof OplockOut, nullptr, &OplockOv)
			&& GetLastError() == ERROR_IO_PENDING) {
			OplockPending = true;
			SetThreadpoolWait(BreakWait, OplockEvent, nullptr);
			return;
		}

		close_locked();
	}

	void on_break() {
		const auto lock = std::scoped_lock(Mtx);

		if (!OplockPending || !HasOverlappedIoCompleted(&OplockOv))
			return;

		OplockPending = false;
		close_locked();
	}

	void cancel_oplock_locked() {
		if (!OplockPending)
			return;

		SetThreadpoolWait(BreakWait, nullptr, nullptr);
		CancelIoEx(File, &OplockOv);
		DWORD bytes{};
		const auto broke = GetOverlappedResult(File, &OplockOv, &bytes, TRUE);
		OplockPending = false;
		if (broke)
			close_locked();
	}

	void close_locked() {
		cancel_oplock_locked();
		if (File != INVALID_HANDLE_VALUE) {
			CloseHandle(File);
			File = INVALID_HANDLE_VALUE;
		}
	}

	bool open_locked() {
		if (Refused)
			return false;
		if (File != INVALID_HANDLE_VALUE)
			return true;

		for (int i = 0;; ++i) {
			File = CreateFileW(Path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
			if (File != INVALID_HANDLE_VALUE)
				break;
			if (GetLastError() != ERROR_SHARING_VIOLATION || i >= OpenRetries)
				return false;
			Sleep(OpenRetryIntervalMs);
		}

		BY_HANDLE_FILE_INFORMATION info{};
		if (!GetFileInformationByHandle(File, &info)) {
			close_locked();
			return false;
		}

		const auto size = static_cast<std::streamsize>((static_cast<uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow);
		if (Known && (size != Size || CompareFileTime(&info.ftLastWriteTime, &WriteTime) != 0)) {
			++Generation;
			if (!AcceptChanges) {
				Refused = true;
				close_locked();
				return false;
			}
		}

		Known = true;
		Size = size;
		WriteTime = info.ftLastWriteTime;
		return true;
	}

	std::streamsize read_locked(std::streamoff offset, void* buf, std::streamsize length) {
		constexpr int64_t ChunkSize = 0x10000000L;
		if (length > ChunkSize) {
			std::streamsize totalRead = 0;
			for (std::streamoff i = 0; i < length; i += ChunkSize) {
				const auto toRead = (std::min<int64_t>)(ChunkSize, length - i);
				const auto r = read_locked(offset + i, static_cast<char*>(buf) + i, toRead);
				totalRead += r;
				if (r != toRead)
					break;
			}
			return totalRead;
		}

		const auto hEvent = std::unique_ptr<std::remove_pointer_t<HANDLE>, decltype(&CloseHandle)>(CreateEventW(nullptr, TRUE, FALSE, nullptr), &CloseHandle);
		if (!hEvent)
			throw last_error();

		OVERLAPPED ov{};
		ov.hEvent = hEvent.get();
		ov.Offset = static_cast<DWORD>(offset);
		ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
		DWORD readLength = 0;
		if (!ReadFile(File, buf, static_cast<DWORD>(length), &readLength, &ov)) {
			const auto err = GetLastError();
			if (err == ERROR_HANDLE_EOF)
				return 0;
			if (err != ERROR_IO_PENDING)
				throw std::system_error(std::error_code(static_cast<int>(err), std::system_category()));
		}

		if (!GetOverlappedResult(File, &ov, &readLength, TRUE)) {
			const auto err = GetLastError();
			if (err != ERROR_HANDLE_EOF)
				throw std::system_error(std::error_code(static_cast<int>(err), std::system_category()));
			return 0;
		}
		return readLength;
	}
};

xivres::oplocking_file_stream::oplocking_file_stream(std::filesystem::path path, bool acceptChanges)
	: m_data(std::make_unique<data>(std::move(path), acceptChanges)) {
	const auto lock = std::scoped_lock(m_data->Mtx);
	if (m_data->open_locked())
		m_data->touch_locked();
}

xivres::oplocking_file_stream::~oplocking_file_stream() = default;

const std::filesystem::path& xivres::oplocking_file_stream::path() const {
	return m_data->Path;
}

bool xivres::oplocking_file_stream::done() const {
	const auto lock = std::scoped_lock(m_data->Mtx);
	if (m_data->File != INVALID_HANDLE_VALUE)
		return false;
	if (!m_data->open_locked())
		return true;
	m_data->touch_locked();
	return false;
}

uint64_t xivres::oplocking_file_stream::generation() const {
	const auto lock = std::scoped_lock(m_data->Mtx);
	return m_data->Generation;
}

void xivres::oplocking_file_stream::hold_until(std::chrono::steady_clock::time_point until) const {
	const auto lock = std::scoped_lock(m_data->Mtx);
	if (until <= m_data->HoldUntil)
		return;

	m_data->HoldUntil = until;
	if (m_data->File == INVALID_HANDLE_VALUE)
		return;

	m_data->cancel_oplock_locked();
	if (m_data->File != INVALID_HANDLE_VALUE)
		m_data->schedule_idle_locked();
}

void xivres::oplocking_file_stream::release() const {
	const auto lock = std::scoped_lock(m_data->Mtx);
	m_data->close_locked();
}

std::streamsize xivres::oplocking_file_stream::size() const {
	const auto lock = std::scoped_lock(m_data->Mtx);
	if (!m_data->Known && m_data->open_locked())
		m_data->touch_locked();
	return m_data->Size;
}

std::streamsize xivres::oplocking_file_stream::read(std::streamoff offset, void* buf, std::streamsize length) const {
	const auto lock = std::scoped_lock(m_data->Mtx);
	m_data->cancel_oplock_locked();

	if (!m_data->open_locked()) {
		memset(buf, 0, static_cast<size_t>(length));
		return length;
	}

	const auto read = m_data->read_locked(offset, buf, length);
	m_data->touch_locked();
	return read;
}
