#include "../include/xivres/textools.h"
#include "../include/xivres/util.thread_pool.h"
#include "../include/xivres/util.zlib_wrapper.h"

#include <set>

#include "include/xivres/util.unicode.h"

#ifdef _WIN32
#include <minizip/iowin32.h>
#else
#include <minizip/ioapi.h>
#endif

using namespace std::string_literals;

namespace {
	template<typename T>
	T JsonValueOrDefault(const nlohmann::json& json, const char* key, T defaultValue, T nullDefaultValue) {
		if (const auto it = json.find(key); it != json.end()) {
			if (it->is_null())
				return nullDefaultValue;
			return it->get<T>();
		}
		return defaultValue;
	}
}

void xivres::textools::to_json(nlohmann::json& j, const mod_pack& p) {
	j = nlohmann::json::object({
		{"Name", p.Name},
		{"Author", p.Author},
		{"Version", p.Version},
		{"Url", p.Url},
	});
}

void xivres::textools::from_json(const nlohmann::json& j, mod_pack& p) {
	if (j.is_null())
		return;
	if (!j.is_object())
		throw bad_data_error("ModPackEntry must be an object");

	p.Name = JsonValueOrDefault(j, "Name", ""s, ""s);
	p.Author = JsonValueOrDefault(j, "Author", ""s, ""s);
	p.Version = JsonValueOrDefault(j, "Version", ""s, ""s);
	p.Url = JsonValueOrDefault(j, "Url", ""s, ""s);
}

void xivres::textools::to_json(nlohmann::json& j, const mods_json& p) {
	j = nlohmann::json::object({
		{"Name", p.Name},
		{"Category", p.Category},
		{"FullPath", p.FullPath},
		{"ModOffset", p.ModOffset},
		{"ModSize", p.ModSize},
		{"DatFile", p.DatFile},
		{"IsDefault", p.IsDefault},
	});
	if (p.ModPack)
		j["ModPackEntry"] = *p.ModPack;
}

void xivres::textools::from_json(const nlohmann::json& j, mods_json& p) {
	if (!j.is_object())
		throw bad_data_error("ModEntry must be an object");

	p.Name = JsonValueOrDefault(j, "Name", ""s, ""s);
	p.Category = JsonValueOrDefault(j, "Category", ""s, ""s);
	p.FullPath = JsonValueOrDefault(j, "FullPath", ""s, ""s);
	p.ModOffset = JsonValueOrDefault(j, "ModOffset", 0LL, 0LL);
	p.ModSize = JsonValueOrDefault(j, "ModSize", 0LL, 0LL);
	p.DatFile = JsonValueOrDefault(j, "DatFile", ""s, ""s);
	p.IsDefault = JsonValueOrDefault(j, "IsDefault", false, false);
	if (const auto it = j.find("ModPackEntry"); it != j.end() && !it->is_null())
		p.ModPack = it->get<mod_pack>();
}

void xivres::textools::mod_pack_page::to_json(nlohmann::json& j, const mod_option_json& p) {
	j = nlohmann::json::object({
		{"Name", p.Name},
		{"Description", p.Description},
		{"ImagePath", p.ImagePath},
		{"ModsJsons", p.ModsJsons},
		{"GroupName", p.GroupName},
		{"SelectionType", p.SelectionType},
		{"IsChecked", p.IsChecked},
	});
}

void xivres::textools::mod_pack_page::from_json(const nlohmann::json& j, mod_option_json& p) {
	if (!j.is_object())
		throw bad_data_error("Option must be an object");

	p.Name = JsonValueOrDefault(j, "Name", ""s, ""s);
	p.Description = JsonValueOrDefault(j, "Description", ""s, ""s);
	p.ImagePath = JsonValueOrDefault(j, "ImagePath", ""s, ""s);
	if (const auto it = j.find("ModsJsons"); it != j.end() && !it->is_null())
		p.ModsJsons = it->get<decltype(p.ModsJsons)>();
	p.GroupName = JsonValueOrDefault(j, "GroupName", ""s, ""s);
	p.SelectionType = JsonValueOrDefault(j, "SelectionType", ""s, ""s);
	p.IsChecked = JsonValueOrDefault(j, "IsChecked", false, false);
}

void xivres::textools::mod_pack_page::to_json(nlohmann::json& j, const mod_group_json& p) {
	j = nlohmann::json::object({
		{"GroupName", p.GroupName},
		{"SelectionType", p.SelectionType},
		{"OptionList", p.OptionList},
	});
}

