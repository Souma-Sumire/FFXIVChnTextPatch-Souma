#ifndef XIVRES_EQDP_H_
#define XIVRES_EQDP_H_

#include <cstdint>
#include <span>
#include <vector>

#include "util.byte_order.h"
#include "util.span_cast.h"

#include "common.h"
#include "stream.h"

namespace xivres {
	struct equipment_deformer_parameter_header {
		LE<uint16_t> Identifier;
		LE<uint16_t> BlockMemberCount;
		LE<uint16_t> BlockCount;
	};

	class equipment_deformer_parameter_file {
	protected:
		std::vector<uint8_t> m_data;

	public:
		equipment_deformer_parameter_file()
			: m_data(sizeof equipment_deformer_parameter_header) {
		}

		equipment_deformer_parameter_file(std::vector<uint8_t> data)
			: m_data(std::move(data)) {
		}

		equipment_deformer_parameter_file(const stream& strm)
			: m_data(strm.read_vector<uint8_t>()) {
		}

		equipment_deformer_parameter_file(equipment_deformer_parameter_file&& file) noexcept
			: m_data(std::move(file.m_data)) {
			file.m_data.resize(sizeof equipment_deformer_parameter_header);
		}

		equipment_deformer_parameter_file(const equipment_deformer_parameter_file& file)
			: m_data(file.m_data) {
		}

		equipment_deformer_parameter_file& operator=(equipment_deformer_parameter_file&& file) noexcept {
			m_data = std::move(file.m_data);
			file.m_data.resize(sizeof equipment_deformer_parameter_header);
			return *this;
		}

		equipment_deformer_parameter_file& operator=(const equipment_deformer_parameter_file& file) {
			m_data = file.m_data;
			return *this;
		}

		~equipment_deformer_parameter_file() = default;

		[[nodiscard]] const std::vector<uint8_t>& data() const {
			return m_data;
		}

		[[nodiscard]] equipment_deformer_parameter_header& header() {
			return *reinterpret_cast<equipment_deformer_parameter_header*>(m_data.data());
		}

		[[nodiscard]] const equipment_deformer_parameter_header& header() const {
			return *reinterpret_cast<const equipment_deformer_parameter_header*>(m_data.data());
		}

		[[nodiscard]] std::span<uint16_t> indices() {
			return {reinterpret_cast<uint16_t*>(m_data.data() + sizeof equipment_deformer_parameter_header), header().BlockCount};
		}

		[[nodiscard]] std::span<const uint16_t> indices() const {
			return {reinterpret_cast<const uint16_t*>(m_data.data() + sizeof equipment_deformer_parameter_header), header().BlockCount};
		}

		[[nodiscard]] size_t base_offset() const {
			return sizeof equipment_deformer_parameter_header + indices().size_bytes();
		}

		[[nodiscard]] std::span<uint16_t> body() {
			return {reinterpret_cast<uint16_t*>(m_data.data() + base_offset()), static_cast<size_t>(header().BlockCount * header().BlockMemberCount)};
		}

		[[nodiscard]] std::span<const uint16_t> body() const {
			return {reinterpret_cast<const uint16_t*>(m_data.data() + base_offset()), static_cast<size_t>(header().BlockCount * header().BlockMemberCount)};
		}

		[[nodiscard]] std::span<uint16_t> block(size_t blockId) {
			const auto index = indices()[blockId];
			if (index == UINT16_MAX)
				return {};

			return body().subspan(index, header().BlockMemberCount);
		}

		[[nodiscard]] std::span<const uint16_t> block(size_t blockId) const {
			const auto index = indices()[blockId];
			if (index == UINT16_MAX)
				return {};

			return body().subspan(index, header().BlockMemberCount);
		}

		[[nodiscard]] uint16_t& setinfo(size_t setId) {
			const auto b = block(setId / header().BlockMemberCount);
			if (b.empty())
				throw std::runtime_error("Must be expanded before accessing set for modification.");
			return b[setId % header().BlockMemberCount];
		}

		[[nodiscard]] uint16_t setinfo(size_t setId) const {
			return get_setinfo(setId);
		}

		[[nodiscard]] uint16_t get_setinfo(size_t setId) const {
			const auto b = block(setId / header().BlockMemberCount);
			if (b.empty())
				return 0;
			return b[setId % header().BlockMemberCount];
		}

		void expand_or_collapse(bool expand) {
			const auto baseOffset = base_offset();
			const auto& header = this->header();
			const auto& body = this->body();
			const auto indices = this->indices();

			std::vector<uint8_t> newData;
			newData.resize(baseOffset + sizeof(uint16_t) * header.BlockCount * header.BlockMemberCount);
			*reinterpret_cast<equipment_deformer_parameter_header*>(&newData[0]) = header;
			const auto newIndices = util::span_cast<uint16_t>(newData, sizeof header, header.BlockCount);
			const auto newBody = util::span_cast<uint16_t>(newData, baseOffset, size_t{1} * header.BlockCount * header.BlockMemberCount);
			uint16_t newBodyIndex = 0;

			for (size_t i = 0; i < indices.size(); ++i) {
				if (expand) {
					newIndices[i] = newBodyIndex;
					newBodyIndex += header.BlockMemberCount;
					if (indices[i] == UINT16_MAX)
						continue;
					std::copy_n(&body[indices[i]], header.BlockMemberCount, &newBody[newIndices[i]]);

				} else {
					auto isAllZeros = true;
					for (size_t j = indices[i], j_ = j + header.BlockMemberCount; isAllZeros && j < j_; j++) {
						isAllZeros = body[j] == 0;
					}
					if (isAllZeros) {
						newIndices[i] = UINT16_MAX;
					} else {
						newIndices[i] = newBodyIndex;
						newBodyIndex += header.BlockMemberCount;
						if (indices[i] == UINT16_MAX)
							continue;
						std::copy_n(&body[indices[i]], header.BlockMemberCount, &newBody[newIndices[i]]);
					}
				}
			}
			newData.resize(xivres::align<size_t>(baseOffset + newBodyIndex * sizeof(uint16_t), 512).Alloc);
			m_data.swap(newData);
		}
	};
}

#endif
