#ifndef XIVRES_SQPACKGENERATOR_H_
#define XIVRES_SQPACKGENERATOR_H_

#include <chrono>
#include <map>
#include <mutex>
#include <optional>
#include <thread>

#include "packed_stream.h"
#include "sqpack.reader.h"
#include "unpacked_stream.h"
#include "util.listener_manager.h"

namespace xivres::sqpack {
	class generator {
		const uint64_t m_maxFileSize;

	public:
		class sqpack_view_entry_cache;

		class entry_info : public packed_stream {
			uint32_t m_entrySize{};
			sqindex::data_locator m_locator{};
			mutable std::mutex m_streamMtx;
			std::shared_ptr<const packed_stream> m_baseStream;
			std::shared_ptr<const packed_stream> m_stream;

			std::optional<std::pair<sqindex::data_locator, uint64_t>> m_originalPlace;
			bool m_keepsOriginalPlace = false;

			[[nodiscard]] std::shared_ptr<const packed_stream> current_stream() const;

		public:
			entry_info(xivres::path_spec pathSpec, std::shared_ptr<const packed_stream> baseStream)
				: packed_stream(std::move(pathSpec))
				, m_baseStream(std::move(baseStream)) {
			}

			[[nodiscard]] uint32_t entry_size() const { return m_entrySize; }
			void reserve_entry_size(uint32_t size) { m_entrySize = (std::max)(m_entrySize, size); }
			void finalize_entry_size();

			[[nodiscard]] const sqindex::data_locator& locator() const { return m_locator; }
			void locator(sqindex::data_locator v) { m_locator = v; }

			void original_place(sqindex::data_locator locator, uint64_t allocation) { m_originalPlace.emplace(locator, allocation); }
			bool try_keep_original_place();
			[[nodiscard]] bool keeps_original_place() const { return m_keepsOriginalPlace; }

			[[nodiscard]] std::shared_ptr<const packed_stream> base_stream() const {
				const auto lock = std::scoped_lock(m_streamMtx);
				return m_baseStream;
			}

			void reset_base_stream(std::shared_ptr<const packed_stream> baseStream) {
				const auto lock = std::scoped_lock(m_streamMtx);
				m_baseStream = std::move(baseStream);
				m_originalPlace.reset();
			}

			std::shared_ptr<const packed_stream> swap_stream(std::shared_ptr<const packed_stream> newStream = nullptr);

			[[nodiscard]] bool swapped() const {
				const auto lock = std::scoped_lock(m_streamMtx);
				return !!m_stream;
			}

			[[nodiscard]] uint64_t data_size() const;

			[[nodiscard]] std::streamsize size() const override { return m_entrySize; }
			std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override;
			void hold_until(std::chrono::steady_clock::time_point until) const override;
			[[nodiscard]] packed::type get_packed_type() const override;

		protected:
			[[nodiscard]] stream_source source_impl() const override;
		};

		class data_view_stream : public default_base_stream {
			const std::vector<uint8_t> m_header;
			const std::span<entry_info*> m_entries;
			const std::shared_ptr<const stream> m_original;
			const uint64_t m_originalSize;
			const uint64_t m_size;
			const std::shared_ptr<sqpack_view_entry_cache> m_buffer;
			const bool m_streamed;

		public:
			data_view_stream(const header& header, const sqdata::header& subheader, std::span<entry_info*> entries, std::shared_ptr<const stream> original, std::shared_ptr<sqpack_view_entry_cache> buffer, bool streamed);

			std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override;
			[[nodiscard]] std::streamsize size() const override { return static_cast<std::streamsize>(m_size); }

			[[nodiscard]] uint64_t original_size() const { return m_originalSize; }
			[[nodiscard]] uint64_t header_size() const { return m_header.size(); }
			[[nodiscard]] bool reads_original(uint64_t offset, uint64_t length) const;
			[[nodiscard]] const entry_info* entry_at(uint64_t offset) const;

			[[nodiscard]] std::span<const entry_info* const> entries_in(uint64_t offset, uint64_t length) const;

		private:
			[[nodiscard]] std::span<entry_info*>::iterator entry_at_or_before(uint64_t offset) const;
		};

		struct add_result {
			std::vector<packed_stream*> Added;
			std::vector<packed_stream*> Replaced;
			std::vector<packed_stream*> SkippedExisting;
			std::vector<std::pair<path_spec, std::string>> Error;

			add_result& operator+=(const add_result& r);
			add_result& operator+=(add_result& r);