void xivres::textools::mod_pack_page::from_json(const nlohmann::json& j, mod_group_json& p) {
	if (!j.is_object())
		throw bad_data_error("Option must be an object");

	p.GroupName = JsonValueOrDefault(j, "GroupName", ""s, ""s);
	p.SelectionType = JsonValueOrDefault(j, "SelectionType", ""s, ""s);
	if (const auto it = j.find("OptionList"); it != j.end() && !it->is_null())
		p.OptionList = it->get<decltype(p.OptionList)>();
}

void xivres::textools::mod_pack_page::to_json(nlohmann::json& j, const mod_pack_page_json& p) {
	j = nlohmann::json::object({
		{"PageIndex", p.PageIndex},
		{"ModGroups", p.ModGroups},
	});
}

void xivres::textools::mod_pack_page::from_json(const nlohmann::json& j, mod_pack_page_json& p) {
	if (!j.is_object())
		throw bad_data_error("Option must be an object");

	p.PageIndex = JsonValueOrDefault(j, "PageIndex", 0, 0);
	if (const auto it = j.find("ModGroups"); it != j.end() && !it->is_null())
		p.ModGroups = it->get<decltype(p.ModGroups)>();
}

void xivres::textools::to_json(nlohmann::json& j, const mod_pack_json& p) {
	j = nlohmann::json::object({
		{"MinimumFrameworkVersion", p.MinimumFrameworkVersion},
		{"TTMPVersion", p.TTMPVersion},
		{"Name", p.Name},
		{"Author", p.Author},
		{"Version", p.Version},
		{"Description", p.Description},
		{"Url", p.Url},
		{"ModPackPages", p.ModPackPages},
		{"SimpleModsList", p.SimpleModsList},
	});
}

void xivres::textools::from_json(const nlohmann::json& j, mod_pack_json& p) {
	if (!j.is_object())
		throw bad_data_error("TTMPL must be an object");

	p.MinimumFrameworkVersion = JsonValueOrDefault(j, "MinimumFrameworkVersion", ""s, ""s);
	p.TTMPVersion = JsonValueOrDefault(j, "TTMPVersion", ""s, ""s);
	p.Name = JsonValueOrDefault(j, "Name", ""s, ""s);
	p.Author = JsonValueOrDefault(j, "Author", ""s, ""s);
	p.Version = JsonValueOrDefault(j, "Version", ""s, ""s);
	p.Description = JsonValueOrDefault(j, "Description", ""s, ""s);
	p.Url = JsonValueOrDefault(j, "Url", ""s, ""s);
	if (const auto it = j.find("ModPackPages"); it != j.end() && !it->is_null())
		p.ModPackPages = it->get<decltype(p.ModPackPages)>();
	if (const auto it = j.find("SimpleModsList"); it != j.end() && !it->is_null())
		p.SimpleModsList = it->get<decltype(p.SimpleModsList)>();
}

xivres::textools::mod_pack_json xivres::textools::mod_pack_json::from_stream(const stream& strm) {
	const auto size = strm.size();
	if (size > 16LL * 1024 * 1024)
		throw bad_data_error("File too big (>16MB).");

	std::string buf(static_cast<size_t>(size), 0);
	strm.read_fully(0, buf.data(), size);

	std::istringstream in(buf);
	mod_pack_json res;
	while (!in.eof()) {
		nlohmann::json j;
		try {
			in >> j;
		} catch (...) {
			if (in.eof())
				break;
			throw;
		}
		if (j.find("ModOffset") != j.end())
			res.SimpleModsList.emplace_back(j.get<mods_json>());
		else
			return j.get<mod_pack_json>();
	}
	return res;
}

void xivres::textools::mod_pack_json::for_each(std::function<void(mods_json&)> cb, const nlohmann::json& choices) {
	for_each_breakable([&cb](mods_json& entry) { cb(entry); return true; }, choices);
}

void xivres::textools::mod_pack_json::for_each(std::function<void(const mods_json&)> cb, const nlohmann::json& choices) const {
	for_each_breakable([&cb](const mods_json& entry) { cb(entry); return true; }, choices);
}

