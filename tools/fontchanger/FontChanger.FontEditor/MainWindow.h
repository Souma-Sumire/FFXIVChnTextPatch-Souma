#pragma once

#include "BaseWindow.h"
#include "FontChanger.Presets/ElementFonts.h"
#include "MainWindow.Internal.h"

namespace App {
	class FontElementEditorDialog;
	class ProgressDialog;

	// The font data of the fonts of a font set, and the textures of their glyphs.
	struct CompiledFontSet {
		std::vector<std::shared_ptr<xivres::fontdata::stream>> Fdts;
		std::vector<std::shared_ptr<xivres::texture::memory_mipmap_stream>> Mipmaps;
	};

	class FontEditorWindow : public BaseWindow {
		static constexpr auto ClassName = L"FontEditorWindowClass";

		enum : uint8_t {
			Id_None,
			Id_FontListBox,
			Id_FontElementListView,
			Id_Edit,
			Id_Last_,
		};

		enum class CompressionMode : uint8_t {
			CompressWhilePacking,
			CompressAfterPacking,
			DoNotCompress,
		};

		enum class VerticalSplitter : uint8_t {
			None,
			ListEdit,
			EditPreview,
		};

		const std::vector<std::wstring> m_args;

		bool m_bChanged = false;
		std::filesystem::path m_currentPath;
		Structs::MultiFontSet m_multiFontSet;
		Structs::Font* m_pActiveFont = nullptr;

		// Elements left out of the preview, such as for comparing a font without them; not saved.
		std::set<const Structs::FontElement*> m_deactivatedElements;

		// The merged font of the active font without them, and what it was made of.
		struct {
			std::vector<const void*> Key;
			std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font> Font;
		} m_previewFont;

		std::shared_ptr<xivres::texture::memory_mipmap_stream> m_pMipmap;
		std::map<Structs::FontElement*, std::unique_ptr<FontElementEditorDialog>> m_editors;
		bool m_bNeedRedraw = false;
		bool m_bWordWrap = false;
		bool m_bKerning = false;
		bool m_bShowLineMetrics = true;
		xivres::font_type m_hotReloadFontType = xivres::font_type::undefined;

		int m_scaledListViewHeight = 0;
		int m_scaledEditHeight = 0;
		int m_splitterThicknessPx = 0;
		int m_splitterListEditTop = 0;
		int m_splitterListEditBottom = 0;
		int m_splitterEditPreviewTop = 0;
		int m_splitterEditPreviewBottom = 0;

		HWND m_hWnd{};
		HACCEL m_hAccelerator{};
		HFONT m_hUiFont{};

		HWND m_hFontsListBox{};
		HWND m_hFontElementsListView{};
		HWND m_hEdit{};
		HMENU m_hFontElementContextMenu{};

		int m_nDrawLeft{};
		int m_nDrawTop{};
		int m_nZoom = PreviewZoomOne;

		int m_nPreviewScrollX = 0;
		int m_nPreviewScrollY = 0;
		int m_nPreviewContentWidth = 0;
		int m_nPreviewContentHeight = 0;
		bool m_nPreviewMayScroll = false;

		bool m_bIsReorderingFontElementList = false;
		VerticalSplitter m_activeSplitter = VerticalSplitter::None;

		struct {
			bool Active{};
			POINT StartPosition{};
			POINT DragRelativePosition{};
		} m_previewPan;

	public:
		FontEditorWindow(std::vector<std::wstring> args);
		FontEditorWindow(FontEditorWindow&&) = delete;
		FontEditorWindow(const FontEditorWindow&) = delete;
		FontEditorWindow operator =(FontEditorWindow&&) = delete;
		FontEditorWindow operator =(const FontEditorWindow&) = delete;

		~FontEditorWindow() override;

		bool ConsumeDialogMessage(MSG& msg) override;

		bool ConsumeAccelerator(MSG& msg) override;

	private:
		LRESULT Window_OnCreate(HWND hwnd);
		LRESULT Window_OnSize();
		LRESULT Window_OnPaint();
		LRESULT Window_OnInitMenuPopup(HMENU hMenu, int index, bool isWindowMenu);
		LRESULT Window_OnMouseMove(uint16_t states, int16_t x, int16_t y);
		LRESULT Window_OnMouseLButtonDown(uint16_t states, int16_t x, int16_t y);
		LRESULT Window_OnMouseLButtonUp(uint16_t states, int16_t x, int16_t y);
		LRESULT Window_OnMouseWheel(int16_t delta, int16_t x, int16_t y);
		LRESULT Window_OnSetCursor(HWND hContainer, int hittest, int wm);
		LRESULT Window_OnDestroy();
		void Window_Redraw();

		LRESULT Menu_File_New(xivres::font_type fontType);
		LRESULT Menu_File_Open();
		LRESULT Menu_File_Save();
		LRESULT Menu_File_SaveAs(bool changeCurrentFile);
		LRESULT Menu_File_OpenRecent(size_t index);
		LRESULT Menu_File_ClearRecent();
		void PopulateRecentFilesMenu(HMENU hMenu);
		LRESULT Menu_File_Language(const char* languageCode);
		LRESULT Menu_File_GameInstallationManager();
		LRESULT Menu_File_Exit();

		LRESULT Menu_Edit_Add();

