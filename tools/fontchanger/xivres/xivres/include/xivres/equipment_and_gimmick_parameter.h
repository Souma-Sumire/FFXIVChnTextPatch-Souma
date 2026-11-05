#ifndef XIVRES_EQPGMP_H_
#define XIVRES_EQPGMP_H_

#include <cstdint>
#include <span>
#include <vector>

#include "stream.h"
#include "util.byte_order.h"
#include "util.span_cast.h"

namespace xivres {
	class equipment_and_gimmick_parameter_file {
		static constexpr size_t CountPerBlock = 160;

		std::vector<uint64_t> m_data;
		std::vector<size_t> m_populatedIndices;

	public:
		equipment_and_gimmick_parameter_file() { clear(); }

		equipment_and_gimmick_parameter_file(std::vector<uint64_t> data)
			: m_data(std::move(data)) {

			size_t populatedIndex = 0;
			for (size_t i = 0; i < 64; i++) {
				if (block_bits() & (uint64_t{1} << i))
					m_populatedIndices.push_back(populatedIndex++);
				else
					m_populatedIndices.push_back(SIZE_MAX);
			}
		}

		equipment_and_gimmick_parameter_file(const stream& strm) : equipment_and_gimmick_parameter_file(strm.read_vector<uint64_t>()) {
		}

		equipment_and_gimmick_parameter_file(const equipment_and_gimmick_parameter_file&) = default;
		equipment_and_gimmick_parameter_file(equipment_and_gimmick_parameter_file&&) noexcept = default;
		equipment_and_gimmick_parameter_file& operator=(const equipment_and_gimmick_parameter_file& file) = default;
		equipment_and_gimmick_parameter_file& operator=(equipment_and_gimmick_parameter_file&& file) noexcept = default;
		~equipment_and_gimmick_parameter_file() = default;

		void clear() {
			m_data.clear();
			m_populatedIndices.clear();
			m_data.resize(CountPerBlock);
			m_data[0] = 1;
			m_populatedIndices.resize(64, SIZE_MAX);
			m_populatedIndices[0] = 0;
		}

		[[nodiscard]] const std::vector<uint64_t>& data() const {
			return m_data;
		}

		[[nodiscard]] std::vector<uint8_t> data_bytes() const {
			std::vector<uint8_t> res(std::span(m_data).size_bytes());
			std::memcpy(&res[0], &m_data[0], res.size());
			return res;
		}

		[[nodiscard]] uint64_t& block_bits() {
			return m_data[0];
		}

		[[nodiscard]] const uint64_t& block_bits() const {
			return m_data[0];
		}

		[[nodiscard]] std::span<uint64_t> block(size_t index) {
			const auto populatedIndex = m_populatedIndices.at(index);
			if (populatedIndex == SIZE_MAX)
				return {};
			return std::span(m_data).subspan(CountPerBlock * populatedIndex, CountPerBlock);
		}

		[[nodiscard]] std::span<const uint64_t> block(size_t index) const {
			const auto populatedIndex = m_populatedIndices.at(index);
			if (populatedIndex == SIZE_MAX)
				return {};
			return std::span(m_data).subspan(CountPerBlock * populatedIndex, CountPerBlock);
		}

		[[nodiscard]] uint64_t& parameter(size_t primaryId) {
			const auto blockIndex = primaryId / CountPerBlock;
			const auto subIndex = primaryId % CountPerBlock;
			const auto block = this->block(blockIndex);
			if (block.empty())
				throw std::runtime_error("Must be expanded before accessing parameter for modification.");
			return block[subIndex];
		}

		[[nodiscard]] uint64_t parameter(size_t primaryId) const {
			return get_parameter(primaryId);
		}

		[[nodiscard]] uint64_t get_parameter(size_t primaryId) const {
			const auto blockIndex = primaryId / CountPerBlock;
			const auto subIndex = primaryId % CountPerBlock;
			const auto block = this->block(blockIndex);
			if (block.empty())
				return 0;
			return block[subIndex];
		}

		[[nodiscard]] std::span<uint8_t> paramter_bytes(size_t primaryId) {
			return util::span_cast<uint8_t>(m_data, primaryId, 8);
		}

		[[nodiscard]] std::span<const uint8_t> paramter_bytes(size_t primaryId) const {
			return util::span_cast<uint8_t>(m_data, primaryId, 8);
		}

		[[nodiscard]] equipment_and_gimmick_parameter_file expand_or_collapse(bool expand) const {
			std::vector<uint64_t> newData;
			newData.reserve(CountPerBlock * 64);

			uint64_t populatedBits = 0;

			size_t sourceIndex = 0, targetIndex = 0;
			for (size_t i = 0; i < 64; i++) {
				if (m_data[0] & (1ULL << i)) {
					const auto currentSourceIndex = sourceIndex;
					sourceIndex++;

					if (!expand) {
						bool isAllZeros = true;
						for (size_t j = currentSourceIndex * CountPerBlock, j_ = j + CountPerBlock; isAllZeros && j < j_; ++j) {
							isAllZeros = m_data[j] == 0;
						}
						if (isAllZeros)
							continue;
					}
					populatedBits |= 1ULL << i;
					newData.resize(newData.size() + CountPerBlock);
					std::copy_n(&m_data[currentSourceIndex * CountPerBlock], CountPerBlock, &newData[targetIndex * CountPerBlock]);
					targetIndex++;
				} else {
					if (expand) {
						populatedBits |= 1ULL << i;
						newData.resize(newData.size() + CountPerBlock);
						targetIndex++;
					}
				}
			}
			newData[0] = populatedBits;

			return {newData};
		}
	};

	using equipment_parameter_file = equipment_and_gimmick_parameter_file;
	using gimmmick_parameter_file = equipment_and_gimmick_parameter_file;
}

#endif