bool xivres::textools::mod_pack_json::for_each_breakable(const std::function<bool(mods_json&)>& cb, const nlohmann::json& choices) {
	static const nlohmann::json emptyChoices;

	for (auto& entry : SimpleModsList)
		if (!cb(entry))
			return false;

	for (size_t pageIndex = 0; pageIndex < ModPackPages.size(); pageIndex++) {
		auto& modPackPage = ModPackPages[pageIndex];
		const auto& pageConf = choices.is_array() && pageIndex < choices.size() ? choices[pageIndex] : emptyChoices;
		for (size_t modGroupIndex = 0; modGroupIndex < modPackPage.ModGroups.size(); modGroupIndex++) {
			auto& modGroup = modPackPage.ModGroups[modGroupIndex];

			std::set<size_t> indices;
			if (!pageConf.is_array() || pageConf.size() <= modGroupIndex) {
				for (size_t i = 0; i < modGroup.OptionList.size(); ++i)
					indices.insert(i);
			} else if (pageConf.at(modGroupIndex).is_array()) {
				const auto tmp = pageConf.at(modGroupIndex).get<std::vector<size_t>>();
				indices.insert(tmp.begin(), tmp.end());
			} else {
				indices.insert(pageConf.at(modGroupIndex).get<size_t>());
			}

			for (const auto k : indices) {
				if (k >= modGroup.OptionList.size())
					continue;

				auto& option = modGroup.OptionList[k];
				for (auto& modJson : option.ModsJsons)
					if (!cb(modJson))
						return false;
			}
		}
	}
	return true;
}

bool xivres::textools::mod_pack_json::for_each_breakable(const std::function<bool(const mods_json&)>& cb, const nlohmann::json& choices) const {
	static const nlohmann::json emptyChoices;

	for (auto& entry : SimpleModsList)
		if (!cb(entry))
			return false;

	for (size_t pageIndex = 0; pageIndex < ModPackPages.size(); pageIndex++) {
		auto& modPackPage = ModPackPages[pageIndex];
		const auto& pageConf = choices.is_array() && pageIndex < choices.size() ? choices[pageIndex] : emptyChoices;
		for (size_t modGroupIndex = 0; modGroupIndex < modPackPage.ModGroups.size(); modGroupIndex++) {
			auto& modGroup = modPackPage.ModGroups[modGroupIndex];

			std::set<size_t> indices;
			if (!pageConf.is_array() || pageConf.size() <= modGroupIndex) {
				for (size_t i = 0; i < modGroup.OptionList.size(); ++i)
					indices.insert(i);
			} else if (pageConf.at(modGroupIndex).is_array()) {
				const auto tmp = pageConf.at(modGroupIndex).get<std::vector<size_t>>();
				indices.insert(tmp.begin(), tmp.end());
			} else {
				indices.insert(pageConf.at(modGroupIndex).get<size_t>());
			}

			for (const auto k : indices) {
				if (k >= modGroup.OptionList.size())
					continue;

				auto& option = modGroup.OptionList[k];
				for (auto& modJson : option.ModsJsons)
					if (!cb(modJson))
						return false;
			}
		}
	}
	return true;
}

bool xivres::textools::mods_json::is_textools_metadata() const {
	if (FullPath.length() < 5)
		return false;
	auto metaExt = FullPath.substr(FullPath.length() - 5);
	for (auto& c : metaExt) {
		if (c < 128)
			c = std::tolower(c);
	}
	return metaExt == ".meta";
}

const srell::u8cregex xivres::textools::metafile::CharacterMetaPathTest(
	"^(?<FullPathPrefix>chara"
	"/(?<PrimaryType>[a-z]+)"
	"/(?<PrimaryCode>[a-z])(?<PrimaryId>[0-9]+)"
	"(?:/obj"
	"/(?<SecondaryType>[a-z]+)"
	"/(?<SecondaryCode>[a-z])(?<SecondaryId>[0-9]+))?"
	"/).*?(?:_(?<Slot>[a-z]{3}))?\\.meta$"
	, srell::u8cregex::icase);
const srell::u8cregex xivres::textools::metafile::HousingMetaPathTest(
	"^(?<FullPathPrefix>bgcommon"
	"/hou"
	"/(?<PrimaryType>[a-z]+)"
	"/general"
	"/(?<PrimaryId>[0-9]+)"
	"/).*?\\.meta$"
	, srell::u8cregex::icase);

