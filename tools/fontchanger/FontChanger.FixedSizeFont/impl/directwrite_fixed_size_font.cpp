#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d2d1.h>

#include "../include/FontChanger.FixedSizeFont/directwrite_fixed_size_font.h"

using namespace xivres;

#include <harfbuzz/hb.h>

static HRESULT success_or_throw(HRESULT hr, std::initializer_list<HRESULT> acceptables = {}) {
	if (SUCCEEDED(hr))
		return hr;

	for (const auto& h : acceptables) {
		if (h == hr)
			return hr;
	}

	const auto err = _com_error(hr);
	wchar_t* pszMsg = nullptr;
	FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr,
		hr,
		MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
		reinterpret_cast<LPWSTR>(&pszMsg),
		0,
		nullptr);
	if (pszMsg) {
		std::unique_ptr<wchar_t, decltype(&LocalFree)> pszMsgFree(pszMsg, LocalFree);

		throw std::runtime_error(std::format(
			"Error (HRESULT=0x{:08X}): {}",
			static_cast<uint32_t>(hr),
			xivres::util::unicode::convert<std::string>(std::wstring(pszMsg))
		));
	} else {
		throw std::runtime_error(std::format(
			"Error (HRESULT=0x{:08X})",
			static_cast<uint32_t>(hr)
		));
	}
}

class stream_based_dwrite_font_file_loader : public IDWriteFontFileLoader {
	class stream_based_dwrite_font_file_stream : public IDWriteFontFileStream {
		const std::shared_ptr<xivres::stream> m_stream;

		std::atomic_uint32_t m_nRef = 1;

		stream_based_dwrite_font_file_stream(std::shared_ptr<xivres::stream> pStream);

	public:
		virtual ~stream_based_dwrite_font_file_stream() = default;

		static stream_based_dwrite_font_file_stream* New(std::shared_ptr<xivres::stream> pStream);

		HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) noexcept override;

		ULONG __stdcall AddRef() noexcept override;

		ULONG __stdcall Release() noexcept override;

		HRESULT __stdcall ReadFileFragment(void const** pFragmentStart, uint64_t fileOffset, uint64_t fragmentSize, void** pFragmentContext) noexcept override;

		void __stdcall ReleaseFileFragment(void* fragmentContext) noexcept override;

		HRESULT __stdcall GetFileSize(uint64_t* pFileSize) noexcept override;

		HRESULT __stdcall GetLastWriteTime(uint64_t* pLastWriteTime) noexcept override;
	};

	stream_based_dwrite_font_file_loader() = default;

public:
	stream_based_dwrite_font_file_loader(stream_based_dwrite_font_file_loader&&) = delete;
	stream_based_dwrite_font_file_loader(const stream_based_dwrite_font_file_loader&) = delete;
	stream_based_dwrite_font_file_loader& operator=(stream_based_dwrite_font_file_loader&&) = delete;
	stream_based_dwrite_font_file_loader& operator=(const stream_based_dwrite_font_file_loader&) = delete;
	virtual ~stream_based_dwrite_font_file_loader() = default;

	static stream_based_dwrite_font_file_loader& GetInstance();

	HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) noexcept override;

	ULONG __stdcall AddRef() noexcept override;

	ULONG __stdcall Release() noexcept override;

	HRESULT __stdcall CreateStreamFromKey(void const* fontFileReferenceKey, uint32_t fontFileReferenceKeySize, IDWriteFontFileStream** pFontFileStream) noexcept override;
};

class stream_based_dwrite_font_collection_loader : public IDWriteFontCollectionLoader {
public:
	virtual ~stream_based_dwrite_font_collection_loader() = default;

	class stream_based_dwrite_font_collection_enumerator : public IDWriteFontFileEnumerator {
		const IDWriteFactoryPtr m_factory;
		const std::shared_ptr<xivres::stream> m_stream;

		std::atomic_uint32_t m_nRef = 1;
		int m_nCurrentFile = -1;

		stream_based_dwrite_font_collection_enumerator(IDWriteFactoryPtr factoryPtr, std::shared_ptr<xivres::stream> pStream);

	public:
		virtual ~stream_based_dwrite_font_collection_enumerator() = default;

		static stream_based_dwrite_font_collection_enumerator* New(IDWriteFactoryPtr factoryPtr, std::shared_ptr<xivres::stream> pStream);

		HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) noexcept override;

		ULONG __stdcall AddRef() noexcept override;

		ULONG __stdcall Release() noexcept override;

		HRESULT __stdcall MoveNext(BOOL* pHasCurrentFile) noexcept override;

		HRESULT __stdcall GetCurrentFontFile(IDWriteFontFile** pFontFile) noexcept override;
	};

	stream_based_dwrite_font_collection_loader() = default;
	stream_based_dwrite_font_collection_loader(stream_based_dwrite_font_collection_loader&&) = delete;
	stream_based_dwrite_font_collection_loader(const stream_based_dwrite_font_collection_loader&) = delete;
	stream_based_dwrite_font_collection_loader& operator=(stream_based_dwrite_font_collection_loader&&) = delete;
	stream_based_dwrite_font_collection_loader& operator=(const stream_based_dwrite_font_collection_loader&) = delete;

	static stream_based_dwrite_font_collection_loader& GetInstance();

	HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) noexcept override;

	ULONG __stdcall AddRef() noexcept override;

	ULONG __stdcall Release() noexcept override;

	HRESULT __stdcall CreateEnumeratorFromKey(IDWriteFactory* factory, void const* collectionKey, uint32_t collectionKeySize, IDWriteFontFileEnumerator** pFontFileEnumerator) noexcept override;
};

class dwrite_font_table {
	const IDWriteFontFacePtr m_pFontFace;
	const void* m_pData;
	void* m_pTableContext;
	uint32_t m_nSize;
	BOOL m_bExists;

public:
	dwrite_font_table(IDWriteFontFace* pFace, uint32_t tag);

	~dwrite_font_table();

	operator bool() const;

	template<typename T = uint8_t>
	[[nodiscard]] std::span<const T> get_span() const {
		if (!m_bExists)
			return {};

		return {static_cast<const T*>(m_pData), m_nSize};
	}
};

FontChanger::FixedSizeFont::directwrite_fixed_size_font::directwrite_fixed_size_font(std::filesystem::path path, int fontIndex, float size, float gamma, const font_render_transformation_matrix& matrix, const create_struct& params)
	: directwrite_fixed_size_font(std::make_shared<memory_stream>(file_stream(std::move(path))), fontIndex, size, gamma, matrix, params) {}

FontChanger::FixedSizeFont::directwrite_fixed_size_font::directwrite_fixed_size_font(IDWriteFactoryPtr factory, IDWriteFontPtr font, float size, float gamma, const font_render_transformation_matrix& matrix, const create_struct& params) {
	if (!font)
		return;

	auto info = std::make_shared<struct info>();
	info->Factory = std::move(factory);
	info->Font = std::move(font);
	info->Params = params;
	info->Size = size;
	info->GammaTable = util::bitmap_copy::create_gamma_table(gamma);
	info->Matrix = {matrix.M11, matrix.M12, matrix.M21, matrix.M22, 0.f, 0.f};

	m_dwrite = face_from_info_t(*info);
	load_font_data(*info, m_dwrite);
	m_info = std::move(info);
}

FontChanger::FixedSizeFont::directwrite_fixed_size_font::directwrite_fixed_size_font(std::shared_ptr<xivres::stream> strm, int fontIndex, float size, float gamma, const font_render_transformation_matrix& matrix, const create_struct& params) {
	if (!strm)
		return;

	auto info = std::make_shared<struct info>();
	info->Stream = std::move(strm);
	info->Params = params;
	info->FontIndex = fontIndex;
	info->Size = size;
	info->GammaTable = util::bitmap_copy::create_gamma_table(gamma);
	info->Matrix = {matrix.M11, matrix.M12, matrix.M21, matrix.M22, 0.f, 0.f};

	m_dwrite = face_from_info_t(*info);
	load_font_data(*info, m_dwrite);
	m_info = std::move(info);
}

