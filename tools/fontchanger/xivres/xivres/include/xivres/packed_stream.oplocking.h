#ifndef XIVRES_PACKEDFILESTREAMOPLOCKING_H_
#define XIVRES_PACKEDFILESTREAMOPLOCKING_H_

#include <filesystem>
#include <mutex>

#include "packed_stream.h"
#include "stream.oplocking.h"

namespace xivres {
	/// packs oplocking_file_stream on read, and again when the file changes
	class oplocking_packed_stream : public packed_stream {
		const std::filesystem::path m_path;
		packed::type m_packedType = packed::type::placeholder;

		mutable std::mutex m_mtx;
		mutable std::shared_ptr<oplocking_file_stream> m_file;
		mutable std::shared_ptr<packed_stream> m_packedStream;
		mutable uint64_t m_packedGeneration = 0;

		[[nodiscard]] std::shared_ptr<packed_stream> packed() const;

	public:
		oplocking_packed_stream(xivres::path_spec pathSpec, std::filesystem::path path);

		[[nodiscard]] packed::type get_packed_type() const override;
		[[nodiscard]] std::streamsize size() const override;
		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override;
		void hold_until(std::chrono::steady_clock::time_point until) const override;

		void close();

	protected:
		[[nodiscard]] stream_source source_impl() const override { return {m_path}; }
	};
}

#endif
