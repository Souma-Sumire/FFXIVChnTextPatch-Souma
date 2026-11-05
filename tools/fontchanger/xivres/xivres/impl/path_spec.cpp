#include "../include/xivres/path_spec.h"

#include <charconv>

namespace {
	uint8_t parse_uint8(std::string_view s) {
		uint8_t value = 0;
		if (std::from_chars(s.data(), s.data() + s.size(), value).ec != std::errc())
			return 0;
		return value;
	}
}

xivres::sqpack_spec::sqpack_spec(std::string_view part1, std::string_view part2, std::string_view part3)
	: FullId(0) {
	if (part1 == "common") {
		CategoryId = 0x00;

	} else if (part1 == "bgcommon") {
		CategoryId = 0x01;

	} else if (part1 == "bg") {
		CategoryId = 0x02;
		ExpacId = part2.size() > 2 && part2.starts_with("ex") ? parse_uint8(part2.substr(2)) : 0;
		PartId = ExpacId > 0U ? parse_uint8(part3) : 0;

	} else if (part1 == "cut") {
		CategoryId = 0x03;
		ExpacId = part2.size() > 2 && part2.starts_with("ex") ? parse_uint8(part2.substr(2)) : 0;

	} else if (part1 == "chara") {
		CategoryId = 0x04;

	} else if (part1 == "shader") {
		CategoryId = 0x05;

	} else if (part1 == "ui") {
		CategoryId = 0x06;

	} else if (part1 == "sound") {
		CategoryId = 0x07;

	} else if (part1 == "vfx") {
		CategoryId = 0x08;

	} else if (part1 == "exd") {
		CategoryId = 0x0a;

	} else if (part1 == "game_script") {
		CategoryId = 0x0b;

	} else if (part1 == "music") {
		CategoryId = 0x0c;
		ExpacId = part2.size() > 2 && part2.starts_with("ex") ? parse_uint8(part2.substr(2)) : 0;
	}
}

std::string xivres::sqpack_spec::required_prefix() const {
	switch (static_cast<uint32_t>(CategoryId)) {
		case 0x00: return "common/";
		case 0x01: return "bgcommon/";
		case 0x02: return ExpacId > 0U ? std::format("bg/ex{}/{:02x}_", ExpacId, PartId) : "bg/ffxiv/";
		case 0x03: return ExpacId > 0U ? std::format("cut/ex{}/", ExpacId) : "cut/ffxiv/";
		case 0x04: return "chara/";
		case 0x05: return "shader/";
		case 0x06: return "ui/";
		case 0x07: return "sound/";
		case 0x08: return "vfx/";
		case 0x09: return "ui_script/";
		case 0x0a: return "exd/";
		case 0x0b: return "game_script/";
		case 0x0c: return "music/";
		case 0x12: return "sqpack_test/";
		case 0x13: return "debug/";
		default: return {};
	}
}

xivres::path_spec::path_spec(std::string fullPath)
	: m_text(std::move(fullPath)) {

	for (auto& c : m_text) {
		if (c == '\\')
			c = '/';
	}

	{
		size_t previousOffset = 0;
		for (size_t offset; (offset = m_text.find('/', previousOffset)) != std::string::npos; previousOffset = offset + 1) {
			m_parts.emplace_back(std::string_view(m_text).substr(previousOffset, offset - previousOffset));
			if (!m_parts.empty() && m_parts.back().empty())
				m_parts.pop_back();
		}
		m_parts.emplace_back(std::string_view(m_text).substr(previousOffset));
		if (!m_parts.empty() && m_parts.back().empty())
			m_parts.pop_back();
	}

	recalculate_hash_values(true);
}

xivres::path_spec& xivres::path_spec::operator/=(const path_spec& r) {
	if (r.empty())
		return *this;

	const auto previousPointer = m_text.data();
	size_t previousOffset = m_text.size() + 1;
	m_text.reserve(m_text.size() + 1 + r.m_text.size());
	m_text.push_back('/');
	m_text.insert(m_text.end(), r.m_text.begin(), r.m_text.end());
	const auto recheckSqPack = m_parts.size() < 3;
	{
		m_parts.reserve(m_parts.size() + r.parts().size());
		for (auto& p : m_parts)
			p = {p.data() - previousPointer + m_text.data(), p.size()};
		for (size_t offset; (offset = m_text.find('/', previousOffset)) != std::string::npos; previousOffset = offset + 1)
			m_parts.emplace_back(std::string_view(m_text).substr(previousOffset, offset - previousOffset));
		m_parts.emplace_back(std::string_view(m_text).substr(previousOffset));
	}

	if (recheckSqPack)
		m_sqpack = {m_parts[0], m_parts.size() > 1 ? m_parts[1] : std::string_view(), m_parts.size() > 2 ? m_parts[2] : std::string_view()};

	if (r.path_hash() == EmptyHashValue) {
		m_pathHash = full_path_hash();
		m_nameHash = r.name_hash();
		m_fullPathHash = ~crc32_combine(
			crc32_combine(~full_path_hash(), ~SlashHashValue, 1),
			~r.name_hash(),
			static_cast<long>(r.text().size()));

	} else {
		m_pathHash = ~crc32_combine(
			crc32_combine(~full_path_hash(), ~SlashHashValue, 1),
			~r.path_hash(),
			static_cast<long>(&r.parts().back().front() - &r.parts().front().front() - 1));
		m_nameHash = r.name_hash();
		m_fullPathHash = ~crc32_combine(
			crc32_combine(~path_hash(), ~SlashHashValue, 1),
			~name_hash(),
			static_cast<long>(parts().back().size()));
	}

	return *this;
}