FontChanger::FixedSizeFont::directwrite_fixed_size_font::directwrite_fixed_size_font(const directwrite_fixed_size_font& r)
	: directwrite_fixed_size_font() {
	if (r.m_info == nullptr)
		return;

	m_dwrite = face_from_info_t(*r.m_info);
	m_info = r.m_info;
}

FontChanger::FixedSizeFont::directwrite_fixed_size_font::directwrite_fixed_size_font() = default;

FontChanger::FixedSizeFont::directwrite_fixed_size_font::directwrite_fixed_size_font(directwrite_fixed_size_font&&) noexcept = default;

const std::map<std::pair<char32_t, char32_t>, int>& FontChanger::FixedSizeFont::directwrite_fixed_size_font::all_kerning_pairs() const {
	return m_info->KerningPairs;
}

const std::set<char32_t>& FontChanger::FixedSizeFont::directwrite_fixed_size_font::all_codepoints() const {
	return m_info->Characters;
}

// Vertical metrics are subject to the vertical scale of the transformation, as the glyphs are.
int FontChanger::FixedSizeFont::directwrite_fixed_size_font::line_height() const {
	return m_info->scale_from_font_unit(static_cast<float>(m_info->Metrics.ascent + m_info->Metrics.descent + m_info->Metrics.lineGap) * std::abs(m_info->Matrix.m22));
}

int FontChanger::FixedSizeFont::directwrite_fixed_size_font::ascent() const {
	return m_info->scale_from_font_unit(static_cast<float>(m_info->Metrics.ascent) * std::abs(m_info->Matrix.m22));
}

float FontChanger::FixedSizeFont::directwrite_fixed_size_font::font_size() const {
	return m_info->Size;
}

std::string FontChanger::FixedSizeFont::directwrite_fixed_size_font::subfamily_name() const {
	IDWriteLocalizedStringsPtr strings;
	success_or_throw(m_dwrite.Font->GetFaceNames(&strings));

	uint32_t index;
	BOOL exists;
	success_or_throw(strings->FindLocaleName(L"en-us", &index, &exists));
	if (!exists)
		index = 0;

	uint32_t length;
	success_or_throw(strings->GetStringLength(index, &length));

	std::wstring res(length + 1, L'\0');
	success_or_throw(strings->GetString(index, &res[0], length + 1));
	res.resize(length);

	return util::unicode::convert<std::string>(res);
}

std::string FontChanger::FixedSizeFont::directwrite_fixed_size_font::family_name() const {
	IDWriteLocalizedStringsPtr strings;
	success_or_throw(m_dwrite.Family->GetFamilyNames(&strings));

	uint32_t index;
	BOOL exists;
	success_or_throw(strings->FindLocaleName(L"en-us", &index, &exists));
	if (!exists)
		index = 0;

	uint32_t length;
	success_or_throw(strings->GetStringLength(index, &length));

	std::wstring res(length + 1, L'\0');
	success_or_throw(strings->GetString(index, &res[0], length + 1));
	res.resize(length);

	return util::unicode::convert<std::string>(res);
}

FontChanger::FixedSizeFont::directwrite_fixed_size_font& FontChanger::FixedSizeFont::directwrite_fixed_size_font::operator=(const directwrite_fixed_size_font& r) {
	if (this == &r)
		return *this;

	if (r.m_info == nullptr) {
		m_dwrite = {};
		m_info = nullptr;
	} else {
		m_dwrite = face_from_info_t(*r.m_info);
		m_info = r.m_info;
	}

	return *this;
}

FontChanger::FixedSizeFont::directwrite_fixed_size_font& FontChanger::FixedSizeFont::directwrite_fixed_size_font::operator=(directwrite_fixed_size_font&&) noexcept = default;

bool FontChanger::FixedSizeFont::directwrite_fixed_size_font::draw(char32_t codepoint, util::b8g8r8a8* pBuf, int drawX, int drawY, int destWidth, int destHeight, util::b8g8r8a8 fgColor, util::b8g8r8a8 bgColor) const {
	IDWriteGlyphRunAnalysisPtr analysis;
	glyph_metrics gm;
	glyph_adjustment adjustment;
	if (!try_get_glyph_metrics(codepoint, gm, analysis, adjustment))
		return false;

	auto src = gm;
	src.translate(-src.X1, -src.Y1);
	auto dest = gm;
	dest.translate(drawX + adjustment.PlacementX, drawY + ascent() + adjustment.PlacementY);
	src.adjust_to_intersection(dest, src.width(), src.height(), destWidth, destHeight);
	if (src.is_effectively_empty() || dest.is_effectively_empty())
		return true;

	m_drawBuffer.resize(gm.area());
	success_or_throw(analysis->CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1, gm.as_const_rect_pointer(), &m_drawBuffer[0], static_cast<uint32_t>(m_drawBuffer.size())));

	util::bitmap_copy::to_b8g8r8a8()
		.from(&m_drawBuffer[0], gm.width(), gm.height(), 1, util::bitmap_vertical_direction::TopRowFirst)
		.to(pBuf, destWidth, destHeight, util::bitmap_vertical_direction::TopRowFirst)
		.fore_color(fgColor)
		.back_color(bgColor)
		.gamma_table(m_info->GammaTable)
		.copy(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);

	return true;
}

bool FontChanger::FixedSizeFont::directwrite_fixed_size_font::draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const {
	IDWriteGlyphRunAnalysisPtr analysis;
	glyph_metrics gm;
	glyph_adjustment adjustment;
	if (!try_get_glyph_metrics(codepoint, gm, analysis, adjustment))
		return false;

	auto src = gm;
	src.translate(-src.X1, -src.Y1);
	auto dest = gm;
	dest.translate(drawX + adjustment.PlacementX, drawY + ascent() + adjustment.PlacementY);
	src.adjust_to_intersection(dest, src.width(), src.height(), destWidth, destHeight);
	if (src.is_effectively_empty() || dest.is_effectively_empty())
		return true;

	m_drawBuffer.resize(gm.area());
	success_or_throw(analysis->CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1, gm.as_const_rect_pointer(), &m_drawBuffer[0], static_cast<uint32_t>(m_drawBuffer.size())));

	util::bitmap_copy::to_l8()
		.from(&m_drawBuffer[0], gm.width(), gm.height(), 1, util::bitmap_vertical_direction::TopRowFirst)
		.to(pBuf, destWidth, destHeight, stride, util::bitmap_vertical_direction::TopRowFirst)
		.fore_color(fgColor)
		.fore_opacity(fgOpacity)
		.back_color(bgColor)
		.back_opacity(bgOpacity)
		.gamma_table(m_info->GammaTable)
		.copy(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);
	return true;
}

std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font> FontChanger::FixedSizeFont::directwrite_fixed_size_font::get_threadsafe_view() const {
	return std::make_shared<directwrite_fixed_size_font>(*this);
}

const FontChanger::FixedSizeFont::fixed_size_font* FontChanger::FixedSizeFont::directwrite_fixed_size_font::get_base_font(char32_t codepoint) const {
	return this;
}

std::optional<float> FontChanger::FixedSizeFont::directwrite_fixed_size_font::get_baseline(uint32_t baselineTag) const {
	const auto it = m_info->Baselines.find(baselineTag);
	return it == m_info->Baselines.end() ? std::nullopt : std::optional(it->second);
}

