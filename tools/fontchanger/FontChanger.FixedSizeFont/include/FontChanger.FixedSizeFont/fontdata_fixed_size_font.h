#ifndef XIVRES_FONTGENERATOR_GAMEFONTDATAFIXEDSIZEFONT_H_
#define XIVRES_FONTGENERATOR_GAMEFONTDATAFIXEDSIZEFONT_H_

#include <span>
#include <string>
#include <vector>

#include "fixed_size_font.h"
#include "xivres/fontdata.h"
#include "xivres/installation.h"

namespace FontChanger::FixedSizeFont {
	// One of the game's fonts, as data/game_fonts.json lists it.
	struct game_fontdata_definition {
		// The font type whose files it is of.
		xivres::font_type FontType;

		// The path of its FDT file (common/font/AXIS_12.fdt).
		std::string Path;

		// The FDT file name without the extension (AXIS_12), which fonts of font sets are named by.
		std::string Name;

		// The family (AXIS, JupiterN) and the subfamily (Regular).
		std::string Family;
		std::string Subfamily;

		float Size;
	};

	// An entry of a table the game loads its fonts from: the font, and how many textures of its font type are loaded.
	struct game_font_table_entry {
		const game_fontdata_definition* Font;
		int TextureCount;
	};

	class fontdata_fixed_size_font : public default_abstract_fixed_size_font {
		struct info {
			std::shared_ptr<const xivres::fontdata::stream> Font;
			std::string FamilyName;
			std::string SubfamilyName;
			std::vector<std::shared_ptr<xivres::texture::memory_mipmap_stream>> Mipmaps;
			std::set<char32_t> Codepoints;
			std::map<std::pair<char32_t, char32_t>, int> KerningPairs;
			std::vector<uint8_t> GammaTable;
		};

		std::shared_ptr<const info> m_info;

	public:
		fontdata_fixed_size_font(std::shared_ptr<const xivres::fontdata::stream> strm, std::vector<std::shared_ptr<xivres::texture::memory_mipmap_stream>> mipmapStreams, std::string familyName, std::string subfamilyName);

		fontdata_fixed_size_font();
		fontdata_fixed_size_font(fontdata_fixed_size_font&&) noexcept;
		fontdata_fixed_size_font(const fontdata_fixed_size_font& r);
		fontdata_fixed_size_font& operator=(fontdata_fixed_size_font&&) noexcept;
		fontdata_fixed_size_font& operator=(const fontdata_fixed_size_font&);
		~fontdata_fixed_size_font() override = default;

		[[nodiscard]] std::string family_name() const override;

		[[nodiscard]] std::string subfamily_name() const override;

		[[nodiscard]] float font_size() const override;

		[[nodiscard]] int ascent() const override;

		[[nodiscard]] int line_height() const override;

		[[nodiscard]] const std::set<char32_t>& all_codepoints() const override;

		[[nodiscard]] bool try_get_glyph_metrics(char32_t codepoint, glyph_metrics& gm) const override;

		[[nodiscard]] const std::map<std::pair<char32_t, char32_t>, int>& all_kerning_pairs() const override;

		[[nodiscard]] int get_adjusted_advance_width(char32_t left, char32_t right) const override;

		bool draw(char32_t codepoint, xivres::util::b8g8r8a8* pBuf, int drawX, int drawY, int destWidth, int destHeight, xivres::util::b8g8r8a8 fgColor, xivres::util::b8g8r8a8 bgColor) const override;

		bool draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override;

		[[nodiscard]] std::shared_ptr<fixed_size_font> get_threadsafe_view() const override;

		[[nodiscard]] const fixed_size_font* get_base_font(char32_t codepoint) const override;

	private:
		static glyph_metrics glyph_metrics_from_glyph_entry(const xivres::fontdata::glyph_entry* pEntry, int x = 0, int y = 0);
	};

	class game_fontdata_set {
		std::vector<std::shared_ptr<fontdata_fixed_size_font>> m_data;

	public:
		game_fontdata_set();
		game_fontdata_set(game_fontdata_set&&) noexcept;
		game_fontdata_set(const game_fontdata_set&);
		game_fontdata_set& operator=(game_fontdata_set&&) noexcept;
		game_fontdata_set& operator=(const game_fontdata_set&);

		game_fontdata_set(std::vector<std::shared_ptr<fontdata_fixed_size_font>> data);

		std::shared_ptr<fontdata_fixed_size_font> operator[](size_t i) const;

		[[nodiscard]] std::shared_ptr<fontdata_fixed_size_font> get_font(size_t i) const;

		// Gets the font of a family (by its name, as data/game_fonts.json has it) closest to a size.
		[[nodiscard]] std::shared_ptr<fontdata_fixed_size_font> get_font(std::string_view family, float size) const;

		[[nodiscard]] size_t count() const;

		operator bool() const;
	};

	// The font types data/game_fonts.json lists, in its order.
	[[nodiscard]] std::span<const xivres::font_type> get_font_types();

	// The fonts of a font type, in the order the game's files list them.
	[[nodiscard]] std::span<const game_fontdata_definition> get_fontdata_definition(xivres::font_type fontType = xivres::font_type::font);

	// Finds a font of any font type by its name (AXIS_12, AXIS_12_lobby); null if the game has none of the name.
	[[nodiscard]] const game_fontdata_definition* find_fontdata_definition(std::string_view name);

	// The families of the game's fonts (AXIS, JupiterN, ...), in the order of data/game_fonts.json.
	[[nodiscard]] std::span<const std::string> get_font_families();

	// The texture file path of a font type, as a format of the texture number (common/font/font{}.tex); null if unknown.
	[[nodiscard]] const char* get_font_tex_filename_format(xivres::font_type fontType = xivres::font_type::font);

	// How many texture files of a font type the game loads; 0 if unknown.
	[[nodiscard]] int get_font_texture_count(xivres::font_type fontType = xivres::font_type::font);

	// The fonts whose glyph tables must list the same characters in the same order: the game may draw a glyph of the second
	// with that of the first at the same position.
	[[nodiscard]] std::span<const std::pair<std::string, std::string>> get_linked_fontdata_names();

	// The table of 41 fonts the game of a font type loads its fonts from (the lobby's for font_lobby); empty if unknown.
	[[nodiscard]] std::span<const game_font_table_entry> get_font_table(xivres::font_type fontType = xivres::font_type::font);

	// Reads the fonts of a font type, with their textures, from a game installation.
	[[nodiscard]] game_fontdata_set get_fontdata_set(const xivres::installation& installation, xivres::font_type fontType = xivres::font_type::font);
}

#endif