xivres::textools::metafile::metafile(std::string gamePath, const stream& stream)
	: Data(stream.read_vector<uint8_t>())
	, Version(*reinterpret_cast<const uint32_t*>(&Data[0]))
	, TargetPath(std::move(gamePath))
	, SourcePath(reinterpret_cast<const char*>(&Data[sizeof Version]))
	, Header(*reinterpret_cast<const meta_header*>(&Data[sizeof Version + SourcePath.size() + 1]))
	, AllEntries(span_cast<entry_locator>(Data, Header.FirstEntryLocatorOffset, Header.EntryCount)) {
	if (srell::u8csmatch matches;
		regex_search(TargetPath, matches, CharacterMetaPathTest)) {
		PrimaryType = matches.str("PrimaryType");
		PrimaryId = static_cast<uint16_t>(std::strtol(matches.str("PrimaryId").c_str(), nullptr, 10));
		SecondaryType = matches.str("SecondaryType");
		SecondaryId = static_cast<uint16_t>(std::strtol(matches.str("SecondaryId").c_str(), nullptr, 10));
		if (SecondaryType.empty())
			TargetImcPath = matches.str("FullPathPrefix") + matches.str("PrimaryCode") + matches.str("PrimaryId") + ".imc";
		else
			TargetImcPath = matches.str("FullPathPrefix") + matches.str("SecondaryCode") + matches.str("SecondaryId") + ".imc";
		for (auto& c : PrimaryType) {
			if (c < 128)
				c = std::tolower(c);
		}
		for (auto& c : SecondaryType) {
			if (c < 128)
				c = std::tolower(c);
		}
		if (PrimaryType == "equipment") {
			auto slot = matches.str("Slot");
			for (auto& c : slot) {
				if (c < 128)
					c = std::tolower(c);
			}
			if (0 == slot.compare("met"))
				ItemType = item_types::Equipment, SlotIndex = 0, EqpEntrySize = 3, EqpEntryOffset = 5, EstType = est_types::Head;
			else if (0 == slot.compare("top"))
				ItemType = item_types::Equipment, SlotIndex = 1, EqpEntrySize = 2, EqpEntryOffset = 0, EstType = est_types::Body;
			else if (0 == slot.compare("glv"))
				ItemType = item_types::Equipment, SlotIndex = 2, EqpEntrySize = 1, EqpEntryOffset = 3;
			else if (0 == slot.compare("dwn"))
				ItemType = item_types::Equipment, SlotIndex = 3, EqpEntrySize = 1, EqpEntryOffset = 2;
			else if (0 == slot.compare("sho"))
				ItemType = item_types::Equipment, SlotIndex = 4, EqpEntrySize = 1, EqpEntryOffset = 4;
			else if (0 == slot.compare("ear"))
				ItemType = item_types::Accessory, SlotIndex = 0;
			else if (0 == slot.compare("nek"))
				ItemType = item_types::Accessory, SlotIndex = 1;
			else if (0 == slot.compare("wrs"))
				ItemType = item_types::Accessory, SlotIndex = 2;
			else if (0 == slot.compare("rir"))
				ItemType = item_types::Accessory, SlotIndex = 3;
			else if (0 == slot.compare("ril"))
				ItemType = item_types::Accessory, SlotIndex = 4;
		} else if (PrimaryType == "human") {
			if (SecondaryType == "hair")
				EstType = est_types::Hair;
			else if (SecondaryType == "face")
				EstType = est_types::Face;
		}

	} else if (regex_search(TargetPath, matches, HousingMetaPathTest)) {
		PrimaryType = matches.str("PrimaryType");
		PrimaryId = static_cast<uint16_t>(std::strtol(matches.str("PrimaryId").c_str(), nullptr, 10));
		ItemType = item_types::Housing;

	} else {
		throw bad_data_error("Unsupported meta file");
	}

	if (srell::u8csmatch matches;
		regex_search(SourcePath, matches, CharacterMetaPathTest)) {
		if (SecondaryType.empty())
			SourceImcPath = matches.str("FullPathPrefix") + matches.str("PrimaryCode") + matches.str("PrimaryId") + ".imc";
		else
			SourceImcPath = matches.str("FullPathPrefix") + matches.str("SecondaryCode") + matches.str("SecondaryId") + ".imc";

	} else {
		throw bad_data_error("Unsupported meta file");
	}
}