FontChanger::FixedSizeFont::directwrite_fixed_size_font::dwrite_interfaces FontChanger::FixedSizeFont::directwrite_fixed_size_font::face_from_info_t(const info& info) {
	dwrite_interfaces res{};

	if (!!info.Font != !!info.Factory)
		throw std::invalid_argument("Both Font and Factory either must be set or not set.");

	if (info.Font) {
		res.Font = info.Font;
		res.Factory = info.Factory;
		success_or_throw(res.Factory.QueryInterface(decltype(res.Factory3)::GetIID(), &res.Factory3), {E_NOINTERFACE});
		success_or_throw(res.Font->GetFontFamily(&res.Family));
		success_or_throw(res.Family->GetFontCollection(&res.Collection));
		success_or_throw(res.Font->CreateFontFace(&res.Face));
		success_or_throw(res.Face.QueryInterface(decltype(res.Face1)::GetIID(), &res.Face1));
	} else {
		success_or_throw(DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&res.Factory)));
		success_or_throw(res.Factory->RegisterFontFileLoader(&stream_based_dwrite_font_file_loader::GetInstance()), {DWRITE_E_ALREADYREGISTERED});
		success_or_throw(res.Factory->RegisterFontCollectionLoader(&stream_based_dwrite_font_collection_loader::GetInstance()), {DWRITE_E_ALREADYREGISTERED});
		success_or_throw(res.Factory.QueryInterface(decltype(res.Factory3)::GetIID(), &res.Factory3), {E_NOINTERFACE});
		success_or_throw(res.Factory->CreateCustomFontCollection(&stream_based_dwrite_font_collection_loader::GetInstance(), &info.Stream, sizeof info.Stream, &res.Collection));
		success_or_throw(res.Collection->GetFontFamily(0, &res.Family));
		success_or_throw(res.Family->GetFont(info.FontIndex, &res.Font));
		success_or_throw(res.Font->CreateFontFace(&res.Face));
		success_or_throw(res.Face.QueryInterface(decltype(res.Face1)::GetIID(), &res.Face1));
	}

	// The face of the font comes with the simulations of the font; the face is made again from the file for others.
	const auto simulations = info.Params.Simulations.value_or(res.Font->GetSimulations());
	if (simulations != res.Face->GetSimulations()) {
		IDWriteFontFile* pFontFileTmp;
		uint32_t nFiles = 1;
		success_or_throw(res.Face->GetFiles(&nFiles, &pFontFileTmp));
		IDWriteFontFilePtr file(pFontFileTmp, false);

		IDWriteFontFacePtr face;
		success_or_throw(res.Factory->CreateFontFace(res.Face->GetType(), 1, &pFontFileTmp, res.Face->GetIndex(), simulations, &face));
		res.Face = face;
		success_or_throw(res.Face.QueryInterface(decltype(res.Face1)::GetIID(), &res.Face1));
	}

	// Variable fonts: start from the instance of the font, and apply the requested axis values.
	// Unless requested otherwise, the optical size follows the font size.
	std::vector<DWRITE_FONT_AXIS_VALUE> axisValues;
	if (IDWriteFontFace5Ptr face5; SUCCEEDED(res.Face.QueryInterface(decltype(face5)::GetIID(), &face5)) && face5->HasVariations()) {
		IDWriteFontResourcePtr resource;
		success_or_throw(face5->GetFontResource(&resource));

		axisValues.resize(face5->GetFontAxisValueCount());
		success_or_throw(face5->GetFontAxisValues(axisValues.data(), static_cast<UINT32>(axisValues.size())));
		std::vector<DWRITE_FONT_AXIS_RANGE> axisRanges(resource->GetFontAxisCount());
		success_or_throw(resource->GetFontAxisRanges(axisRanges.data(), static_cast<UINT32>(axisRanges.size())));

		for (auto& axisValue : axisValues) {
			if (const auto it = info.Params.Variations.find(static_cast<uint32_t>(axisValue.axisTag)); it != info.Params.Variations.end())
				axisValue.value = it->second;
			else if (axisValue.axisTag == DWRITE_FONT_AXIS_TAG_OPTICAL_SIZE)
				axisValue.value = info.Size;

			for (const auto& range : axisRanges) {
				if (range.axisTag == axisValue.axisTag)
					axisValue.value = std::clamp(axisValue.value, range.minValue, range.maxValue);
			}
		}

		IDWriteFontFace5Ptr instanceFace;
		success_or_throw(resource->CreateFontFace(simulations, axisValues.data(), static_cast<UINT32>(axisValues.size()), &instanceFace));
		res.Face = instanceFace;
		success_or_throw(res.Face.QueryInterface(decltype(res.Face1)::GetIID(), &res.Face1));
	}

	IDWriteLocalizedStringsPtr familyNames;
	success_or_throw(res.Family->GetFamilyNames(&familyNames));
	uint32_t index;
	if (BOOL exists; FAILED(familyNames->FindLocaleName(L"en-us", &index, &exists)) || !exists)
		index = 0;
	uint32_t length;
	success_or_throw(familyNames->GetStringLength(index, &length));
	std::wstring familyName(length + 1, L'\0');
	success_or_throw(familyNames->GetString(index, familyName.data(), length + 1));
	familyName.resize(length);

	// Layouts match the font again by these properties; ask for what makes DirectWrite apply the same simulations.
	auto weight = res.Font->GetWeight();
	auto style = res.Font->GetStyle();
	if (info.Params.Simulations) {
		if ((simulations & DWRITE_FONT_SIMULATIONS_BOLD) && !(res.Font->GetSimulations() & DWRITE_FONT_SIMULATIONS_BOLD))
			weight = (std::max)(weight, DWRITE_FONT_WEIGHT_BOLD);
		if ((simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE) && style == DWRITE_FONT_STYLE_NORMAL)
			style = DWRITE_FONT_STYLE_OBLIQUE;
	}
	success_or_throw(res.Factory->CreateTextFormat(
		familyName.c_str(),
		res.Collection,
		weight,
		style,
		res.Font->GetStretch(),
		info.Size,
		info.Params.LanguageCode.empty() ? L"en-us" : util::unicode::convert<std::wstring>(info.Params.LanguageCode).c_str(),
		&res.Format));
	if (IDWriteTextFormat3Ptr format3; !axisValues.empty() && SUCCEEDED(res.Format.QueryInterface(decltype(format3)::GetIID(), &format3)))
		success_or_throw(format3->SetFontAxisValues(axisValues.data(), static_cast<UINT32>(axisValues.size())));
	success_or_throw(res.Factory->CreateTypography(&res.Typography));
	// A typography replaces the features that are on by default instead of adding to them; those stay on unless the params say otherwise.
	for (const auto tag : {
		     DWRITE_FONT_FEATURE_TAG_STANDARD_LIGATURES,
		     DWRITE_FONT_FEATURE_TAG_CONTEXTUAL_LIGATURES,
		     DWRITE_FONT_FEATURE_TAG_CONTEXTUAL_ALTERNATES,
		     DWRITE_FONT_FEATURE_TAG_KERNING,
	     }) {
		if (std::ranges::none_of(info.Params.Features, [tag](const DWRITE_FONT_FEATURE& f) { return f.nameTag == tag; }))
			success_or_throw(res.Typography->AddFontFeature({tag, 1}));
	}
	for (const auto& feature : info.Params.Features)
		success_or_throw(res.Typography->AddFontFeature(feature));
	return res;
}

