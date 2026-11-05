#ifndef XIVRES_STREAMOPLOCKING_H_
#define XIVRES_STREAMOPLOCKING_H_

#include <chrono>
#include <filesystem>
#include <memory>

#include "stream.h"

namespace xivres {
	class oplocking_file_stream : public default_base_stream {
		struct data;
		std::unique_ptr<data> m_data;

	public:
		static constexpr std::chrono::milliseconds IdleDelay{1000};

		oplocking_file_stream(std::filesystem::path path, bool acceptChanges);
		oplocking_file_stream(oplocking_file_stream&&) = delete;
		oplocking_file_stream(const oplocking_file_stream&) = delete;
		oplocking_file_stream& operator=(oplocking_file_stream&&) = delete;
		oplocking_file_stream& operator=(const oplocking_file_stream&) = delete;
		~oplocking_file_stream() override;

		[[nodiscard]] const std::filesystem::path& path() const;

		[[nodiscard]] bool done() const;

		[[nodiscard]] uint64_t generation() const;

		void hold_until(std::chrono::steady_clock::time_point until) const;

		void release() const;

		[[nodiscard]] std::streamsize size() const override;
		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override;

	protected:
		[[nodiscard]] stream_source source_impl() const override { return {path()}; }
	};
}

#endif
