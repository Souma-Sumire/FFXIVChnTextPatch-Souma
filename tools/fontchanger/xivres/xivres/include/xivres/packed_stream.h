#ifndef XIVRES_PACKEDSTREAM_H_
#define XIVRES_PACKEDSTREAM_H_

#include <chrono>
#include <thread>
#include <type_traits>

#include <zlib.h>

#include "path_spec.h"
#include "sqpack.h"
#include "stream.h"
#include "util.thread_pool.h"
#include "util.zlib_wrapper.h"

namespace xivres {
	class unpacked_stream;

	class packed_stream : public default_base_stream {
		path_spec m_pathSpec;

	public:
		packed_stream(path_spec pathSpec)
			: m_pathSpec(std::move(pathSpec)) {
		}

		bool update_path_spec(const path_spec& r) {
			if (m_pathSpec.has_original() || !r.has_original() || m_pathSpec != r)
				return false;

			m_pathSpec = r;
			return true;
		}

		[[nodiscard]] const path_spec& path_spec() const {
			return m_pathSpec;
		}

		[[nodiscard]] virtual packed::type get_packed_type() const = 0;

		virtual void hold_until(std::chrono::steady_clock::time_point until) const {}

		unpacked_stream get_unpacked(std::span<uint8_t> obfuscatedHeaderRewrite = {}) const;

		std::unique_ptr<unpacked_stream> make_unpacked_ptr(std::span<uint8_t> obfuscatedHeaderRewrite = {}) const;
	};

	class stream_as_packed_stream : public packed_stream {
		const std::shared_ptr<const stream> m_stream;

		mutable packed::type m_entryType = packed::type::invalid;

	public:
		stream_as_packed_stream(xivres::path_spec pathSpec, std::shared_ptr<const stream> strm)
			: packed_stream(std::move(pathSpec))
			, m_stream(std::move(strm)) {
		}

		[[nodiscard]] std::streamsize size() const override {
			return m_stream->size();
		}

		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override {
			return m_stream->read(offset, buf, length);
		}

		[[nodiscard]] packed::type get_packed_type() const override {
			if (m_entryType == packed::type::invalid) {
				// operation that should be lightweight enough that lock should not be needed
				m_entryType = m_stream->read_fully<packed::file_header>(0).Type;
			}
			return m_entryType;
		}

		std::shared_ptr<const stream> base_stream() const {
			return m_stream;
		}

	protected:
		[[nodiscard]] stream_source source_impl() const override {
			return m_stream->source();
		}
	};

	class untyped_passthrough_packer {
	public:
		untyped_passthrough_packer() = default;
		untyped_passthrough_packer(untyped_passthrough_packer&&) = default;
		untyped_passthrough_packer(const untyped_passthrough_packer&) = default;
		untyped_passthrough_packer& operator=(untyped_passthrough_packer&&) = default;
		untyped_passthrough_packer& operator=(const untyped_passthrough_packer&) = default;
		virtual ~untyped_passthrough_packer() = default;

		[[nodiscard]] virtual packed::type get_packed_type() = 0;

		[[nodiscard]] virtual std::streamsize size() = 0;