xivres::path_spec& xivres::path_spec::operator+=(const path_spec& r) {
	if (r.empty())
		return *this;

	const auto previousPointer = m_text.data();
	auto previousOffset = static_cast<size_t>(m_parts.back().data() - m_text.data());
	m_text.reserve(m_text.size() + r.m_text.size());
	m_text.insert(m_text.end(), r.m_text.begin(), r.m_text.end());
	const auto recheckSqPack = m_parts.size() < 3;
	{
		m_parts.reserve(m_parts.size() + r.parts().size());
		for (auto& p : m_parts)
			p = {p.data() - previousPointer + m_text.data(), p.size()};
		for (size_t offset; (offset = m_text.find('/', previousOffset)) != std::string::npos; previousOffset = offset + 1)
			m_parts.emplace_back(std::string_view(m_text).substr(previousOffset, offset - previousOffset));
		m_parts.emplace_back(std::string_view(m_text).substr(previousOffset));
	}

	recalculate_hash_values(recheckSqPack);

	return *this;
}

uint32_t xivres::path_spec::hash(std::string_view text) {
	std::string s;
	s.reserve(text.size());
	util::unicode::convert(s, text, &util::unicode::lower);
	return ~crc32_z(0, reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

xivres::path_spec& xivres::path_spec::replace_stem(const std::string& newStem) {
	if (m_parts.empty())
		void();
	else if (m_parts.size() == 1) {
		m_text.clear();
		m_parts.clear();
		clear();
	} else {
		m_text.resize(m_text.size() - m_parts.back().size() - 1);
		m_parts.pop_back();
		m_fullPathHash = m_pathHash;

		std::string s;
		if (m_parts.size() > 1) {
			const auto dirPart = std::string_view(m_text).substr(
				static_cast<size_t>(m_parts.front().data() - m_text.data()),
				static_cast<size_t>(m_parts.back().data() - m_parts.front().data() - 1));
			s.reserve(dirPart.size());
			util::unicode::convert(s, dirPart, &util::unicode::lower);
			m_pathHash = ~crc32_z(0, reinterpret_cast<const uint8_t*>(s.data()), s.size());
		} else
			m_pathHash = EmptyHashValue;

		s.clear();
		s.reserve(m_parts.back().size());
		util::unicode::convert(s, m_parts.back(), &util::unicode::lower);
		m_nameHash = ~crc32_z(0, reinterpret_cast<const uint8_t*>(s.data()), s.size());

		if (newStem.empty() && m_parts.size() < 3)
			m_sqpack = {m_parts[0], m_parts.size() > 1 ? m_parts[1] : std::string_view(), m_parts.size() > 2 ? m_parts[2] : std::string_view()};
	}

	*this /= newStem;
	return *this;
}

xivres::path_spec xivres::path_spec::filename() const {
	path_spec res;
	if (!parts().empty()) {
		res.m_text = parts().back();
		res.m_nameHash = res.m_fullPathHash = m_nameHash;
		res.m_parts = {std::string_view(res.m_text)};
		res.m_sqpack = {res.m_parts.front()};
	}
	return res;
}

void xivres::path_spec::recalculate_hash_values(bool checkSqPack) {
	if (m_parts.empty()) {
		clear();

	} else if (m_parts.size() == 1) {
		m_empty = false;
		
		std::string s;
		s.reserve(m_parts.front().size());
		util::unicode::convert(s, m_parts.front(), &util::unicode::lower);
		m_fullPathHash = m_nameHash = ~crc32_z(0, reinterpret_cast<const uint8_t*>(s.data()), s.size());
		if (checkSqPack)
			m_sqpack = {m_parts[0], std::string_view(), std::string_view()};

	} else {
		m_empty = false;
		
		const auto dirPart = std::string_view(m_text).substr(
			static_cast<size_t>(m_parts.front().data() - m_text.data()),
			static_cast<size_t>(m_parts.back().data() - m_parts.front().data() - 1));
		std::string s;
		s.reserve(dirPart.size());
		util::unicode::convert(s, dirPart, &util::unicode::lower);
		m_pathHash = ~crc32_z(0, reinterpret_cast<const uint8_t*>(s.data()), s.size());

		s.clear();
		s.reserve(m_parts.back().size());
		util::unicode::convert(s, m_parts.back(), &util::unicode::lower);
		m_nameHash = ~crc32_z(0, reinterpret_cast<const uint8_t*>(s.data()), s.size());
		
		m_fullPathHash = ~crc32_combine(crc32_combine(~m_pathHash, ~SlashHashValue, 1), ~m_nameHash, static_cast<long>(m_parts.back().size()));
		if (checkSqPack)
			m_sqpack = {m_parts[0], m_parts[1], m_parts.size() > 2 ? m_parts[2] : std::string_view()};
	}
}