void xivres::textools::metafile::apply_image_change_data_edits(const std::function<image_change_data::file&()>& reader) const {
	if (const auto imcedit = get_span<image_change_data::entry>(meta_types::Imc); !imcedit.empty()) {
		auto& imc = reader();
		using imc_t = image_change_data::image_change_data_type;
		if (imc.header().Type == imc_t::Unknown) {
			const auto& typeStr = SecondaryType.empty() ? PrimaryType : SecondaryType;
			imc.header().Type = typeStr == "equipment" || typeStr == "accessory" ? imc_t::Set : imc_t::NonSet;
		}
		imc.resize_if_needed(imcedit.size() - 1);
		for (size_t i = 0; i < imcedit.size(); ++i) {
			imc.entry(i * imc.entry_count_per_set() + SlotIndex) = imcedit[i];
		}
	}
}

void xivres::textools::metafile::apply_equipment_deformer_parameter_edits(const std::function<equipment_deformer_parameter_file&(item_types, uint32_t)>& reader) const {
	if (const auto eqdpedit = get_span<equipment_deformer_parameter_entry>(meta_types::Eqdp); !eqdpedit.empty()) {
		for (const auto& v : eqdpedit) {
			auto& eqdp = reader(ItemType, v.RaceCode);
			auto& target = eqdp.setinfo(PrimaryId);
			target &= ~(0b11 << (SlotIndex * 2));
			target |= v.Value << (SlotIndex * 2);
		}
	}
}

bool xivres::textools::metafile::has_equipment_parameter_edits() const {
	return !get_span<uint8_t>(meta_types::Eqp).empty();
}

void xivres::textools::metafile::apply_equipment_parameter_edits(equipment_parameter_file& eqp) const {
	if (const auto eqpedit = get_span<uint8_t>(meta_types::Eqp); !eqpedit.empty()) {
		if (eqpedit.size() != EqpEntrySize)
			throw bad_data_error(std::format("expected {}b for eqp; got {}b", EqpEntrySize, eqpedit.size()));
		std::copy_n(&eqpedit[0], EqpEntrySize, &eqp.paramter_bytes(PrimaryId)[EqpEntryOffset]);
	}
}

bool xivres::textools::metafile::has_gimmick_parameter_edits() const {
	return !get_span<uint8_t>(meta_types::Gmp).empty();
}

void xivres::textools::metafile::apply_gimmick_parameter_edits(gimmmick_parameter_file& gmp) const {
	if (const auto gmpedit = get_span<uint8_t>(meta_types::Gmp); !gmpedit.empty()) {
		if (gmpedit.size() != sizeof uint64_t)
			throw bad_data_error(std::format("gmp data must be 8 bytes; {} byte(s) given", gmpedit.size()));
		std::copy_n(&gmpedit[0], gmpedit.size(), &gmp.paramter_bytes(PrimaryId)[0]);
	}
}

void xivres::textools::metafile::apply_ex_skeleton_table_edits(ex_skeleton_table_file& est) const {
	if (const auto estedit = get_span<ex_skeleton_table_entry>(meta_types::Est); !estedit.empty()) {
		auto estpairs = est.to_pairs();
		for (const auto& v : estedit) {
			const auto key = ex_skeleton_table_file::descriptor{.SetId = v.SetId, .RaceCode = v.RaceCode};
			if (v.SkelId == 0)
				estpairs.erase(key);
			else
				estpairs.insert_or_assign(key, v.SkelId);
		}
		est.from_pairs(estpairs);
	}
}

xivres::textools::simple_ttmp2_writer::simple_ttmp2_writer(std::filesystem::path path) {
	open(std::move(path));
}

void xivres::textools::swap(simple_ttmp2_writer& l, simple_ttmp2_writer& r) noexcept {
	if (&l == &r)
		return;

	std::swap(l.m_path, r.m_path);
	std::swap(l.m_pathTemp, r.m_pathTemp);
	std::swap(l.m_zffunc, r.m_zffunc);
	std::swap(l.m_zf, r.m_zf);
	std::swap(l.m_ttmpl, r.m_ttmpl);
	std::swap(l.m_zstream, r.m_zstream);
	std::swap(l.m_packed, r.m_packed);
	std::swap(l.m_errorState, r.m_errorState);
}

