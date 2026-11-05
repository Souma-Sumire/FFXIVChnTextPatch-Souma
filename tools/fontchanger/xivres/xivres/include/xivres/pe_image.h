#ifndef XIVRES_PE_IMAGE_H_
#define XIVRES_PE_IMAGE_H_

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace xivres {
	// A PE32+ executable, as its file or as loaded into memory, with what finding code in it needs: its sections, and its
	// function table (.pdata). Addresses within it are RVAs; data() is where they are, mapped by offset_of.
	class pe_image {
	public:
		struct section {
			char Name[8];
			uint32_t VirtualSize;
			uint32_t VirtualAddress;
			uint32_t RawSize;
			uint32_t RawAddress;
			uint32_t Characteristics;

			[[nodiscard]] std::string_view name() const;
			[[nodiscard]] bool is_executable() const;
		};

		// An entry of the function table: a function (or a part of one) [BeginAddress, EndAddress), and its unwind info.
		struct runtime_function {
			uint32_t BeginAddress;
			uint32_t EndAddress;
			uint32_t UnwindData;
		};

	private:
		std::span<const uint8_t> m_data;
		bool m_loaded;
		std::vector<section> m_sections;
		std::span<const runtime_function> m_functions;

		pe_image(std::span<const uint8_t> data, bool loaded);

	public:
		// The bytes of an executable's file.
		static pe_image from_file(std::span<const uint8_t> data);

		// An executable loaded into memory, at its base address.
		static pe_image from_loaded(const void* base);

		[[nodiscard]] std::span<const uint8_t> data() const { return m_data; }
		[[nodiscard]] bool is_loaded() const { return m_loaded; }
		[[nodiscard]] std::span<const section> sections() const { return m_sections; }

		// A section by name (.text, .rdata, ...), or nullptr.
		[[nodiscard]] const section* find_section(std::string_view name) const;

		// The bytes of a section: of its raw data in a file, and of its virtual size when loaded. The named one throws if
		// there is no section of the name.
		[[nodiscard]] std::span<const uint8_t> section_data(const section& s) const;
		[[nodiscard]] std::span<const uint8_t> section_data(std::string_view name) const;

		// Where an RVA is in data(); throws if it is in no section of a file, or outside the image.
		[[nodiscard]] size_t offset_of(uint32_t rva) const;

		// The RVA of a byte of data(), which must be in a section of a file.
		[[nodiscard]] uint32_t rva_of(const uint8_t* p) const;

		[[nodiscard]] std::span<const runtime_function> function_table() const { return m_functions; }

		// The first part of a function a function table entry is a part of: later parts point to it by their chained
		// unwind info (or an indirect entry).
		[[nodiscard]] runtime_function primary_of(const runtime_function& entry) const;

		// The function containing an RVA, by its first part; nullopt if no entry of the function table covers it (as for
		// leaf functions, which have none).
		[[nodiscard]] std::optional<runtime_function> function_containing(uint32_t rva) const;

		// The stretch of executable code around an RVA between the function table's entries, for finding the leaf
		// function there [first, second); nullopt if an entry covers it, or it isn't in an executable section.
		[[nodiscard]] std::optional<std::pair<uint32_t, uint32_t>> code_gap_around(uint32_t rva) const;
	};
}

#endif
