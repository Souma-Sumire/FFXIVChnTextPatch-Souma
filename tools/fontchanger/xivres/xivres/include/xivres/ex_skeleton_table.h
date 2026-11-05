#ifndef XIVRES_EST_H_
#define XIVRES_EST_H_

#include <cstdint>
#include <map>
#include <span>
#include <vector>

#include "stream.h"

namespace xivres {
	class ex_skeleton_table_file {
	public:
		struct descriptor {
			uint16_t SetId;
			uint16_t RaceCode;

			bool operator<(const descriptor& r) const {
				return RaceCode == r.RaceCode ? SetId < r.SetId : RaceCode < r.RaceCode;
			}

			bool operator<=(const descriptor& r) const {
				return RaceCode == r.RaceCode ? SetId <= r.SetId : RaceCode <= r.RaceCode;
			}

			bool operator>(const descriptor& r) const {
				return RaceCode == r.RaceCode ? SetId > r.SetId : RaceCode > r.RaceCode;
			}

			bool operator>=(const descriptor& r) const {
				return RaceCode == r.RaceCode ? SetId >= r.SetId : RaceCode >= r.RaceCode;
			}

			bool operator==(const descriptor& r) const {
				return SetId == r.SetId && RaceCode == r.RaceCode;
			}

			bool operator!=(const descriptor& r) const {
				return SetId != r.SetId || RaceCode != r.RaceCode;
			}
		};

	private:
		std::vector<uint8_t> m_data;

	public:
		ex_skeleton_table_file()
			: m_data(4) {
		}

		ex_skeleton_table_file(std::vector<uint8_t> data)
			: m_data(std::move(data)) {
		}

		ex_skeleton_table_file(const stream& strm)
			: m_data(strm.read_vector<uint8_t>()) {
		}

		ex_skeleton_table_file(const std::map<descriptor, uint16_t>& pairs)
			: m_data(4) {
			from_pairs(pairs);
		}

		ex_skeleton_table_file(ex_skeleton_table_file&& file) noexcept
			: m_data(std::move(file.m_data)) {
			file.m_data.resize(4);
		}

		ex_skeleton_table_file(const ex_skeleton_table_file& file)
			: m_data(file.m_data) {
		}

		ex_skeleton_table_file& operator=(ex_skeleton_table_file&& file) noexcept {
			m_data = std::move(file.m_data);
			file.m_data.resize(4);
			return *this;
		}

		ex_skeleton_table_file& operator=(const ex_skeleton_table_file& file) {
			m_data = file.m_data;
			return *this;
		}

		~ex_skeleton_table_file() = default;

		[[nodiscard]] const std::vector<uint8_t>& data() const {
			return m_data;
		}

		[[nodiscard]] uint32_t& count() {
			return *reinterpret_cast<uint32_t*>(&m_data[0]);
		}

		[[nodiscard]] const uint32_t& count() const {
			return *reinterpret_cast<const uint32_t*>(&m_data[0]);
		}

		[[nodiscard]] descriptor& descriptor_at(size_t index) {
			return reinterpret_cast<descriptor*>(&m_data[4])[index];
		}

		[[nodiscard]] const descriptor& descriptor_at(size_t index) const {
			return reinterpret_cast<const descriptor*>(&m_data[4])[index];
		}

		[[nodiscard]] std::span<descriptor> descriptors() {
			return {reinterpret_cast<descriptor*>(&m_data[4]), count()};
		}

		[[nodiscard]] std::span<const descriptor> descriptors() const {
			return {reinterpret_cast<const descriptor*>(&m_data[4]), count()};
		}

		[[nodiscard]] uint16_t& skel_id(size_t index) {
			return reinterpret_cast<uint16_t*>(&m_data[4 + descriptors().size_bytes()])[index];
		}

		[[nodiscard]] const uint16_t& skel_id(size_t index) const {
			return reinterpret_cast<const uint16_t*>(&m_data[4 + descriptors().size_bytes()])[index];
		}

		[[nodiscard]] std::span<uint16_t> skel_ids() {
			return {reinterpret_cast<uint16_t*>(&m_data[4 + descriptors().size_bytes()]), count()};
		}

		[[nodiscard]] std::span<const uint16_t> skel_ids() const {
			return {reinterpret_cast<const uint16_t*>(&m_data[4 + descriptors().size_bytes()]), count()};
		}

		[[nodiscard]] std::map<descriptor, uint16_t> to_pairs() const {
			std::map<descriptor, uint16_t> res;
			for (size_t i = 0, i_ = count(); i < i_; ++i)
				res.emplace(descriptor_at(i), skel_id(i));
			return res;
		}

		void from_pairs(const std::map<descriptor, uint16_t>& pairs) {
			m_data.resize(4 + pairs.size() * 6);
			count() = static_cast<uint32_t>(pairs.size());

			size_t i = 0;
			for (const auto& [desc, skelId] : pairs) {
				descriptor_at(i) = desc;
				skel_id(i) = skelId;
				i++;
			}
		}
	};
}

#endif
