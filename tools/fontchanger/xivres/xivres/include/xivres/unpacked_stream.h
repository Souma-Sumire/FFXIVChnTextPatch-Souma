#ifndef XIVRES_PACKEDFILEUNPACKINGSTREAM_H_
#define XIVRES_PACKEDFILEUNPACKINGSTREAM_H_

#include <utility>

#include "packed_stream.h"
#include "util.thread_pool.h"
#include "util.zlib_wrapper.h"

namespace xivres {
	class unpacked_stream;

	class base_unpacker {
	protected:
		/*
		 * Value   Time taken to decode everything (ms)
		 * 256     37593
		 * 512     36250
		 * 768     35453
		 * 1024    36438
		 */
		static constexpr size_t MinBlockCountForMultithreadedDecompression = 768;

		util::thread_pool::object_pool<std::vector<uint8_t>> m_preloads;

		class block_decoder {
			static constexpr auto ReadBufferMaxSize = 16384;

			util::thread_pool::task_waiter<> m_waiter;
			bool m_bMultithreaded = false;

			const std::span<uint8_t> m_target;
			std::span<uint8_t> m_remaining;
			uint32_t m_skipLength;
			uint32_t m_currentOffset;

		public:
			block_decoder(void* buf, std::streamsize length, std::streamoff offset);
			block_decoder(block_decoder&&) = delete;
			block_decoder(const block_decoder&) = delete;
			block_decoder& operator=(block_decoder&&) = delete;
			block_decoder& operator=(const block_decoder&) = delete;
			~block_decoder() = default;

			void multithreaded(bool m) { m_bMultithreaded = m; }

			bool skip(size_t lengthToSkip, bool dataFilled = false);

			bool skip_to(size_t offset, bool dataFilled = false);

			bool forward_copy(std::span<const uint8_t> data);

			bool forward_sqblock(std::span<const uint8_t> data);

			[[nodiscard]] uint32_t current_offset() const { return m_currentOffset; }

			[[nodiscard]] bool complete() const { return m_remaining.empty(); }

			[[nodiscard]] std::streamsize filled() { m_waiter.wait_all(); return static_cast<std::streamsize>(m_target.size() - m_remaining.size()); }

		private:
			void decode_block_to(std::span<const uint8_t> data, std::span<uint8_t> target, size_t skip) const;
		};

		const uint32_t m_size, m_packedSize;
		const std::shared_ptr<const packed_stream> m_stream;

	public:
		base_unpacker(const packed::file_header& header, std::shared_ptr<const packed_stream> strm)
			: m_size(header.DecompressedSize)
			, m_packedSize(static_cast<uint32_t>(header.occupied_size()))
			, m_stream(std::move(strm)) {}
		base_unpacker(base_unpacker&&) = delete;
		base_unpacker(const base_unpacker&) = delete;
		base_unpacker& operator=(base_unpacker&&) = delete;
		base_unpacker& operator=(const base_unpacker&) = delete;

		virtual std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) = 0;

		[[nodiscard]] uint32_t size() const { return m_size; }

		virtual ~base_unpacker() = default;

		[[nodiscard]] static std::unique_ptr<base_unpacker> make_unique(std::shared_ptr<const packed_stream> strm, std::span<uint8_t> obfuscatedHeaderRewrite = {});

		[[nodiscard]] static std::unique_ptr<base_unpacker> make_unique(const packed::file_header& header, std::shared_ptr<const packed_stream> strm, std::span<uint8_t> obfuscatedHeaderRewrite = {});
	};

	class unpacked_stream : public default_base_stream {
		const std::shared_ptr<const packed_stream> m_provider;
		const packed::file_header m_entryHeader;
		const std::unique_ptr<base_unpacker> m_decoder;

	public:
		unpacked_stream(std::shared_ptr<const packed_stream> provider, std::span<uint8_t> obfuscatedHeaderRewrite = {})
			: m_provider(std::move(provider))
			, m_entryHeader(m_provider->read_fully<packed::file_header>(0))
			, m_decoder(base_unpacker::make_unique(m_entryHeader, m_provider, obfuscatedHeaderRewrite)) {
		}

		[[nodiscard]] std::streamsize size() const override {
			return m_decoder ? *m_entryHeader.DecompressedSize : 0;
		}

		[[nodiscard]] packed::type type() const {
			return m_provider->get_packed_type();
		}

		[[nodiscard]] const path_spec& path_spec() const {
			return m_provider->path_spec();
		}

		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override {
			if (!m_decoder)
				return 0;

			const auto fullSize = *m_entryHeader.DecompressedSize;
			if (std::cmp_greater_equal(offset, fullSize))
				return 0;
			if (offset + length > fullSize)
				length = fullSize - offset;

			const auto read =m_decoder->read(offset, buf, length);
			if (read != length)
				std::fill_n(static_cast<char*>(buf) + read, length - read, 0);
			return length;
		}

	protected:
		[[nodiscard]] stream_source source_impl() const override {
			return m_provider->source();
		}
	};
}

#endif
