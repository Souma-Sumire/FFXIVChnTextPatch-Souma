#ifndef XIVRES_INSTALLATION_H_
#define XIVRES_INSTALLATION_H_

#include <filesystem>
#include <map>
#include <mutex>
#include <optional>

#include "sqpack.reader.h"

namespace xivres::excel {
	class reader;
}

namespace xivres {
	enum class font_type {
		undefined,
		font,
		font_lobby,
		chn_axis,
		krn_axis,
		tc_axis
	};

	class installation {
		const std::filesystem::path m_gamePath;
		mutable std::map<uint32_t, std::optional<sqpack::reader>> m_readers;
		mutable std::map<uint32_t, std::mutex> m_populateMtx;

	public:
		installation(std::filesystem::path gamePath);

		[[nodiscard]] std::shared_ptr<packed_stream> get_file_packed(const path_spec& pathSpec) const;

		[[nodiscard]] std::shared_ptr<unpacked_stream> get_file(const path_spec& pathSpec, std::span<uint8_t> obfuscatedHeaderRewrite = {}) const;

		[[nodiscard]] std::vector<uint32_t> get_sqpack_ids() const;

		[[nodiscard]] const sqpack::reader& get_sqpack(const path_spec& pathSpec) const;

		[[nodiscard]] const sqpack::reader& get_sqpack(uint32_t packId) const;

		[[nodiscard]] const sqpack::reader& get_sqpack(uint8_t categoryId, uint8_t expacId, uint8_t partId) const;

		[[nodiscard]] excel::reader get_excel(const std::string& name) const;

		[[nodiscard]] std::string get_version(uint8_t expac) const;

		void preload_all_sqpacks() const;

		static std::filesystem::path find_installation_global();
		
		static std::filesystem::path find_installation_china();
		
		static std::filesystem::path find_installation_korea();
	};
}

#endif