hb_face_t* FontChanger::FixedSizeFont::create_harfbuzz_face(IDWriteFontFace* pFace) {
	pFace->AddRef();
	struct table_context {
		IDWriteFontFacePtr Face;
		void* Context;
	};

	return hb_face_create_for_tables([](hb_face_t*, hb_tag_t tag, void* userData) -> hb_blob_t* {
		const auto pFace = static_cast<IDWriteFontFace*>(userData);
		const void* pData;
		UINT32 size;
		void* pContext;
		BOOL exists;
		// HarfBuzz tags are big endian integers, and DirectWrite tags keep the bytes in file order.
		if (FAILED(pFace->TryGetFontTable(_byteswap_ulong(tag), &pData, &size, &pContext, &exists)) || !exists)
			return nullptr;

		return hb_blob_create(static_cast<const char*>(pData), size, HB_MEMORY_MODE_READONLY, new table_context{ pFace, pContext }, [](void* p) {
			const auto pTableContext = static_cast<table_context*>(p);
			pTableContext->Face->ReleaseFontTable(pTableContext->Context);
			delete pTableContext;
		});
	}, pFace, [](void* p) { static_cast<IDWriteFontFace*>(p)->Release(); });
}
void FontChanger::FixedSizeFont::directwrite_fixed_size_font::load_font_data(info& info, const dwrite_interfaces& dwrite) {
	dwrite.Face->GetMetrics(&info.Metrics);

	std::vector<DWRITE_UNICODE_RANGE> ranges;
	for (uint32_t rangeCount = 0;;) {
		ranges.resize(rangeCount);
		if (success_or_throw(dwrite.Face1->GetUnicodeRanges(rangeCount, ranges.data(), &rangeCount), {E_NOT_SUFFICIENT_BUFFER}) == E_NOT_SUFFICIENT_BUFFER)
			continue;
		ranges.resize(rangeCount);
		break;
	}

	for (const auto& range : ranges)
		for (uint32_t i = range.first; i <= range.last; ++i)
			info.Characters.insert(static_cast<char32_t>(i));

	// Baselines are subject to the vertical scale of the transformation, as the glyphs are.
	if (dwrite_font_table baseDataRef(dwrite.Face, truetype::Base::DirectoryTableTag.NativeValue); baseDataRef) {
		const auto scale = static_cast<double>(info.Size) * info.Matrix.m22 / info.Metrics.designUnitsPerEm;
		for (const auto& [tag, value] : read_baselines(baseDataRef.get_span<char>()))
			info.Baselines.emplace(tag, static_cast<float>(value * scale));
	}

	dwrite_font_table kernDataRef(dwrite.Face, truetype::Kern::DirectoryTableTag.NativeValue);
	dwrite_font_table gposDataRef(dwrite.Face, truetype::Gpos::DirectoryTableTag.NativeValue);
	if (!kernDataRef && !gposDataRef)
		return;

	// Positioning is keyed by the glyphs that are actually drawn, which may be substituted by the selected features.
	std::vector<std::set<char32_t>> glyphToCharMap(dwrite.Face->GetGlyphCount());
	if (info.Params.Features.empty() && info.Params.LanguageCode.empty()) {
		const std::vector<UINT32> codepoints(info.Characters.begin(), info.Characters.end());
		std::vector<UINT16> glyphIndices(codepoints.size());
		success_or_throw(dwrite.Face->GetGlyphIndices(codepoints.data(), static_cast<UINT32>(codepoints.size()), glyphIndices.data()));
		for (size_t i = 0; i < codepoints.size(); i++) {
			if (glyphIndices[i])
				glyphToCharMap[glyphIndices[i]].insert(static_cast<char32_t>(codepoints[i]));
		}
	} else {
		for (const auto c : info.Characters) {
			if (const auto glyphIndex = resolve_glyph_index(dwrite, c); glyphIndex && glyphIndex < glyphToCharMap.size())
				glyphToCharMap[glyphIndex].insert(c);
		}
	}

	std::map<uint32_t, float> designCoordinates;
	if (IDWriteFontFace5Ptr face5; SUCCEEDED(dwrite.Face->QueryInterface(IID_PPV_ARGS(&face5))) && face5->HasVariations()) {
		std::vector<DWRITE_FONT_AXIS_VALUE> axisValues(face5->GetFontAxisValueCount());
		success_or_throw(face5->GetFontAxisValues(axisValues.data(), static_cast<UINT32>(axisValues.size())));
		for (const auto& axisValue : axisValues)
			designCoordinates.emplace(static_cast<uint32_t>(axisValue.axisTag), axisValue.value);
	}

	dwrite_font_table gdefDataRef(dwrite.Face, truetype::Gdef::DirectoryTableTag.NativeValue);
	dwrite_font_table fvarDataRef(dwrite.Face, truetype::Fvar::DirectoryTableTag.NativeValue);
	dwrite_font_table avarDataRef(dwrite.Face, truetype::Avar::DirectoryTableTag.NativeValue);
	opentype_positioning_params params{
		.Gpos = gposDataRef.get_span<char>(),
		.Kern = kernDataRef.get_span<char>(),
		.Gdef = gdefDataRef.get_span<char>(),
		.Fvar = fvarDataRef.get_span<char>(),
		.Avar = avarDataRef.get_span<char>(),
		.DesignCoordinates = std::move(designCoordinates),
		.LanguageCode = info.Params.LanguageCode,
		.Size = info.Size,
		.UnitsPerEm = info.Metrics.designUnitsPerEm,
		.ScaleX = info.Matrix.m11,
		.ScaleY = info.Matrix.m22,
	};
	for (const auto& feature : info.Params.Features) {
		if (feature.parameter)
			params.FeatureTags.insert(static_cast<uint32_t>(feature.nameTag));
		else
			params.DisabledFeatureTags.insert(static_cast<uint32_t>(feature.nameTag));
	}

	const auto hbFace = std::unique_ptr<hb_face_t, decltype(&hb_face_destroy)>(create_harfbuzz_face(dwrite.Face), &hb_face_destroy);
	params.HarfBuzzFace = hbFace.get();

	auto positioning = extract_opentype_positioning(params, glyphToCharMap);
	info.KerningPairs = std::move(positioning.KerningPairs);
	info.GlyphAdjustments = std::move(positioning.GlyphAdjustments);
}

uint16_t FontChanger::FixedSizeFont::directwrite_fixed_size_font::resolve_glyph_index(const dwrite_interfaces& dwrite, char32_t codepoint) {
	try {
		wchar_t buf[3]{};
		UINT32 buflen;
		if (codepoint < 0x10000) {
			buf[0] = static_cast<wchar_t>(codepoint);
			buflen = 1;
		} else if (codepoint < 0x110000) {
			buf[0] = static_cast<wchar_t>(0xD800 + ((codepoint - 0x10000) >> 10));
			buf[1] = static_cast<wchar_t>(0xDC00 + ((codepoint - 0x10000) & 0x3FF));
			buflen = 2;
		} else {
			return 0;
		}

		IDWriteTextLayoutPtr layout;
		success_or_throw(dwrite.Factory->CreateTextLayout(buf, buflen, dwrite.Format, 9999999, 9999999, &layout));
		success_or_throw(layout->SetTypography(dwrite.Typography, {.startPosition = 0, .length = buflen}));

		class DummyRenderer final : public IDWriteTextRenderer {
		public:
			uint16_t GlyphIndex = 0;

			STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override {
				if (!ppv)
					return E_INVALIDARG;

				if (riid == __uuidof(IUnknown)
					|| riid == __uuidof(IDWritePixelSnapping)
					|| riid == __uuidof(IDWriteTextRenderer)) {
					this->AddRef();
					*ppv = this;
					return S_OK;
				}

				return E_NOINTERFACE;
			}
			ULONG __stdcall AddRef() override {
				return 1;
			}
			ULONG __stdcall Release() override {
				return 0;
			}
			STDMETHOD(IsPixelSnappingDisabled)(_In_opt_ void* clientDrawingContext, _Out_ BOOL* isDisabled) override {
				*isDisabled = false;
				return S_OK;
			}
			STDMETHOD(GetCurrentTransform)(_In_opt_ void* clientDrawingContext, _Out_ DWRITE_MATRIX* transform) override {
				*transform = {1, 0, 0, 1, 0, 0};
				return S_OK;
			}
			STDMETHOD(GetPixelsPerDip)(_In_opt_ void* clientDrawingContext, _Out_ FLOAT* pixelsPerDip) override {
				*pixelsPerDip = 96;
				return S_OK;
			}
		    STDMETHOD(DrawGlyphRun)(
		        _In_opt_ void* clientDrawingContext,
		        FLOAT baselineOriginX,
		        FLOAT baselineOriginY,
		        DWRITE_MEASURING_MODE measuringMode,
		        _In_ DWRITE_GLYPH_RUN const* glyphRun,
		        _In_ DWRITE_GLYPH_RUN_DESCRIPTION const* glyphRunDescription,
		        _In_opt_ IUnknown* clientDrawingEffect
		        ) override {
		        GlyphIndex = glyphRun->glyphIndices[0];
		        return S_OK;
		    }

			STDMETHOD(DrawUnderline)(_In_opt_ void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY, _In_ DWRITE_UNDERLINE const* underline, _In_opt_ IUnknown* clientDrawingEffect) override {
				return E_NOTIMPL;
			}
			STDMETHOD(DrawStrikethrough)(_In_opt_ void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY, _In_ DWRITE_STRIKETHROUGH const* strikethrough, _In_opt_ IUnknown* clientDrawingEffect) override {
				return E_NOTIMPL;
			}
			STDMETHOD(DrawInlineObject)(_In_opt_ void* clientDrawingContext, FLOAT originX, FLOAT originY, _In_ IDWriteInlineObject* inlineObject, BOOL isSideways, BOOL isRightToLeft, _In_opt_ IUnknown* clientDrawingEffect) override {
				return E_NOTIMPL;
			}
		} dummyRenderer;
		success_or_throw(layout->Draw(nullptr, &dummyRenderer, 0, 0));

		return dummyRenderer.GlyphIndex;
	} catch (...) {
		return 0;
	}
}

