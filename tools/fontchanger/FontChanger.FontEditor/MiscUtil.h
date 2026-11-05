#pragma once

class WException {
	std::wstring m_msg;

public:
	WException(std::wstring msg) : m_msg(msg) {}

	const std::wstring& what() const {
		return m_msg;
	}
};

std::wstring_view GetStringResource(UINT id, UINT langId);
std::wstring_view GetStringResource(UINT id);
void ShowErrorMessageBox(HWND hParent, UINT preambleStringResID, const std::wstring& details);
void ShowErrorMessageBox(HWND hParent, UINT preambleStringResID, const class std::system_error& e);
void ShowErrorMessageBox(HWND hParent, UINT preambleStringResID, const class std::exception& e);

std::wstring GetOpenTypeFeatureName(enum DWRITE_FONT_FEATURE_TAG tag);

std::wstring GetFontWeightName(DWRITE_FONT_WEIGHT weight);
std::wstring GetFontStretchName(DWRITE_FONT_STRETCH stretch);
std::wstring GetFontStyleName(DWRITE_FONT_STYLE style);

double GetZoomFromWindow(HWND hWnd);

std::wstring FormatPixelValue(int value, bool plusSign = false);
std::wstring FormatPixelValue(float value, bool plusSign = false);

[[nodiscard]] std::unique_ptr<std::remove_pointer_t<HWND>, void(*)(HWND)> SuppressRedraw(HWND hWnd);

// Adds a column to a list view, of a width in DIPs and with the name from a string resource.
void AddListViewColumn(HWND hListView, int columnIndex, int width, UINT nameResId);

// Moves a window to the center of another.
void CenterWindowOnParent(HWND hWnd, HWND hParent);

// Gets the language ID of a locale name; 0 (the user's language) for an empty name.
WORD GetLanguageIdFromLocaleName(const std::wstring& localeName);

// Shows a file dialog for a folder; nullopt if cancelled.
std::optional<std::filesystem::path> PickFolder(HWND hWnd, const GUID& clientGuid, UINT titleResId);

// Shows a file dialog to open or save a file; nullopt if cancelled.
std::optional<std::filesystem::path> PickFile(
	HWND hWnd,
	bool save,
	const GUID& clientGuid,
	UINT titleResId,
	std::span<const COMDLG_FILTERSPEC> fileTypes,
	const std::wstring& fileName = {},
	const wchar_t* defaultExtension = nullptr);

[[nodiscard]] std::unique_ptr<std::remove_pointer_t<HGLOBAL>, decltype(&FreeResource)> LoadResourceWithLanguageFallback(LPCWSTR type, UINT id);

template<typename TRet, typename... TIgnore>
struct TryCatchShowErrorImpl;

template<typename TRet>
struct TryCatchShowErrorImpl<TRet> {
	template<typename TFn>
	TRet operator()(TFn&& fn, TRet) const { return fn(); }
};

template<typename TRet, typename T, typename... Ts>
struct TryCatchShowErrorImpl<TRet, T, Ts...> {
	template<typename TFn>
	TRet operator()(TFn&& fn, TRet errorReturn) const {
		try {
			return TryCatchShowErrorImpl<TRet, Ts...>{}(std::forward<TFn>(fn), errorReturn);
		} catch (const T&) {
			return errorReturn;
		}
	}
};

template<typename... TIgnore, typename TRet, typename TFn>
TRet TryCatchShowError(HWND hParent, UINT msgId, TRet errorReturn, TFn&& fn) {
	try {
		return TryCatchShowErrorImpl<TRet, TIgnore...>{}(std::forward<TFn>(fn), errorReturn);
	} catch (const WException& e) {
		ShowErrorMessageBox(hParent, msgId, e.what());
	} catch (const std::system_error& e) {
		ShowErrorMessageBox(hParent, msgId, e);
	} catch (const std::exception& e) {
		ShowErrorMessageBox(hParent, msgId, e);
	}
	return errorReturn;
}

template<typename T>
INT_PTR __stdcall DlgProcStaticImpl(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
	T* pThis;
	if (message == WM_INITDIALOG) {
		pThis = reinterpret_cast<T*>(lParam);
		pThis->m_hWnd = hwnd;
		SetWindowLongPtrW(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(pThis));
	} else {
		pThis = reinterpret_cast<T*>(GetWindowLongPtrW(hwnd, DWLP_USER));
	}
	if (!pThis)
		return FALSE;
	return pThis->DlgProc(message, wParam, lParam);
}