void xivres::textools::simple_ttmp2_writer::begin_packed(int compressionLevel) {
	if (!m_zf)
		throw std::logic_error("No file is open");
	if (m_packed)
		throw std::logic_error("Packing has already begun");

	constexpr zip_fileinfo zi{};

	if (const auto err = zipOpenNewFileInZip3_64(
		m_zf, "TTMPD.mpd", &zi,
		nullptr, 0, nullptr, 0,
		nullptr,
		compressionLevel != Z_NO_COMPRESSION ? Z_DEFLATED : 0, compressionLevel,
		1,
		-MAX_WBITS, DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY,
		nullptr, 0, 1))
		throw std::runtime_error(std::format("zipOpenNewFileInZip3_64 error({})", err));

	m_packed.emplace(std::make_unique<std::mutex>(), std::nullopt, false, crc32_z(0, nullptr, 0), 0);
	if (compressionLevel != Z_NO_COMPRESSION) {
		m_packed->Z.emplace();
		if (const auto res = deflateInit2(&*m_packed->Z, compressionLevel, Z_DEFLATED, -MAX_WBITS, DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY); res != Z_OK)
			throw util::zlib_error(res);
	}
}

void xivres::textools::simple_ttmp2_writer::add_packed(const packed_stream& stream) {
	if (!m_zf)
		throw std::logic_error("No file is open");
	if (!m_packed)
		throw std::logic_error("Packing has not started yet");
	if (m_packed->Complete)
		throw std::logic_error("Packing has already ended");

	std::unique_lock lock(*m_packed->WriteMtx, std::defer_lock);
	util::thread_pool::pool::current().release_working_status([&lock] { lock.lock(); });

	try {
		constexpr uint32_t bufferSize = 65536;

		auto& mods_json = m_ttmpl->SimpleModsList.emplace_back();
		mods_json.Name = stream.path_spec().text();
		mods_json.Category = "Raw File Imports";
		mods_json.DatFile = "000000";
		mods_json.FullPath = util::unicode::convert<std::string>(stream.path_spec().text(), &util::unicode::lower);
		mods_json.ModOffset = m_packed->Size;
		mods_json.ModSize = stream.size();
		mods_json.IsDefault = true;

		m_packed->Size += stream.size();

		auto pooledBuffer = util::thread_pool::pooled_byte_buffer();
		if (!pooledBuffer)
			pooledBuffer.emplace();
		auto& buffer = *pooledBuffer;
		buffer.resize(bufferSize);

		auto pooledBuffer2 = util::thread_pool::pooled_byte_buffer();
		if (!pooledBuffer2)
			pooledBuffer2.emplace();
		auto& buffer2 = *pooledBuffer2;
		buffer2.resize(bufferSize);

		for (std::streamoff i = 0, i_ = stream.size(); i < i_; i += bufferSize) {
			const auto chunkSpan = std::span(buffer).subspan(0, (std::min)(static_cast<uint32_t>(i_ - i), bufferSize));

			util::thread_pool::pool::throw_if_current_task_cancelled();
			stream.read_fully(i, chunkSpan);
			m_packed->Crc32 = crc32_z(m_packed->Crc32, &chunkSpan[0], chunkSpan.size());

			util::thread_pool::pool::throw_if_current_task_cancelled();
			if (!m_packed->Z) {
				if (const auto err = zipWriteInFileInZip(m_zf, &chunkSpan[0], static_cast<uint32_t>(chunkSpan.size())))
					throw std::runtime_error(std::format("zipWriteInFileInZip error({})", err));

			} else {
				m_packed->Z->next_in = &chunkSpan[0];
				m_packed->Z->avail_in = static_cast<uint32_t>(chunkSpan.size());

				do {
					m_packed->Z->next_out = &buffer2[0];
					m_packed->Z->avail_out = static_cast<uint32_t>(buffer2.size());

					util::thread_pool::pool::throw_if_current_task_cancelled();
					if (const auto res = deflate(&*m_packed->Z, Z_NO_FLUSH); res == Z_STREAM_ERROR)
						throw util::zlib_error(res);

					const auto deflated = std::span(buffer2).subspan(0, buffer2.size() - m_packed->Z->avail_out);

					util::thread_pool::pool::throw_if_current_task_cancelled();
					if (const auto err = zipWriteInFileInZip(m_zf, &deflated[0], static_cast<uint32_t>(deflated.size())))
						throw std::runtime_error(std::format("zipWriteInFileInZip error({})", err));
				} while (m_packed->Z->avail_out == 0);
			}
		}

	} catch (...) {
		m_errorState = true;
		throw;
	}
}

