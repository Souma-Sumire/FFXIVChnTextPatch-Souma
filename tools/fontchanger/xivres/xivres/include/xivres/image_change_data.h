#ifndef XIVRES_IMC_H_
#define XIVRES_IMC_H_

#include <cstdint>
#include <span>
#include <vector>

#include "stream.h"
#include "util.byte_order.h"

namespace xivres::image_change_data {
	enum class image_change_data_type : uint16_t {
		Unknown = 0,
		NonSet = 1,
		Set = 31
	};

	struct header {
		LE<uint16_t> SubsetCount;
		LE<image_change_data_type> Type;
	};

	struct entry {
		uint8_t MaterialId;
		uint8_t DecalId;
		LE<uint16_t> AttributeAndSound;
		uint8_t VfxId;
		uint8_t MaterialAnimationId;
	};

	class file {
		std::vector<uint8_t> m_data;

	public:
		file()
			: m_data(sizeof(image_change_data::header)) {
		}

		file(const stream& strm)
			: m_data(strm.read_vector<uint8_t>()) {
			if (m_data.size() < sizeof(image_change_data::header)) {
				m_data.clear();
				m_data.resize(sizeof(image_change_data::header));
			}
		}

		file(file&& file) noexcept
			: m_data(std::move(file.m_data)) {
			file.m_data.resize(sizeof(image_change_data::header));
		}

		file(const file& file)
			: m_data(file.m_data) {
		}

		file& operator=(file&& file) noexcept {
			m_data = std::move(file.m_data);
			file.m_data.resize(sizeof(image_change_data::header));
			return *this;
		}

		file& operator=(const file& file) {
			m_data = file.m_data;
			return *this;
		}

		~file() = default;

		[[nodiscard]] const std::vector<uint8_t>& data() const {
			return m_data;
		}

		[[nodiscard]] size_t entry_count_per_set() const {
			size_t entryCountPerSet = 0;
			for (size_t i = 0; i < 16; i++) {
				if (static_cast<uint16_t>(*header().Type) & (1 << i))
					entryCountPerSet++;
			}
			return entryCountPerSet;
		}

		[[nodiscard]] header& header() {
			return *reinterpret_cast<image_change_data::header*>(m_data.data());
		}

		[[nodiscard]] const image_change_data::header& header() const {
			return *reinterpret_cast<const image_change_data::header*>(m_data.data());
		}

		[[nodiscard]] entry& entry(size_t index) {
			return reinterpret_cast<image_change_data::entry*>(m_data.data() + sizeof(image_change_data::header))[index];
		}

		[[nodiscard]] const image_change_data::entry& entry(size_t index) const {
			return reinterpret_cast<const image_change_data::entry*>(m_data.data() + sizeof(image_change_data::header))[index];
		}

		[[nodiscard]] std::span<image_change_data::entry> entries() {
			return { reinterpret_cast<image_change_data::entry*>(m_data.data() + sizeof(image_change_data::header)), (m_data.size() - sizeof(image_change_data::header)) / sizeof(image_change_data::entry) };
		}

		[[nodiscard]] std::span<const image_change_data::entry> entries() const {
			return { reinterpret_cast<const image_change_data::entry*>(m_data.data() + sizeof(image_change_data::header)), (m_data.size() - sizeof(image_change_data::header)) / sizeof(image_change_data::entry) };
		}

		void resize(size_t subsetCount) {
			if (subsetCount > UINT16_MAX)
				throw std::range_error("up to 64k subsets are supported");
			const auto countPerSet = entry_count_per_set();
			if (subsetCount > header().SubsetCount && header().SubsetCount > 0) {
				m_data.resize(sizeof(image_change_data::header) + sizeof(image_change_data::entry) * countPerSet * (size_t{ 1 } + header().SubsetCount));
				m_data.reserve(sizeof(image_change_data::header) + sizeof(image_change_data::entry) * countPerSet * (1 + subsetCount));
				for (auto i = *header().SubsetCount; i < subsetCount; ++i) {
					m_data.insert(m_data.end(), &m_data[sizeof(image_change_data::header)], &m_data[sizeof(image_change_data::header) + sizeof(image_change_data::entry) * countPerSet]);
				}
			} else {
				m_data.resize(sizeof(image_change_data::header) + sizeof(image_change_data::entry) * countPerSet * (1 + subsetCount));
			}
			header().SubsetCount = static_cast<uint16_t>(subsetCount);
		}

		void resize_if_needed(size_t minSubsetCount) {
			if (minSubsetCount > header().SubsetCount)
				resize(minSubsetCount);
		}
	};
}

#endif