bool FontChanger::FixedSizeFont::directwrite_fixed_size_font::try_get_glyph_metrics(char32_t codepoint, glyph_metrics& gm, IDWriteGlyphRunAnalysisPtr& analysis, glyph_adjustment& adjustment) const {
	const auto glyphIndex = resolve_glyph_index(m_dwrite, codepoint);
	if (!glyphIndex)
		return false;

	const auto it = m_info->GlyphAdjustments.find(glyphIndex);
	adjustment = it == m_info->GlyphAdjustments.end() ? glyph_adjustment{} : it->second;

	if (!try_get_glyph_index_metrics(glyphIndex, gm, analysis))
		return false;

	gm.AdvanceX += adjustment.AdvanceX;
	return true;
}

bool FontChanger::FixedSizeFont::directwrite_fixed_size_font::try_get_glyph_index_metrics(uint16_t glyphIndex, glyph_metrics& gm, IDWriteGlyphRunAnalysisPtr& analysis, float originX, float originY) const {
	try {
		DWRITE_GLYPH_METRICS dgm;
		success_or_throw(m_dwrite.Face->GetGdiCompatibleGlyphMetrics(
			m_info->Size, 1.0f, &m_info->Matrix,
			m_info->Params.MeasureMode == DWRITE_MEASURING_MODE_GDI_NATURAL ? TRUE : FALSE,
			&glyphIndex, 1, &dgm));

		float glyphAdvance{};
		DWRITE_GLYPH_OFFSET glyphOffset{};
		const DWRITE_GLYPH_RUN run{
			.fontFace = m_dwrite.Face,
			.fontEmSize = m_info->Size,
			.glyphCount = 1,
			.glyphIndices = &glyphIndex,
			.glyphAdvances = &glyphAdvance,
			.glyphOffsets = &glyphOffset,
			.isSideways = FALSE,
			.bidiLevel = 0,
		};

		auto renderMode = m_info->Params.RenderMode;
		if (renderMode == DWRITE_RENDERING_MODE1_DEFAULT) {
			DWRITE_RENDERING_MODE recommended;
			success_or_throw(m_dwrite.Face->GetRecommendedRenderingMode(m_info->Size, 1.f, m_info->Params.MeasureMode, nullptr, &recommended));
			renderMode = static_cast<DWRITE_RENDERING_MODE1>(recommended);
		}

		// Outlines are not rasterized by glyph run analysis.
		if (renderMode == DWRITE_RENDERING_MODE1_OUTLINE)
			renderMode = DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC;

		// The origin is given in pixels, after the transformation; the baseline origin of the run would be transformed too.
		auto matrix = m_info->Matrix;
		matrix.dx = originX;
		matrix.dy = originY;
		success_or_throw(m_dwrite.Factory3->CreateGlyphRunAnalysis(
			&run,
			&matrix,
			renderMode,
			m_info->Params.MeasureMode,
			m_info->Params.GridFitMode,
			DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE,
			0,
			0,
			&analysis));

		success_or_throw(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, gm.as_mutable_rect_pointer()));

		gm.AdvanceX = m_info->scale_from_font_unit(static_cast<float>(dgm.advanceWidth) * m_info->Matrix.m11);

		return true;
	} catch (...) {
		return false;
	}
}

bool FontChanger::FixedSizeFont::directwrite_fixed_size_font::try_get_glyph_metrics(char32_t codepoint, glyph_metrics& gm) const {
	IDWriteGlyphRunAnalysisPtr analysis;
	glyph_adjustment adjustment;
	if (!try_get_glyph_metrics(codepoint, gm, analysis, adjustment))
		return false;

	gm.translate(adjustment.PlacementX, ascent() + adjustment.PlacementY);
	return true;
}

bool FontChanger::FixedSizeFont::directwrite_fixed_size_font::try_get_glyph_index_metrics(uint32_t glyphIndex, float originX, float originY, glyph_metrics& gm) const {
	if (!glyphIndex || glyphIndex >= m_dwrite.Face->GetGlyphCount())
		return false;

	IDWriteGlyphRunAnalysisPtr analysis;
	return try_get_glyph_index_metrics(static_cast<uint16_t>(glyphIndex), gm, analysis, originX, originY);
}

bool FontChanger::FixedSizeFont::directwrite_fixed_size_font::try_get_glyph_index_ink_extent(uint32_t glyphIndex, float& x1, float& x2) const {
	if (!glyphIndex || glyphIndex >= m_dwrite.Face->GetGlyphCount())
		return false;

	const auto index = static_cast<uint16_t>(glyphIndex);
	DWRITE_GLYPH_METRICS dgm;
	if (FAILED(m_dwrite.Face->GetDesignGlyphMetrics(&index, 1, &dgm, FALSE)))
		return false;

	// The side bearings are of the outline, which the matrix scales horizontally.
	const auto scale = m_info->Size / static_cast<float>(m_info->Metrics.designUnitsPerEm) * m_info->Matrix.m11;
	x1 = static_cast<float>(dgm.leftSideBearing) * scale;
	x2 = (static_cast<float>(dgm.advanceWidth) - static_cast<float>(dgm.rightSideBearing)) * scale;
	if (x1 > x2)
		std::swap(x1, x2);
	return true;
}

bool FontChanger::FixedSizeFont::directwrite_fixed_size_font::draw_glyph_index(uint32_t glyphIndex, uint8_t* pBuf, size_t stride, float drawX, float drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const {
	if (!glyphIndex || glyphIndex >= m_dwrite.Face->GetGlyphCount())
		return false;

	// The glyph is rasterized at its origin, which may be between pixels.
	IDWriteGlyphRunAnalysisPtr analysis;
	glyph_metrics gm;
	if (!try_get_glyph_index_metrics(static_cast<uint16_t>(glyphIndex), gm, analysis, drawX, drawY))
		return false;

	auto src = gm;
	src.translate(-src.X1, -src.Y1);
	auto dest = gm;
	src.adjust_to_intersection(dest, src.width(), src.height(), destWidth, destHeight);
	if (src.is_effectively_empty() || dest.is_effectively_empty())
		return true;

	m_drawBuffer.resize(gm.area());
	success_or_throw(analysis->CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1, gm.as_const_rect_pointer(), &m_drawBuffer[0], static_cast<uint32_t>(m_drawBuffer.size())));

	util::bitmap_copy::to_l8()
		.from(&m_drawBuffer[0], gm.width(), gm.height(), 1, util::bitmap_vertical_direction::TopRowFirst)
		.to(pBuf, destWidth, destHeight, stride, util::bitmap_vertical_direction::TopRowFirst)
		.fore_color(fgColor)
		.fore_opacity(fgOpacity)
		.back_color(bgColor)
		.back_opacity(bgOpacity)
		.gamma_table(m_info->GammaTable)
		.copy(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);
	return true;
}

