#ifndef XIVRES_STREAM_H_
#define XIVRES_STREAM_H_

#include <algorithm>
#include <filesystem>
#include <functional>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

#include "util.span_cast.h"
#include "util.tag_holder.h"

namespace xivres {
	class partial_view_stream;

	template<typename T>
	using linear_reader = std::function<std::span<T>(size_t len, bool throwOnIncompleteRead)>;

	struct stream_source {
		std::filesystem::path Path;
		std::streamoff Offset = 0;
		std::vector<util::tag_set> Tags;

		template<typename T>
		[[nodiscard]] std::shared_ptr<const T> find_tag() const {
			for (auto it = Tags.rbegin(); it != Tags.rend(); ++it) {
				if (auto tag = it->get<T>())
					return tag;
			}
			return nullptr;
		}
	};

	class stream : public util::tag_holder {
	protected:
		[[nodiscard]] virtual stream_source source_impl() const { return {}; }

	public:
		[[nodiscard]] stream_source source() const {
			auto result = source_impl();
			if (auto own = tags(); !own.empty())
				result.Tags.push_back(std::move(own));
			return result;
		}

		stream() = default;
		stream(stream&&) = default;
		stream(const stream&) = default;
		stream& operator=(stream&&) = default;
		stream& operator=(const stream&) = default;
		virtual ~stream() = default;

		[[nodiscard]] virtual std::streamsize size() const = 0;

		[[nodiscard]] virtual std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const = 0;

		[[nodiscard]] virtual std::unique_ptr<stream> substream(std::streamoff offset, std::streamsize length = (std::numeric_limits<std::streamsize>::max)()) const = 0;

		void read_fully(std::streamoff offset, void* buf, std::streamsize length) const;

		template<typename T>
		[[nodiscard]] T read_fully(std::streamoff offset) const {
			T buf{};
			read_fully(offset, &buf, sizeof(T));
			return buf;
		}

		template<typename T>
		void read_fully(std::streamoff offset, std::span<T> buf) const {
			read_fully(offset, buf.data(), buf.size_bytes());
		}

		template<typename T>
		[[nodiscard]] std::vector<T> read_vector(std::streamoff offset = 0, size_t count = SIZE_MAX, size_t maxCount = SIZE_MAX) const {
			if (count == SIZE_MAX) {
				const auto streamSize = size();
				if (offset < 0 || offset > streamSize)
					throw std::out_of_range("offset is outside the stream");
				count = static_cast<size_t>(static_cast<uint64_t>(streamSize - offset) / sizeof(T));
			}
			if (count > maxCount)
				throw std::runtime_error("trying to read too many");
			std::vector<T> result(count);
			read_fully(offset, std::span(result));
			return result;
		}

		template<typename T>
		[[nodiscard]] linear_reader<T> as_linear_reader() const {
			return [this, buf = std::vector<T>(), ptr = uint64_t(), to = size()](size_t len, bool throwOnIncompleteRead) mutable {
				if (std::cmp_equal(ptr, to))
					return std::span<T>();
				buf.resize(static_cast<size_t>(std::min<uint64_t>(len, to - ptr)));
				const auto r = read(ptr, buf.data(), buf.size());
				if (r < static_cast<std::streamoff>(buf.size()) && throwOnIncompleteRead)
					throw std::runtime_error("incomplete read");
				ptr += buf.size();
				return std::span(buf);
			};
		}
	};

	class default_base_stream : public stream, public std::enable_shared_from_this<default_base_stream> {
	public:
		default_base_stream() = default;
		default_base_stream(default_base_stream&&) = default;
		default_base_stream(const default_base_stream&) = default;
		default_base_stream& operator=(default_base_stream&&) = default;
		default_base_stream& operator=(const default_base_stream&) = default;
		~default_base_stream() override = default;

		std::unique_ptr<stream> substream(std::streamoff offset, std::streamsize length = (std::numeric_limits<std::streamsize>::max)()) const override;
	};

	class partial_view_stream : public default_base_stream {
		const std::shared_ptr<const stream> m_streamSharedPtr;

	public:
		const stream& m_stream;
		const std::streamoff m_offset;

	private:
		const std::streamsize m_size;

	public:
		partial_view_stream(const stream& strm, std::streamoff offset = 0, std::streamsize length = (std::numeric_limits<std::streamsize>::max)());
		partial_view_stream(const partial_view_stream&);
		partial_view_stream(std::shared_ptr<const stream> strm, std::streamoff offset = 0, std::streamsize length = (std::numeric_limits<std::streamsize>::max)());
		partial_view_stream(partial_view_stream&&) = delete;
		partial_view_stream& operator=(partial_view_stream&&) = delete;
		partial_view_stream& operator=(const partial_view_stream&) = delete;
		~partial_view_stream() override = default;

		[[nodiscard]] std::streamsize size() const override;
		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override;
		[[nodiscard]] std::unique_ptr<stream> substream(std::streamoff offset, std::streamsize length = (std::numeric_limits<std::streamsize>::max)()) const override;

	protected:
		[[nodiscard]] stream_source source_impl() const override;
	};

	class file_stream : public default_base_stream {
		struct data;
		std::unique_ptr<data> m_data;

	public:
		file_stream();
		file_stream(std::filesystem::path path);
		file_stream(file_stream&&) noexcept;
		file_stream& operator=(file_stream&&) noexcept;
		file_stream(const file_stream&) = delete;
		file_stream& operator=(const file_stream&) = delete;
		~file_stream() override;

		[[nodiscard]] std::streamsize size() const override;
		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override;

	protected:
		[[nodiscard]] stream_source source_impl() const override;
	};

	class memory_stream : public default_base_stream {
		std::vector<uint8_t> m_buffer;
		std::span<const uint8_t> m_view;

	public:
		memory_stream() = default;
		memory_stream(memory_stream&& r) noexcept;
		memory_stream(const memory_stream& r);
		memory_stream(const stream& r);
		memory_stream(std::vector<uint8_t> buffer);
		memory_stream(std::span<const char> view);
		memory_stream(std::span<const uint8_t> view);
		memory_stream& operator=(std::vector<uint8_t>&& buf) noexcept;
		memory_stream& operator=(const std::vector<uint8_t>& buf);
		memory_stream& operator=(memory_stream&& r) noexcept;
		memory_stream& operator=(const memory_stream& r);
		~memory_stream() override = default;

		[[nodiscard]] std::streamsize size() const override;
		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override;

		[[nodiscard]] bool owns_data() const;
		std::span<const uint8_t> as_span(std::streamoff offset, std::streamsize length = (std::numeric_limits<std::streamsize>::max)()) const;

		friend void swap(memory_stream& l, memory_stream& r) noexcept;
	};
}

#endif