void xivres::textools::simple_ttmp2_writer::end_packed() {
	if (!m_zf)
		throw std::logic_error("No file is open");
	if (!m_packed)
		throw std::logic_error("Packing has not started yet");
	if (m_packed->Complete)
		throw std::logic_error("Packing has already ended");

	try {
		if (m_packed->Z) {
			constexpr uint32_t bufferSize = 65536;

			auto pooledBuffer2 = util::thread_pool::pooled_byte_buffer();
			if (!pooledBuffer2)
				pooledBuffer2.emplace();
			auto& buffer2 = *pooledBuffer2;
			buffer2.resize(bufferSize);

			do {
				m_packed->Z->next_in = nullptr;
				m_packed->Z->avail_in = 0;
				m_packed->Z->next_out = &buffer2[0];
				m_packed->Z->avail_out = static_cast<uint32_t>(buffer2.size());

				util::thread_pool::pool::throw_if_current_task_cancelled();
				if (const auto res = deflate(&*m_packed->Z, Z_FINISH); res == Z_STREAM_ERROR)
					throw util::zlib_error(res);

				const auto deflated = std::span(buffer2).subspan(0, buffer2.size() - m_packed->Z->avail_out);

				util::thread_pool::pool::throw_if_current_task_cancelled();
				if (const auto err = zipWriteInFileInZip(m_zf, &deflated[0], static_cast<uint32_t>(deflated.size())))
					throw std::runtime_error(std::format("zipWriteInFileInZip error({})", err));
			} while (m_packed->Z->avail_out == 0);
		}

		zipCloseFileInZipRaw64(m_zf, m_packed->Size, m_packed->Crc32);
		m_packed->Complete = true;

	} catch (...) {
		m_errorState = true;
		throw;
	}
}

void xivres::textools::simple_ttmp2_writer::add_file(const std::string& path, const stream& stream, int compressionLevel, const std::string& comment) {
	if (!m_zf)
		throw std::logic_error("No file is open");

	constexpr zip_fileinfo zi{};

	if (m_packed && !m_packed->Complete)
		throw std::logic_error("Cannot add file while packing is in progress");

	if (const auto err = zipOpenNewFileInZip3_64(
		m_zf, path.c_str(), &zi,
		nullptr, 0, nullptr, 0,
		comment.empty() ? nullptr : comment.c_str(),
		compressionLevel != Z_NO_COMPRESSION ? Z_DEFLATED : 0, compressionLevel,
		1,
		-MAX_WBITS, DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY,
		nullptr, 0, 1))
		throw std::runtime_error(std::format("zipOpenNewFileInZip3_64 error({})", err));

	try {
		constexpr uint32_t bufferSize = 65536;

		auto pooledBuffer = util::thread_pool::pooled_byte_buffer();
		if (!pooledBuffer)
			pooledBuffer.emplace();
		auto& buffer = *pooledBuffer;
		buffer.resize(bufferSize);

		auto pooledBuffer2 = util::thread_pool::pooled_byte_buffer();
		if (!pooledBuffer2)
			pooledBuffer2.emplace();
		auto& buffer2 = *pooledBuffer2;
		buffer2.resize(bufferSize);

		if (compressionLevel != Z_NO_COMPRESSION) {
			if (!m_zstream) {
				m_zstream.emplace();
				if (const auto res = deflateInit2(&*m_zstream, compressionLevel, Z_DEFLATED, -MAX_WBITS, DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY); res != Z_OK)
					throw util::zlib_error(res);
			} else {
				if (const auto res = deflateReset(&*m_zstream); res != Z_OK)
					throw util::zlib_error(res);
			}
		}

		uint32_t crc32 = crc32_z(0, nullptr, 0);
		for (std::streamoff i = 0, i_ = stream.size(); i < i_; i += bufferSize) {
			const auto chunkSpan = std::span(buffer).subspan(0, (std::min)(static_cast<uint32_t>(i_ - i), bufferSize));
			const auto isLastChunk = i + static_cast<std::streamsize>(chunkSpan.size()) >= i_;

			util::thread_pool::pool::throw_if_current_task_cancelled();
			stream.read_fully(i, chunkSpan);
			crc32 = crc32_z(crc32, &chunkSpan[0], chunkSpan.size());

			util::thread_pool::pool::throw_if_current_task_cancelled();
			if (compressionLevel == Z_NO_COMPRESSION) {
				if (const auto err = zipWriteInFileInZip(m_zf, &chunkSpan[0], static_cast<uint32_t>(chunkSpan.size())))
					throw std::runtime_error(std::format("zipWriteInFileInZip error({})", err));

			} else {
				m_zstream->next_in = &chunkSpan[0];
				m_zstream->avail_in = static_cast<uint32_t>(chunkSpan.size());

				do {
					m_zstream->next_out = &buffer2[0];
					m_zstream->avail_out = static_cast<uint32_t>(buffer2.size());

					util::thread_pool::pool::throw_if_current_task_cancelled();
					if (const auto res = deflate(&*m_zstream, isLastChunk ? Z_FINISH : Z_NO_FLUSH); res == Z_STREAM_ERROR)
						throw util::zlib_error(res);

					const auto deflated = std::span(buffer2).subspan(0, buffer2.size() - m_zstream->avail_out);

					util::thread_pool::pool::throw_if_current_task_cancelled();
					if (!deflated.empty()) {
						if (const auto err = zipWriteInFileInZip(m_zf, &deflated[0], static_cast<uint32_t>(deflated.size())))
							throw std::runtime_error(std::format("zipWriteInFileInZip error({})", err));
					}
				} while (m_zstream->avail_out == 0);
			}
		}

		zipCloseFileInZipRaw64(m_zf, stream.size(), crc32);

	} catch (...) {
		m_errorState = true;
		throw;
	}
}