		std::optional<LOGFONTW> ChooseSystemFont();
		LRESULT Menu_Edit_AddElementFromFont();
		LRESULT Menu_Edit_AddFromFont();
		LRESULT Menu_Edit_AutoFit(bool allSizes, bool selectedOnly = true);
		LRESULT Menu_Edit_Cut();
		LRESULT Menu_Edit_Copy();
		LRESULT Menu_Edit_Paste();
		LRESULT Menu_Edit_Delete();
		LRESULT Menu_Edit_SelectAll();
		LRESULT Menu_Edit_Details();
		LRESULT Menu_Edit_ChangeParams(int verticalOffset, int horizontalOffset, int letterSpacing, float fontSize);
		LRESULT Menu_Edit_ToggleMergeMode();
		LRESULT Menu_Edit_SetVerticalAlignment(FontChanger::FixedSizeFont::vertical_alignment alignment);
		LRESULT Menu_Edit_MoveUpOrDown(int direction);
		LRESULT Menu_Edit_CreateEmptyCopyFromSelection();

		LRESULT Menu_View_NextOrPrevFont(int direction);
		LRESULT Menu_View_WordWrap();
		LRESULT Menu_View_Kerning();
		LRESULT Menu_View_ShowLineMetrics();
		LRESULT Menu_View_Zoom(int zoom);

		LRESULT Menu_Export_Preview();
		LRESULT Menu_Export_Raw();
		LRESULT Menu_Export_TTMP(CompressionMode compressionMode);
		LRESULT Menu_Export_ToggleMapping(const ExportMapping& mapping);
		LRESULT Menu_Export_Glyphs(bool withAdjustments);
		LRESULT Menu_Export_FontGlyphs();

		LRESULT Menu_HotReload_Reload(bool restore);
		LRESULT Menu_HotReload_Font(xivres::font_type mode);

		LRESULT Edit_OnCommand(uint16_t commandId);

		LRESULT FontListBox_OnCommand(uint16_t commandId);

		// screenPos is (-1, -1) if the menu is opened from the keyboard.
		LRESULT FontListBox_OnContextMenu(POINT screenPos);

		// Scales the elements of the font to a size that the user enters.
		void FontListBox_ScaleToSize(Structs::Font& font);

		// Replaces the elements of the fonts of the same game font family in the font set with copies of the elements of
		// the font, scaled from its size to theirs. Sizes of fonts are those of their first elements, or for fonts without
		// elements, those of the game fonts by their names.
		// Returns the fonts replaced; none if the user declined.
		std::vector<Structs::Font*> FontListBox_ReplaceOtherSizes(Structs::FontSet& fontSet, const Structs::Font& font);

		void CloseEditors(const Structs::Font& font);
		void CloseEditors(const std::vector<Structs::FontElement*>& elements);

		LRESULT FontElementsListView_OnBeginDrag(NM_LISTVIEW& nmlv);
		bool FontElementsListView_OnDragProcessMouseUp(int16_t x, int16_t y);
		bool FontElementsListView_OnDragProcessMouseMove(int16_t x, int16_t y);
		bool FontElementsListView_DragProcessDragging(int16_t x, int16_t y);
		LRESULT FontElementsListView_OnDblClick(NMITEMACTIVATE& nmia);
		LRESULT FontElementsListView_OnRightClick(NMITEMACTIVATE& nmia);

		// Draws the values of fields that are Auto in the gray of disabled text.
		LRESULT FontElementsListView_OnCustomDraw(NMLVCUSTOMDRAW& nmcd);
		LRESULT FontElementsListView_Clone();
		LRESULT FontElementsListView_ShowNegativeBearingCodepoints();

		[[nodiscard]] double GetZoom() const noexcept;

		VerticalSplitter HitTestSplitter(int16_t x, int16_t y) const;
		void UpdateSplitterDragPosition(int16_t y);
		void EndSplitterDrag();

		void OpenFile(std::filesystem::path path);
		void SetCurrentMultiFontSet(Structs::MultiFontSet multiFontSet, std::filesystem::path path);
		[[nodiscard]] std::wstring GetCurrentFileName() const;

		// Finds the font set that has the font.
		[[nodiscard]] Structs::FontSet* FindFontSet(const Structs::Font& font) const;

		// Gets the indices of the selected elements of the active font, in ascending order.
		[[nodiscard]] std::vector<int> GetSelectedElementIndices() const;

		// Marks the changes and draws again after the elements of the active font changed.
		void OnActiveFontElementsChanged();

		// Fits the Auto fields of the active font's elements again, and shows them where they are shown.
		void RefitAutoFields();

		// Takes the folder of the current file as the one that relative paths in the configuration are resolved against.
		void UpdateProjectDirectory();

		// Watches the folders of glyph files, and draws again with the changed files.
		void StartWatchingGlyphFolders();
		void StopWatchingGlyphFolders();
		[[nodiscard]] static UINT GetGlyphFoldersChangedMessage();
		LRESULT OnGlyphFoldersChanged();

		void Changes_MarkFresh();
		void Changes_MarkDirty();
		bool Changes_ConfirmIfDirty();

		void ShowEditor(Structs::FontElement& element);

		// The merged font of the active font for the preview: without the deactivated elements; exports use GetMergedFont.
		std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font> GetPreviewFont();

		void UpdateFontList();
		// Makes the list of elements show those of the active font in their order, keeping the selection.
		void UpdateFontElementList();

		// Selects the elements of the active font in the list, and only them.
		void SelectFontElements(const std::set<const Structs::FontElement*>& elements);
		void UpdateFontElementListViewItem(const Structs::FontElement& element);

		// Compiles the font set, and takes the number of its textures as the expected one.
		CompiledFontSet CompileCurrentFontSet(ProgressDialog&, Structs::FontSet& fontSet);

		LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

		static LRESULT WINAPI WndProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
		static LRESULT WINAPI WndProcInitial(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	};
}