		[[nodiscard]] virtual std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) = 0;
	};

	template<packed::type TPackedFileType>
	class passthrough_packer : public untyped_passthrough_packer {
	public:
		static constexpr auto Type = TPackedFileType;

	protected:
		const std::shared_ptr<const stream> m_stream;
		size_t m_size = 0;

		virtual void ensure_initialized() = 0;

		virtual std::streamsize translate_read(std::streamoff offset, void* buf, std::streamsize length) = 0;

	public:
		passthrough_packer(std::shared_ptr<const stream> strm) : m_stream(std::move(strm)) {
		}

		[[nodiscard]] packed::type get_packed_type() final { return TPackedFileType; }

		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) final {
			ensure_initialized();
			return translate_read(offset, buf, length);
		}
	};

	template<typename TPacker> requires std::is_base_of_v<untyped_passthrough_packer, TPacker>
	class passthrough_packed_stream : public packed_stream {
		const std::shared_ptr<const stream> m_stream;
		mutable TPacker m_packer;

	public:
		passthrough_packed_stream(xivres::path_spec spec, std::shared_ptr<const stream> strm)
			: packed_stream(std::move(spec))
			, m_stream(strm)
			, m_packer(std::move(strm)) {
		}

		[[nodiscard]] std::streamsize size() const final {
			return m_packer.size();
		}

		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const final {
			return m_packer.read(offset, buf, length);
		}

		packed::type get_packed_type() const final {
			return m_packer.get_packed_type();
		}

	protected:
		[[nodiscard]] stream_source source_impl() const override {
			return m_stream->source();
		}
	};

	class compressing_packer {
		const stream& m_stream;
		const int m_nCompressionLevel;
		const bool m_bMultithreaded;
		bool m_bCancel = false;
		
		mutable std::optional<memory_stream> m_preloadedStream;

	public:
		compressing_packer(const stream& strm, int compressionLevel, bool multithreaded)
			: m_stream(strm)
			, m_nCompressionLevel(compressionLevel)
			, m_bMultithreaded(multithreaded) {
		}

		compressing_packer(compressing_packer&&) = delete;
		compressing_packer(const compressing_packer&) = delete;
		compressing_packer& operator=(compressing_packer&&) = delete;
		compressing_packer& operator=(const compressing_packer&) = delete;

		virtual ~compressing_packer() = default;
		
		void cancel() { m_bCancel = true; }

		[[nodiscard]] virtual std::unique_ptr<stream> pack() = 0;

	protected:
		struct block_data {
			bool Deflated{};
			uint32_t DecompressedSize{};
			std::vector<uint8_t> Data;
		};

		[[nodiscard]] bool multithreaded() const {
			return m_bMultithreaded;
		}
		
		[[nodiscard]] bool cancelled() const {
			return m_bCancel;
		}
		
		[[nodiscard]] const stream& unpacked() const {
			return m_preloadedStream ? *m_preloadedStream : m_stream;
		}

		[[nodiscard]] int compression_level() const {
			return m_nCompressionLevel;
		}

		void preload();

		void compress_block(uint32_t offset, uint32_t length, block_data& blockData) const;
	};

	template<typename TPacker> requires std::is_base_of_v<compressing_packer, TPacker>
	class compressing_packed_stream : public packed_stream {
		constexpr static int CompressionLevel_AlreadyPacked = Z_BEST_COMPRESSION + 1;

		mutable std::mutex m_mtx;
		const stream_source m_originalSource;
		mutable std::shared_ptr<const stream> m_stream;
		mutable int m_compressionLevel;
		const bool m_bMultithreaded;

	public:
		compressing_packed_stream(xivres::path_spec spec, std::shared_ptr<const stream> strm, int compressionLevel = Z_BEST_COMPRESSION, bool multithreaded = true)
			: packed_stream(std::move(spec))
			, m_originalSource(strm->source())
			, m_stream(std::move(strm))
			, m_compressionLevel(compressionLevel)
			, m_bMultithreaded(multithreaded) {
		}

		[[nodiscard]] std::streamsize size() const final {
			ensure_initialized();
			return m_stream->size();
		}

		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const final {
			ensure_initialized();
			return m_stream->read(offset, buf, length);
		}

		packed::type get_packed_type() const final {
			return TPacker::Type;
		}

	protected:
		[[nodiscard]] stream_source source_impl() const override {
			return m_originalSource;
		}

	private:
		void ensure_initialized() const {
			if (m_compressionLevel == CompressionLevel_AlreadyPacked)
				return;

			const auto lock = std::scoped_lock(m_mtx);
			if (m_compressionLevel == CompressionLevel_AlreadyPacked)
				return;

			auto newStream = TPacker(*m_stream, m_compressionLevel, m_bMultithreaded).pack();
			if (!newStream)
				throw std::logic_error("TODO; cancellation currently unhandled");

			m_stream = std::move(newStream);
			m_compressionLevel = CompressionLevel_AlreadyPacked;
		}
	};
}

#endif