void xivres::textools::simple_ttmp2_writer::open(std::filesystem::path path) {
	close();

	m_path = std::move(path);
	m_pathTemp = m_path;
#ifdef _WIN32
	m_pathTemp.replace_filename(std::format(L"{}.{:X}.tmp", m_pathTemp.filename().c_str(), std::chrono::steady_clock::now().time_since_epoch().count()));
	fill_win32_filefunc64W(&m_zffunc);
#else
	m_pathTemp.replace_filename(std::format("{}.{:X}.tmp", m_pathTemp.filename().c_str(), std::chrono::steady_clock::now().time_since_epoch().count()));
	fill_fopen64_filefunc(&m_zffunc);
#endif
	m_zf = zipOpen2_64(m_pathTemp.c_str(), APPEND_STATUS_CREATE, nullptr, &m_zffunc);
	if (!m_zf)
		throw std::runtime_error("zipOpen2_64: failed to create file");

	m_ttmpl.emplace();
	m_ttmpl->TTMPVersion = "1.3s";
	m_ttmpl->MinimumFrameworkVersion = "1.3.0.0";
}

void xivres::textools::simple_ttmp2_writer::close(bool revert, const std::string& comment) {
	if (!m_zf)
		return;

	if (!m_packed && !(revert || m_errorState))
		throw std::logic_error("Packing has not been done");

	if (!(revert || m_errorState)) {
		for (auto& mods_json : m_ttmpl->SimpleModsList) {
			if (mods_json.ModPack)
				continue;

			auto& mod_pack = mods_json.ModPack.emplace();
			mod_pack.Name = m_ttmpl->Name;
			mod_pack.Author = m_ttmpl->Author;
			mod_pack.Version = m_ttmpl->Version;
			mod_pack.Url = m_ttmpl->Url;
		}

		auto jsonobj = nlohmann::json::object();
		to_json(jsonobj, *m_ttmpl);
		const auto ttmpls = jsonobj.dump(1, '\t');

		if (!m_packed->Complete)
			end_packed();
		add_file("TTMPL.mpl", memory_stream(std::span(reinterpret_cast<const uint8_t*>(&ttmpls[0]), ttmpls.size())));
	}

	zipClose(m_zf, comment.empty() ? nullptr : comment.c_str());

	if (m_zstream)
		deflateEnd(&*m_zstream);
	if (m_packed && m_packed->Z)
		deflateEnd(&*m_packed->Z);

	m_zffunc = {};
	m_zf = nullptr;
	m_ttmpl.reset();
	m_zstream.reset();
	m_packed.reset();

	if (revert || m_errorState) {
		if (exists(m_pathTemp))
			remove(m_pathTemp);
	} else {
		if (exists(m_path))
			remove(m_path);
		rename(m_pathTemp, m_path);
	}

	m_path.clear();
	m_pathTemp.clear();
	m_errorState = false;
}