bool FontChanger::FixedSizeFont::directwrite_fixed_size_font::try_get_glyph_outline(char32_t codepoint, glyph_outline& outline) const {
	const auto glyphIndex = resolve_glyph_index(m_dwrite, codepoint);
	if (!glyphIndex)
		return false;

	const auto it = m_info->GlyphAdjustments.find(glyphIndex);
	const auto adjustment = it == m_info->GlyphAdjustments.end() ? glyph_adjustment{} : it->second;
	return try_get_glyph_index_outline(glyphIndex, static_cast<float>(adjustment.PlacementX), static_cast<float>(ascent() + adjustment.PlacementY), outline);
}

bool FontChanger::FixedSizeFont::directwrite_fixed_size_font::try_get_glyph_index_outline(uint32_t glyphIndex, float originX, float originY, glyph_outline& outline) const {
	if (!glyphIndex || glyphIndex >= m_dwrite.Face->GetGlyphCount())
		return false;

	class OutlineSink final : public IDWriteGeometrySink {
	public:
		glyph_outline& Outline;
		DWRITE_MATRIX Matrix;

		OutlineSink(glyph_outline& outline, const DWRITE_MATRIX& matrix)
			: Outline(outline)
			, Matrix(matrix) {}

		[[nodiscard]] glyph_outline::point Convert(const D2D1_POINT_2F& p) const {
			return {p.x * Matrix.m11 + p.y * Matrix.m21 + Matrix.dx, p.x * Matrix.m12 + p.y * Matrix.m22 + Matrix.dy};
		}

		STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override {
			if (!ppv)
				return E_INVALIDARG;
			if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteGeometrySink)) {
				*ppv = this;
				return S_OK;
			}
			*ppv = nullptr;
			return E_NOINTERFACE;
		}
		ULONG __stdcall AddRef() override { return 1; }
		ULONG __stdcall Release() override { return 0; }

		STDMETHOD_(void, SetFillMode)(D2D1_FILL_MODE fillMode) override {
			Outline.EvenOdd = fillMode == D2D1_FILL_MODE_ALTERNATE;
		}
		STDMETHOD_(void, SetSegmentFlags)(D2D1_PATH_SEGMENT vertexFlags) override {}
		STDMETHOD_(void, BeginFigure)(D2D1_POINT_2F startPoint, D2D1_FIGURE_BEGIN figureBegin) override {
			Outline.move_to(Convert(startPoint));
		}
		STDMETHOD_(void, AddLines)(const D2D1_POINT_2F* points, UINT32 pointsCount) override {
			for (UINT32 i = 0; i < pointsCount; i++)
				Outline.line_to(Convert(points[i]));
		}
		STDMETHOD_(void, AddBeziers)(const D2D1_BEZIER_SEGMENT* beziers, UINT32 beziersCount) override {
			for (UINT32 i = 0; i < beziersCount; i++)
				Outline.cubic_to(Convert(beziers[i].point1), Convert(beziers[i].point2), Convert(beziers[i].point3));
		}
		STDMETHOD_(void, EndFigure)(D2D1_FIGURE_END figureEnd) override {
			Outline.close();
		}
		STDMETHOD(Close)() override { return S_OK; }
	};

	auto matrix = m_info->Matrix;
	matrix.dx = originX;
	matrix.dy = originY;

	const auto index = static_cast<uint16_t>(glyphIndex);
	outline = {};
	OutlineSink sink(outline, matrix);
	if (FAILED(m_dwrite.Face->GetGlyphRunOutline(m_info->Size, &index, nullptr, nullptr, 1, FALSE, FALSE, &sink)))
		return false;
	outline.close();
	return true;
}

