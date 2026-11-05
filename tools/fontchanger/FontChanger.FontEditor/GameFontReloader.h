#pragma once

namespace FontChanger::Structs {
	struct MultiFontSet;
}

namespace GameFontReloader {
	static constexpr size_t FontCount = 0x29;

	// A font table of the game: which font each of its 41 slots loads.
	struct FontSetInGame {
		struct Font {
			int TexCount;
			int Padding;
			char* TexPattern;
			char* Fdt;
		} Fonts[FontCount];
	};

	struct FontSet {
		struct Font {
			int TexCount;
			std::string TexPattern;
			std::string Fdt;
		} Fonts[FontCount];
	};

	// A running game: its font tables (the game's and the two of the lobby), read from its executable, and what
	// reloading its fonts calls, found by signatures in its code.
	class GameProcess {
		struct Table {
			void* Address;
			FontSetInGame Original;  // as the executable has it, with pointers into the running game
			FontSet Fonts;
		};

		const std::unique_ptr<void, decltype(&CloseHandle)> m_hProcess;
		const HMODULE m_hModule;
		const std::filesystem::path m_gameExePath;

		std::vector<Table> m_tables;  // the game's first
		xivres::font_type m_fontType = xivres::font_type::undefined;

		// What the injected code (CallAtkModuleVf43.asm) reads, of the game.
		struct {
			void* ppFramework;
			void* pfnFrameworkGetUiModule;
			void* pfnAtkUnitManagerGetAddonByName;
			void* pfnAtkTextNodeToggleFontCache;
			void* ppAtkStage;
			size_t AtkModule_IsLobby;
			size_t AtkStage_RaptureAtkUnitManager;
			size_t AddonNamePlate_NamePlateObjects;
			size_t NamePlateObject_Size;
			size_t NamePlateObject_NameText;
			size_t AtkTextNode_FontCacheFlags;
			size_t NamePlateObjectCount;
			size_t UIModule_GetRaptureAtkModule_VtableOffset;
			void* pfnAtkModuleLoadFonts;
			size_t UseFontCacheFlag;
		} m_game{};

	public:
		GameProcess(DWORD pid);

		// The font type of the game's table (font, chn_axis or krn_axis), by the names of the fonts it loads.
		xivres::font_type GetFontType() const { return m_fontType; }

		// Makes the game load its fonts again, with its tables as they will be for a project and a font type to show:
		// undefined, the tables as the game has them with the project's fonts loaded from the project's textures; another,
		// the game's table of that font type. Without a project, the tables are restored as the executable has them.
		void RefreshFonts(const FontChanger::Structs::MultiFontSet* pProject, xivres::font_type fontType = xivres::font_type::undefined) const;
	};

	// The table of the fonts the game of a font type loads (data/game_fonts.json of FontChanger.FixedSizeFont).
	FontSet GetDefaultFontSet(xivres::font_type type);
}
