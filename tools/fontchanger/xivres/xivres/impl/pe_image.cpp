#include "../include/xivres/pe_image.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <format>
#include <stdexcept>

static_assert(sizeof(xivres::pe_image::runtime_function) == sizeof(IMAGE_RUNTIME_FUNCTION_ENTRY));

namespace {
	template<typename T>
	T read(std::span<const uint8_t> data, size_t offset) {
		if (offset > data.size() || data.size() - offset < sizeof(T))
			throw std::out_of_range("The executable is truncated.");
		T value;
		std::memcpy(&value, &data[offset], sizeof(T));
		return value;
	}

	// Where the NT headers are, checking that they're of a PE32+ image.
	size_t nt_headers_offset(std::span<const uint8_t> data) {
		if (read<IMAGE_DOS_HEADER>(data, 0).e_magic != IMAGE_DOS_SIGNATURE)
			throw std::invalid_argument("Not a PE file.");
		const auto offset = static_cast<size_t>(read<IMAGE_DOS_HEADER>(data, 0).e_lfanew);
		const auto nt = read<IMAGE_NT_HEADERS64>(data, offset);
		if (nt.Signature != IMAGE_NT_SIGNATURE)
			throw std::invalid_argument("Not a PE file.");
		if (nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
			throw std::invalid_argument("Not a 64-bit executable.");
		return offset;
	}
}

std::string_view xivres::pe_image::section::name() const {
	return {Name, static_cast<size_t>(std::ranges::find(Name, '\0') - std::begin(Name))};
}

bool xivres::pe_image::section::is_executable() const {
	return Characteristics & IMAGE_SCN_MEM_EXECUTE;
}

xivres::pe_image::pe_image(std::span<const uint8_t> data, bool loaded)
	: m_data(data)
	, m_loaded(loaded) {
	const auto ntOffset = nt_headers_offset(data);
	const auto nt = read<IMAGE_NT_HEADERS64>(data, ntOffset);
	const auto sectionsOffset = ntOffset + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;
	for (size_t i = 0; i < nt.FileHeader.NumberOfSections; i++) {
		const auto header = read<IMAGE_SECTION_HEADER>(data, sectionsOffset + i * sizeof(IMAGE_SECTION_HEADER));
		auto& sec = m_sections.emplace_back();
		std::memcpy(sec.Name, header.Name, sizeof sec.Name);
		sec.VirtualSize = header.Misc.VirtualSize;
		sec.VirtualAddress = header.VirtualAddress;
		sec.RawSize = header.SizeOfRawData;
		sec.RawAddress = header.PointerToRawData;
		sec.Characteristics = header.Characteristics;
	}

	const auto& exceptions = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
	const auto table = exceptions.VirtualAddress;
	const auto tableSize = exceptions.Size;
	if (table && tableSize) {
		const auto offset = offset_of(table);
		const auto count = tableSize / sizeof(runtime_function);
		if (offset + count * sizeof(runtime_function) > data.size())
			throw std::out_of_range("The executable is truncated.");
		m_functions = {reinterpret_cast<const runtime_function*>(&data[offset]), count};
	}
}

xivres::pe_image xivres::pe_image::from_file(std::span<const uint8_t> data) {
	return {data, false};
}

xivres::pe_image xivres::pe_image::from_loaded(const void* base) {
	const auto p = static_cast<const uint8_t*>(base);

	// The headers are there whatever the image's size.
	const std::span headers(p, 0x1000);
	const auto size = read<IMAGE_NT_HEADERS64>(headers, nt_headers_offset(headers)).OptionalHeader.SizeOfImage;
	return {std::span(p, size), true};
}

const xivres::pe_image::section* xivres::pe_image::find_section(std::string_view name) const {
	const auto it = std::ranges::find(m_sections, name, &section::name);
	return it == m_sections.end() ? nullptr : &*it;
}

std::span<const uint8_t> xivres::pe_image::section_data(const section& s) const {
	const auto start = m_loaded ? s.VirtualAddress : s.RawAddress;
	if (start > m_data.size())
		return {};
	return m_data.subspan(start, (std::min<size_t>)(m_loaded ? s.VirtualSize : s.RawSize, m_data.size() - start));
}

std::span<const uint8_t> xivres::pe_image::section_data(std::string_view name) const {
	if (const auto s = find_section(name))
		return section_data(*s);
	throw std::out_of_range(std::format("The executable has no {} section.", name));
}

size_t xivres::pe_image::offset_of(uint32_t rva) const {
	if (m_loaded) {
		if (rva >= m_data.size())
			throw std::out_of_range(std::format("RVA {:X} is outside the image.", rva));
		return rva;
	}
	for (const auto& s : m_sections) {
		if (s.VirtualAddress <= rva && rva < s.VirtualAddress + (std::max)(s.VirtualSize, s.RawSize))
			return rva - s.VirtualAddress + s.RawAddress;
	}
	throw std::out_of_range(std::format("RVA {:X} is in no section.", rva));
}

uint32_t xivres::pe_image::rva_of(const uint8_t* p) const {
	const auto offset = static_cast<size_t>(p - m_data.data());
	if (m_loaded)
		return static_cast<uint32_t>(offset);
	for (const auto& s : m_sections) {
		if (s.RawAddress <= offset && offset < s.RawAddress + s.RawSize)
			return static_cast<uint32_t>(offset - s.RawAddress + s.VirtualAddress);
	}
	throw std::out_of_range(std::format("Offset {:X} is in no section.", offset));
}

xivres::pe_image::runtime_function xivres::pe_image::primary_of(const runtime_function& entry) const {
	auto current = entry;
	for (size_t depth = 0; depth < 32; depth++) {
		// An indirect entry: the RVA of the entry it is.
		if (current.UnwindData & 1) {
			current = read<runtime_function>(m_data, offset_of(current.UnwindData & ~1U));
			continue;
		}

		// UNWIND_INFO: version and flags, prologue size, count of codes, frame; the codes (aligned to 2); then, with
		// UNW_FLAG_CHAININFO, the entry it continues.
		const auto info = offset_of(current.UnwindData);
		if (((read<uint8_t>(m_data, info) >> 3) & UNW_FLAG_CHAININFO) == 0)
			return current;
		const auto codeCount = static_cast<size_t>(read<uint8_t>(m_data, info + 2));
		current = read<runtime_function>(m_data, info + 4 + ((codeCount + 1) & ~static_cast<size_t>(1)) * 2);
	}
	throw std::runtime_error(std::format("The unwind data of RVA {:X} chains too far.", entry.BeginAddress));
}

std::optional<xivres::pe_image::runtime_function> xivres::pe_image::function_containing(uint32_t rva) const {
	const auto it = std::ranges::upper_bound(m_functions, rva, {}, &runtime_function::BeginAddress);
	if (it == m_functions.begin())
		return std::nullopt;
	if (const auto& entry = *std::prev(it); rva < entry.EndAddress)
		return primary_of(entry);
	return std::nullopt;
}

std::optional<std::pair<uint32_t, uint32_t>> xivres::pe_image::code_gap_around(uint32_t rva) const {
	const auto it = std::ranges::upper_bound(m_functions, rva, {}, &runtime_function::BeginAddress);
	if (it != m_functions.begin() && rva < std::prev(it)->EndAddress)
		return std::nullopt;

	const auto s = std::ranges::find_if(m_sections, [rva](const section& s) {
		return s.is_executable() && s.VirtualAddress <= rva && rva < s.VirtualAddress + s.VirtualSize;
	});
	if (s == m_sections.end())
		return std::nullopt;

	auto first = s->VirtualAddress;
	auto second = s->VirtualAddress + s->VirtualSize;
	if (it != m_functions.begin())
		first = (std::max)(first, std::prev(it)->EndAddress);
	if (it != m_functions.end())
		second = (std::min)(second, it->BeginAddress);
	return std::make_pair(first, second);
}