std::optional<FontChanger::FixedSizeFont::shaped_line> FontChanger::FixedSizeFont::directwrite_fixed_size_font::shape_line(std::u32string_view text, int letterSpacing) const {
	try {
		const auto text16 = util::unicode::convert<std::wstring>(text);

		IDWriteTextLayoutPtr layout;
		success_or_throw(m_dwrite.Factory->CreateTextLayout(text16.data(), static_cast<UINT32>(text16.size()), m_dwrite.Format, 9999999, 9999999, &layout));
		success_or_throw(layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));

		// A typography replaces the features on by default instead of adding to them; those stay on unless the font's say otherwise.
		IDWriteTypographyPtr typography;
		success_or_throw(m_dwrite.Factory->CreateTypography(&typography));
		for (const auto tag : {
			     DWRITE_FONT_FEATURE_TAG_STANDARD_LIGATURES,
			     DWRITE_FONT_FEATURE_TAG_CONTEXTUAL_LIGATURES,
			     DWRITE_FONT_FEATURE_TAG_CONTEXTUAL_ALTERNATES,
			     DWRITE_FONT_FEATURE_TAG_KERNING,
		     }) {
			if (std::ranges::none_of(m_info->Params.Features, [tag](const DWRITE_FONT_FEATURE& f) { return f.nameTag == tag; }))
				success_or_throw(typography->AddFontFeature({tag, 1}));
		}
		for (const auto& feature : m_info->Params.Features)
			success_or_throw(typography->AddFontFeature(feature));
		success_or_throw(layout->SetTypography(typography, {.startPosition = 0, .length = static_cast<UINT32>(text16.size())}));

		// Glyphs from fallback fonts would not be of this font; characters that it does not have are drawn as .notdef.
		if (IDWriteTextLayout2Ptr layout2; SUCCEEDED(layout.QueryInterface(decltype(layout2)::GetIID(), &layout2))) {
			if (IDWriteFactory2Ptr factory2; SUCCEEDED(m_dwrite.Factory->QueryInterface(decltype(factory2)::GetIID(), reinterpret_cast<void**>(&factory2)))) {
				IDWriteFontFallbackBuilderPtr builder;
				IDWriteFontFallbackPtr fallback;
				if (SUCCEEDED(factory2->CreateFontFallbackBuilder(&builder)) && SUCCEEDED(builder->CreateFontFallback(&fallback)))
					layout2->SetFontFallback(fallback);
			}
		}

		class ShapingRenderer final : public IDWriteTextRenderer {
		public:
			struct glyph {
				uint16_t Index;
				float X;
				float Y;
			};

			std::vector<glyph> Glyphs;
			std::optional<float> BaselineY;
			float AdvanceWidth = 0;

			STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override {
				if (!ppv)
					return E_INVALIDARG;

				if (riid == __uuidof(IUnknown)
					|| riid == __uuidof(IDWritePixelSnapping)
					|| riid == __uuidof(IDWriteTextRenderer)) {
					this->AddRef();
					*ppv = this;
					return S_OK;
				}

				return E_NOINTERFACE;
			}
			ULONG __stdcall AddRef() override {
				return 1;
			}
			ULONG __stdcall Release() override {
				return 0;
			}
			STDMETHOD(IsPixelSnappingDisabled)(_In_opt_ void* clientDrawingContext, _Out_ BOOL* isDisabled) override {
				*isDisabled = TRUE;
				return S_OK;
			}
			STDMETHOD(GetCurrentTransform)(_In_opt_ void* clientDrawingContext, _Out_ DWRITE_MATRIX* transform) override {
				*transform = {1, 0, 0, 1, 0, 0};
				return S_OK;
			}
			STDMETHOD(GetPixelsPerDip)(_In_opt_ void* clientDrawingContext, _Out_ FLOAT* pixelsPerDip) override {
				*pixelsPerDip = 1;
				return S_OK;
			}
			STDMETHOD(DrawGlyphRun)(
				_In_opt_ void* clientDrawingContext,
				FLOAT baselineOriginX,
				FLOAT baselineOriginY,
				DWRITE_MEASURING_MODE measuringMode,
				_In_ DWRITE_GLYPH_RUN const* glyphRun,
				_In_ DWRITE_GLYPH_RUN_DESCRIPTION const* glyphRunDescription,
				_In_opt_ IUnknown* clientDrawingEffect
			) override {
				if (!BaselineY)
					BaselineY = baselineOriginY;

				// Glyphs of right-to-left runs go leftwards from the origin of the run.
				const auto rtl = (glyphRun->bidiLevel & 1) != 0;
				auto pen = baselineOriginX;
				for (UINT32 i = 0; i < glyphRun->glyphCount; i++) {
					const auto advance = glyphRun->glyphAdvances ? glyphRun->glyphAdvances[i] : 0.f;
					const auto offset = glyphRun->glyphOffsets ? glyphRun->glyphOffsets[i] : DWRITE_GLYPH_OFFSET{};
					if (rtl)
						pen -= advance;
					Glyphs.push_back({
						.Index = glyphRun->glyphIndices[i],
						.X = pen + (rtl ? -offset.advanceOffset : offset.advanceOffset),
						.Y = baselineOriginY - offset.ascenderOffset,
					});
					if (!rtl)
						pen += advance;
					AdvanceWidth += advance;
				}
				return S_OK;
			}

			STDMETHOD(DrawUnderline)(_In_opt_ void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY, _In_ DWRITE_UNDERLINE const* underline, _In_opt_ IUnknown* clientDrawingEffect) override {
				return E_NOTIMPL;
			}
			STDMETHOD(DrawStrikethrough)(_In_opt_ void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY, _In_ DWRITE_STRIKETHROUGH const* strikethrough, _In_opt_ IUnknown* clientDrawingEffect) override {
				return E_NOTIMPL;
			}
			STDMETHOD(DrawInlineObject)(_In_opt_ void* clientDrawingContext, FLOAT originX, FLOAT originY, _In_ IDWriteInlineObject* inlineObject, BOOL isSideways, BOOL isRightToLeft, _In_opt_ IUnknown* clientDrawingEffect) override {
				return E_NOTIMPL;
			}
		} renderer;
		success_or_throw(layout->Draw(nullptr, &renderer, 0, 0));

		// The glyphs are transformed by the matrix as they are drawn, and so are their positions.
		const auto& m = m_info->Matrix;
		const auto baselineY = renderer.BaselineY.value_or(0.f);
		shaped_line res;
		for (size_t i = 0; i < renderer.Glyphs.size(); i++) {
			const auto x = renderer.Glyphs[i].X, y = renderer.Glyphs[i].Y - baselineY;
			res.Glyphs.push_back({
				.GlyphIndex = renderer.Glyphs[i].Index,
				.X = x * m.m11 + y * m.m21 + static_cast<float>(static_cast<int>(i) * letterSpacing),
				.Y = x * m.m12 + y * m.m22,
			});
		}
		res.AdvanceWidth = static_cast<int>(std::lround(renderer.AdvanceWidth * m.m11))
			+ (renderer.Glyphs.empty() ? 0 : static_cast<int>(renderer.Glyphs.size() - 1) * letterSpacing);
		return res;
	} catch (...) {
		return std::nullopt;
	}
}

const wchar_t* FontChanger::FixedSizeFont::directwrite_fixed_size_font::create_struct::get_grid_fit_mode_string() const {
	switch (GridFitMode) {
		case DWRITE_GRID_FIT_MODE_DEFAULT: return L"Default";
		case DWRITE_GRID_FIT_MODE_DISABLED: return L"Disabled";
		case DWRITE_GRID_FIT_MODE_ENABLED: return L"Enabled";
		default: return L"Invalid";
	}
}

const wchar_t* FontChanger::FixedSizeFont::directwrite_fixed_size_font::create_struct::get_rendering_mode_string() const {
	switch (RenderMode) {
		case DWRITE_RENDERING_MODE1_DEFAULT: return L"Default";
		case DWRITE_RENDERING_MODE1_ALIASED: return L"Aliased";
		case DWRITE_RENDERING_MODE1_GDI_CLASSIC: return L"GDI Classic";
		case DWRITE_RENDERING_MODE1_GDI_NATURAL: return L"GDI Natural";
		case DWRITE_RENDERING_MODE1_NATURAL: return L"Natural";
		case DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC: return L"Natural Symmetric";
		case DWRITE_RENDERING_MODE1_OUTLINE: return L"Outline";
		case DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC_DOWNSAMPLED: return L"Natural Symmetric Downsampled";
		default: return L"Invalid";
	}
}

const wchar_t* FontChanger::FixedSizeFont::directwrite_fixed_size_font::create_struct::get_measuring_mode_string() const {
	switch (MeasureMode) {
		case DWRITE_MEASURING_MODE_NATURAL: return L"Natural";
		case DWRITE_MEASURING_MODE_GDI_CLASSIC: return L"GDI Classic";
		case DWRITE_MEASURING_MODE_GDI_NATURAL: return L"GDI Natural";
		default: return L"Invalid";
	}
}

dwrite_font_table::operator bool() const {
	return m_bExists;
}

dwrite_font_table::~dwrite_font_table() {
	if (m_bExists)
		m_pFontFace->ReleaseFontTable(m_pTableContext);
}

dwrite_font_table::dwrite_font_table(IDWriteFontFace* pFace, uint32_t tag)
	: m_pFontFace(pFace, true) {
	success_or_throw(pFace->TryGetFontTable(tag, &m_pData, &m_nSize, &m_pTableContext, &m_bExists));
}

HRESULT __stdcall stream_based_dwrite_font_collection_loader::CreateEnumeratorFromKey(IDWriteFactory* factory, void const* collectionKey, uint32_t collectionKeySize, IDWriteFontFileEnumerator* * pFontFileEnumerator) noexcept {
	if (collectionKeySize != sizeof(std::shared_ptr<xivres::stream>))
		return E_INVALIDARG;


	*pFontFileEnumerator = stream_based_dwrite_font_collection_enumerator::New(IDWriteFactoryPtr(factory, true), *static_cast<const std::shared_ptr<xivres::stream>*>(collectionKey));
	return S_OK;
}

ULONG __stdcall stream_based_dwrite_font_collection_loader::Release() noexcept {
	return 0;
}

ULONG __stdcall stream_based_dwrite_font_collection_loader::AddRef() noexcept {
	return 1;
}

HRESULT __stdcall stream_based_dwrite_font_collection_loader::QueryInterface(REFIID riid, void** ppvObject) noexcept {
	if (riid == __uuidof(IUnknown))
		*ppvObject = static_cast<IUnknown*>(this);
	else if (riid == __uuidof(IDWriteFontCollectionLoader))
		*ppvObject = static_cast<IDWriteFontCollectionLoader*>(this);
	else
		*ppvObject = nullptr;

	if (!*ppvObject)
		return E_NOINTERFACE;

	AddRef();
	return S_OK;
}

stream_based_dwrite_font_collection_loader& stream_based_dwrite_font_collection_loader::GetInstance() {
	static stream_based_dwrite_font_collection_loader s_instance;
	return s_instance;
}