			[[nodiscard]] packed_stream* any() const;
			[[nodiscard]] std::vector<packed_stream*> all_success() const;
		};

		struct sqpack_views {
			std::shared_ptr<stream> Index1;
			std::shared_ptr<stream> Index2;
			std::vector<std::shared_ptr<stream>> Data;
			std::vector<entry_info*> Entries;

			mutable std::map<path_spec, entry_info, path_spec::AllHashComparator> HashOnlyEntries;
			mutable std::map<path_spec, entry_info, path_spec::FullPathComparator> FullPathEntries;

			[[nodiscard]] entry_info* find_entry(const path_spec& pathSpec) const;
			[[nodiscard]] entry_info& get_entry(const path_spec& pathSpec) const;
		};

		class sqpack_view_entry_cache {
		public:
			static constexpr uint64_t MaxBufferedEntrySize = (INTPTR_MAX == INT64_MAX ? 1024ULL : 64ULL) * 1048576;
			static constexpr std::chrono::seconds ReaderTimeout{30};
			static constexpr std::chrono::seconds StreamMinimumHold{15};
			static constexpr std::chrono::seconds StreamPredictedMinimumHold{2};

			util::listener_manager<sqpack_view_entry_cache, void, const path_spec&, std::optional<std::chrono::steady_clock::duration>> OnStreamWindowPredicted;

			bool read(const entry_info& entry, bool streamed, uint64_t offset, std::span<uint8_t> out);

			void flush();

		private:
			using clock = std::chrono::steady_clock;

			struct buffer {
				std::mutex FillMtx;
				bool Filled = false;
				bool Failed = false;
				std::vector<uint8_t> Data;
			};

			struct reader {
				const entry_info* Entry = nullptr;
				std::shared_ptr<buffer> Buffer;
				clock::time_point LastRead;
			};

			struct stream_state {
				clock::time_point LastWindow;
				clock::duration LongestInterval{};
				bool Predicted = false;
				std::optional<clock::duration> PredictedInterval;
			};

			std::mutex m_mtx;
			std::map<const entry_info*, std::weak_ptr<buffer>> m_buffers;
			std::map<std::thread::id, reader> m_readers;
			std::map<const entry_info*, stream_state> m_streams;

			void expire_locked(clock::time_point now);
			bool read_streamed(const entry_info& entry, uint64_t offset);
		};

		const std::string DatExpac;
		const std::string DatName;

	private:
		std::map<path_spec, entry_info, path_spec::AllHashComparator> m_hashOnlyEntries;
		std::map<path_spec, entry_info, path_spec::FullPathComparator> m_fullEntries;

		std::vector<sqindex::segment_3_entry> m_sqpackIndexSegment3;
		std::vector<sqindex::segment_3_entry> m_sqpackIndex2Segment3;

		std::vector<std::shared_ptr<const stream>> m_originalData;

		entry_info* find_entry_mutable(const path_spec& pathSpec);

	public:
		util::listener_manager<generator, void, size_t, size_t> ProgressCallback;

		generator(std::string ex, std::string name, uint64_t maxFileSize = sqdata::header::MaxFileSize_MaxValue);

		void add(add_result& result, std::shared_ptr<packed_stream> provider, bool overwriteExisting);
		add_result add(std::shared_ptr<packed_stream> provider, bool overwriteExisting = true);
		add_result add_sqpack(const std::filesystem::path& indexPath, bool overwriteExisting = true, bool overwriteUnknownSegments = false, bool keepOriginalLayout = false);
		add_result add_sqpack(const reader& reader, bool overwriteExisting = true, bool overwriteUnknownSegments = false, bool keepOriginalLayout = false);
		add_result add_file(path_spec pathSpec, const std::filesystem::path& path, bool overwriteExisting = true);
		[[nodiscard]] const entry_info* find_entry(const path_spec& pathSpec) const;
		void reserve_space(path_spec pathSpec, uint32_t size);

		[[nodiscard]] sqpack_views export_to_views(bool strict, const std::shared_ptr<sqpack_view_entry_cache>& dataBuffer = nullptr, bool streamed = false);
		void export_to_files(const std::filesystem::path& dir, bool strict = false, size_t cores = std::thread::hardware_concurrency());

		[[nodiscard]] std::unique_ptr<default_base_stream> get(const path_spec& pathSpec) const;
		[[nodiscard]] std::vector<path_spec> all_path_spec() const;
	};
}

#endif