HRESULT __stdcall stream_based_dwrite_font_collection_loader::stream_based_dwrite_font_collection_enumerator::GetCurrentFontFile(IDWriteFontFile** pFontFile) noexcept {
	if (m_nCurrentFile != 0)
		return E_FAIL;

	return m_factory->CreateCustomFontFileReference(&m_stream, sizeof m_stream, &stream_based_dwrite_font_file_loader::GetInstance(), pFontFile);
}

HRESULT __stdcall stream_based_dwrite_font_collection_loader::stream_based_dwrite_font_collection_enumerator::MoveNext(BOOL* pHasCurrentFile) noexcept {
	if (m_nCurrentFile == -1) {
		m_nCurrentFile = 0;
		*pHasCurrentFile = TRUE;
	} else {
		m_nCurrentFile = 1;
		*pHasCurrentFile = FALSE;
	}
	return S_OK;
}

ULONG __stdcall stream_based_dwrite_font_collection_loader::stream_based_dwrite_font_collection_enumerator::Release() noexcept {
	const auto newRef = --m_nRef;
	if (!newRef)
		delete this;
	return newRef;
}

ULONG __stdcall stream_based_dwrite_font_collection_loader::stream_based_dwrite_font_collection_enumerator::AddRef() noexcept {
	return ++m_nRef;
}

HRESULT __stdcall stream_based_dwrite_font_collection_loader::stream_based_dwrite_font_collection_enumerator::QueryInterface(REFIID riid, void** ppvObject) noexcept {
	if (riid == __uuidof(IUnknown))
		*ppvObject = static_cast<IUnknown*>(this);
	else if (riid == __uuidof(IDWriteFontFileEnumerator))
		*ppvObject = static_cast<IDWriteFontFileEnumerator*>(this);
	else
		*ppvObject = nullptr;

	if (!*ppvObject)
		return E_NOINTERFACE;

	AddRef();
	return S_OK;
}

stream_based_dwrite_font_collection_loader::stream_based_dwrite_font_collection_enumerator* stream_based_dwrite_font_collection_loader::stream_based_dwrite_font_collection_enumerator::New(IDWriteFactoryPtr factoryPtr, std::shared_ptr<xivres::stream> pStream) {
	return new stream_based_dwrite_font_collection_enumerator(std::move(factoryPtr), std::move(pStream));
}

stream_based_dwrite_font_collection_loader::stream_based_dwrite_font_collection_enumerator::stream_based_dwrite_font_collection_enumerator(IDWriteFactoryPtr factoryPtr, std::shared_ptr<xivres::stream> pStream)
	: m_factory(std::move(factoryPtr))
	, m_stream(std::move(pStream)) {}

HRESULT __stdcall stream_based_dwrite_font_file_loader::CreateStreamFromKey(void const* fontFileReferenceKey, uint32_t fontFileReferenceKeySize, IDWriteFontFileStream* * pFontFileStream) noexcept {
	if (fontFileReferenceKeySize != sizeof(std::shared_ptr<xivres::stream>))
		return E_INVALIDARG;

	*pFontFileStream = stream_based_dwrite_font_file_stream::New(*static_cast<const std::shared_ptr<xivres::stream>*>(fontFileReferenceKey));
	return S_OK;
}

ULONG __stdcall stream_based_dwrite_font_file_loader::Release() noexcept {
	return 0;
}

ULONG __stdcall stream_based_dwrite_font_file_loader::AddRef() noexcept {
	return 1;
}

HRESULT __stdcall stream_based_dwrite_font_file_loader::QueryInterface(REFIID riid, void** ppvObject) noexcept {
	if (riid == __uuidof(IUnknown))
		*ppvObject = static_cast<IUnknown*>(this);
	else if (riid == __uuidof(IDWriteFontFileLoader))
		*ppvObject = static_cast<IDWriteFontFileLoader*>(this);
	else
		*ppvObject = nullptr;

	if (!*ppvObject)
		return E_NOINTERFACE;

	AddRef();
	return S_OK;
}

stream_based_dwrite_font_file_loader& stream_based_dwrite_font_file_loader::GetInstance() {
	static stream_based_dwrite_font_file_loader s_instance;
	return s_instance;
}

HRESULT __stdcall stream_based_dwrite_font_file_loader::stream_based_dwrite_font_file_stream::GetLastWriteTime(uint64_t* pLastWriteTime) noexcept {
	*pLastWriteTime = 0;
	return E_NOTIMPL; // E_NOTIMPL by design -- see method documentation in dwrite.h.
}

HRESULT __stdcall stream_based_dwrite_font_file_loader::stream_based_dwrite_font_file_stream::GetFileSize(uint64_t* pFileSize) noexcept {
	*pFileSize = static_cast<uint64_t>(m_stream->size());
	return S_OK;
}

void __stdcall stream_based_dwrite_font_file_loader::stream_based_dwrite_font_file_stream::ReleaseFileFragment(void* fragmentContext) noexcept {
	if (fragmentContext)
		delete static_cast<std::vector<uint8_t>*>(fragmentContext);
}

HRESULT __stdcall stream_based_dwrite_font_file_loader::stream_based_dwrite_font_file_stream::ReadFileFragment(void const** pFragmentStart, uint64_t fileOffset, uint64_t fragmentSize, void** pFragmentContext) noexcept {
	*pFragmentContext = nullptr;
	*pFragmentStart = nullptr;

	if (const auto pMemoryStream = dynamic_cast<xivres::memory_stream*>(m_stream.get())) {
		try {
			*pFragmentStart = pMemoryStream->as_span(static_cast<std::streamoff>(fileOffset), static_cast<std::streamsize>(fragmentSize)).data();
			return S_OK;
		} catch (const std::out_of_range&) {
			return E_INVALIDARG;
		}
	} else {
		const auto size = static_cast<uint64_t>(m_stream->size());
		if (fileOffset <= size && fileOffset + fragmentSize <= size && fragmentSize <= (std::numeric_limits<uint32_t>::max)()) {
			auto pVec = new std::vector<uint8_t>();
			try {
				pVec->resize(static_cast<size_t>(fragmentSize));
				if (m_stream->read(static_cast<std::streamoff>(fileOffset), pVec->data(), static_cast<std::streamsize>(fragmentSize)) == static_cast<std::streamsize>(fragmentSize)) {
					*pFragmentStart = pVec->data();
					*pFragmentContext = pVec;
					return S_OK;
				}
			} catch (...) {
			}
			delete pVec;
			return E_FAIL;
		} else
			return E_INVALIDARG;
	}
}

ULONG __stdcall stream_based_dwrite_font_file_loader::stream_based_dwrite_font_file_stream::Release() noexcept {
	const auto newRef = --m_nRef;
	if (!newRef)
		delete this;
	return newRef;
}

ULONG __stdcall stream_based_dwrite_font_file_loader::stream_based_dwrite_font_file_stream::AddRef() noexcept {
	return ++m_nRef;
}

HRESULT __stdcall stream_based_dwrite_font_file_loader::stream_based_dwrite_font_file_stream::QueryInterface(REFIID riid, void** ppvObject) noexcept {
	if (riid == __uuidof(IUnknown))
		*ppvObject = static_cast<IUnknown*>(this);
	else if (riid == __uuidof(IDWriteFontFileStream))
		*ppvObject = static_cast<IDWriteFontFileStream*>(this);
	else
		*ppvObject = nullptr;

	if (!*ppvObject)
		return E_NOINTERFACE;

	AddRef();
	return S_OK;
}

stream_based_dwrite_font_file_loader::stream_based_dwrite_font_file_stream* stream_based_dwrite_font_file_loader::stream_based_dwrite_font_file_stream::New(std::shared_ptr<xivres::stream> pStream) {
	return new stream_based_dwrite_font_file_stream(std::move(pStream));
}

stream_based_dwrite_font_file_loader::stream_based_dwrite_font_file_stream::stream_based_dwrite_font_file_stream(std::shared_ptr<xivres::stream> pStream)
	: m_stream(std::move(pStream)) {}
