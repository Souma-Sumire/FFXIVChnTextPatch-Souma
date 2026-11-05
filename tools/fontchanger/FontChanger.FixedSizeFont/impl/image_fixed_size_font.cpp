#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "../include/FontChanger.FixedSizeFont/image_fixed_size_font.h"

using namespace xivres;

#include <cmath>
#include <functional>
#include <stdexcept>

#include <comdef.h>
#include <d2d1_3.h>
#include <d3d11.h>
#include <Shlwapi.h>
#include <xmllite.h>

#include "xivres/util.bitmap_copy.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "xmllite.lib")

_COM_SMARTPTR_TYPEDEF(ID2D1Factory1, __uuidof(ID2D1Factory1));
_COM_SMARTPTR_TYPEDEF(ID2D1Device, __uuidof(ID2D1Device));
_COM_SMARTPTR_TYPEDEF(ID2D1DeviceContext, __uuidof(ID2D1DeviceContext));
_COM_SMARTPTR_TYPEDEF(ID2D1DeviceContext5, __uuidof(ID2D1DeviceContext5));
_COM_SMARTPTR_TYPEDEF(ID2D1Bitmap1, __uuidof(ID2D1Bitmap1));
_COM_SMARTPTR_TYPEDEF(ID2D1SvgDocument, __uuidof(ID2D1SvgDocument));
_COM_SMARTPTR_TYPEDEF(ID2D1SvgElement, __uuidof(ID2D1SvgElement));
_COM_SMARTPTR_TYPEDEF(ID3D11Device, __uuidof(ID3D11Device));
_COM_SMARTPTR_TYPEDEF(IDXGIDevice, __uuidof(IDXGIDevice));
_COM_SMARTPTR_TYPEDEF(IXmlReader, __uuidof(IXmlReader));

namespace {
	void success_or_throw(HRESULT hr, const char* what) {
		if (FAILED(hr))
			throw std::runtime_error(std::format("{} failed (HRESULT=0x{:08X})", what, static_cast<uint32_t>(hr)));
	}

	IStreamPtr create_memory_stream(std::string_view data) {
		IStreamPtr stream(SHCreateMemStream(reinterpret_cast<const BYTE*>(data.data()), static_cast<UINT>(data.size())), false);
		if (!stream)
			throw std::bad_alloc();
		return stream;
	}

	// Direct2D on a WARP device of its own for each thread, which needs neither COM nor a GPU, and gives the same pixels
	// on any computer. Device contexts cannot be shared by threads.
	class d2d_context {
		ID3D11DevicePtr m_device3d;
		ID2D1DevicePtr m_device;
		ID2D1DeviceContext5Ptr m_context;
		ID2D1Bitmap1Ptr m_target;
		ID2D1Bitmap1Ptr m_readback;
		UINT32 m_width = 0;
		UINT32 m_height = 0;

		static ID2D1Factory1* factory() {
			static const auto s_factory = [] {
				ID2D1Factory1Ptr factory;
				const D2D1_FACTORY_OPTIONS options{};
				success_or_throw(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory1), &options, reinterpret_cast<void**>(&factory)), "D2D1CreateFactory");
				return factory;
			}();
			return s_factory;
		}

		void create_device() {
			m_device3d = nullptr;
			m_device = nullptr;
			m_context = nullptr;
			m_target = m_readback = nullptr;
			m_width = m_height = 0;

			success_or_throw(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &m_device3d, nullptr, nullptr), "D3D11CreateDevice");

			IDXGIDevicePtr dxgiDevice;
			success_or_throw(m_device3d.QueryInterface(__uuidof(IDXGIDevice), &dxgiDevice), "QueryInterface(IDXGIDevice)");
			success_or_throw(factory()->CreateDevice(dxgiDevice, &m_device), "ID2D1Factory1::CreateDevice");

			ID2D1DeviceContextPtr context;
			success_or_throw(m_device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context), "ID2D1Device::CreateDeviceContext");

			// SVG documents need Windows 10 1703 or later.
			success_or_throw(context.QueryInterface(__uuidof(ID2D1DeviceContext5), &m_context), "QueryInterface(ID2D1DeviceContext5)");
			m_context->SetUnitMode(D2D1_UNIT_MODE_PIXELS);
		}

		void ensure_size(UINT32 width, UINT32 height) {
			if (width <= m_width && height <= m_height)
				return;

			m_width = (std::max)(m_width, (width + 63) & ~63u);
			m_height = (std::max)(m_height, (height + 63) & ~63u);
			const auto pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
			const auto targetProps = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, pixelFormat);
			const auto readbackProps = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, pixelFormat);
			m_target = m_readback = nullptr;
			success_or_throw(m_context->CreateBitmap({m_width, m_height}, nullptr, 0, targetProps, &m_target), "CreateBitmap(target)");
			success_or_throw(m_context->CreateBitmap({m_width, m_height}, nullptr, 0, readbackProps, &m_readback), "CreateBitmap(readback)");
		}

	public:
		static d2d_context& get() {
			thread_local d2d_context s_context;
			return s_context;
		}

		[[nodiscard]] ID2D1DeviceContext5* context() {
			if (!m_context)
				create_device();
			return m_context;
		}

		// Draws into a transparent target of the given size, and returns its premultiplied BGRA pixels.
		void render(int width, int height, const std::function<void(ID2D1DeviceContext5*)>& draw, std::vector<uint32_t>& pixels) {
			if (width <= 0 || height <= 0)
				return;

			const auto ctx = context();
			ensure_size(static_cast<UINT32>(width), static_cast<UINT32>(height));

			ctx->SetTarget(m_target);
			ctx->BeginDraw();
			ctx->Clear(D2D1::ColorF(0, 0, 0, 0));
			ctx->SetTransform(D2D1::Matrix3x2F::Identity());
			draw(ctx);
			const auto hr = ctx->EndDraw();
			ctx->SetTarget(nullptr);
			if (hr == D2DERR_RECREATE_TARGET)
				create_device();
			success_or_throw(hr, "ID2D1DeviceContext::EndDraw");

			const D2D1_POINT_2U origin{0, 0};
			const D2D1_RECT_U rect{0, 0, static_cast<UINT32>(width), static_cast<UINT32>(height)};
			success_or_throw(m_readback->CopyFromBitmap(&origin, m_target, &rect), "ID2D1Bitmap::CopyFromBitmap");

			D2D1_MAPPED_RECT mapped;
			success_or_throw(m_readback->Map(D2D1_MAP_OPTIONS_READ, &mapped), "ID2D1Bitmap1::Map");
			pixels.resize(static_cast<size_t>(width) * height);
			for (int y = 0; y < height; y++)
				memcpy(&pixels[static_cast<size_t>(y) * width], mapped.bits + static_cast<size_t>(y) * mapped.pitch, static_cast<size_t>(width) * 4);
			m_readback->Unmap();
		}
	};

	// Opacity less luminance, of premultiplied BGRA.
	uint8_t pixel_to_coverage(uint32_t bgra) {
		const auto b = static_cast<int>(bgra & 0xFF), g = static_cast<int>(bgra >> 8 & 0xFF), r = static_cast<int>(bgra >> 16 & 0xFF), a = static_cast<int>(bgra >> 24);
		const auto luminance = (2126 * r + 7152 * g + 722 * b + 5000) / 10000;
		return static_cast<uint8_t>(std::clamp(a - luminance, 0, 255));
	}

	// Numbers of lengths in pixels or without units; others are not taken.
	std::optional<float> parse_length(std::wstring_view s) {
		while (!s.empty() && iswspace(s.front()))
			s.remove_prefix(1);
		while (!s.empty() && iswspace(s.back()))
			s.remove_suffix(1);
		if (s.ends_with(L"px"))
			s.remove_suffix(2);
		if (s.empty())
			return std::nullopt;

		const std::wstring str(s);
		wchar_t* end;
		const auto value = std::wcstof(str.c_str(), &end);
		if (end == str.c_str() || *end || !std::isfinite(value))
			return std::nullopt;
		return value;
	}

	std::vector<float> parse_numbers(std::wstring_view s) {
		std::wstring str(s);
		for (auto& c : str) {
			if (c == L',')
				c = L' ';
		}
		std::vector<float> res;
		for (const wchar_t* p = str.c_str(); *p;) {
			wchar_t* end;
			const auto value = std::wcstof(p, &end);
			if (end == p)
				break;
			res.push_back(value);
			p = end;
		}
		return res;
	}

	// Maps user units to pixels as a matrix of Direct2D, which transforms row vectors.
	D2D1::Matrix3x2F to_d2d_matrix(const std::array<float, 6>& m) {
		return D2D1::Matrix3x2F(m[0], m[3], m[1], m[4], m[2], m[5]);
	}

	// A large viewport and viewBox of the same size map user units to the viewport one to one, so that the transformation
	// of the device context alone decides where the drawing goes. Percentages of the viewport do not mean anything here.
	constexpr float SvgViewportHalfSize = 100000.f;

	void draw_svg(ID2D1DeviceContext5* ctx, std::string_view svg, const std::array<float, 6>& m) {
		ID2D1SvgDocumentPtr document;
		success_or_throw(ctx->CreateSvgDocument(create_memory_stream(svg), {2 * SvgViewportHalfSize, 2 * SvgViewportHalfSize}, &document), "CreateSvgDocument");

		// The reference layer of exported glyphs is there to be traced over, not drawn.
		if (ID2D1SvgElementPtr reference; SUCCEEDED(document->FindElementById(L"xivfont-reference", &reference)) && reference) {
			ID2D1SvgElementPtr parent;
			reference->GetParent(&parent);
			if (parent)
				parent->RemoveChild(reference);
		}

		ID2D1SvgElementPtr root;
		document->GetRoot(&root);
		if (!root)
			throw std::runtime_error("The SVG document has no root");

		const D2D1_SVG_VIEWBOX viewBox{-SvgViewportHalfSize, -SvgViewportHalfSize, 2 * SvgViewportHalfSize, 2 * SvgViewportHalfSize};
		const D2D1_SVG_LENGTH length{2 * SvgViewportHalfSize, D2D1_SVG_LENGTH_UNITS_NUMBER};
		const D2D1_SVG_PRESERVE_ASPECT_RATIO aspectRatio{FALSE, D2D1_SVG_ASPECT_ALIGN_NONE, D2D1_SVG_ASPECT_SCALING_MEET};
		success_or_throw(root->SetAttributeValue(L"viewBox", D2D1_SVG_ATTRIBUTE_POD_TYPE_VIEWBOX, &viewBox, sizeof viewBox), "SetAttributeValue(viewBox)");
		success_or_throw(root->SetAttributeValue(L"width", D2D1_SVG_ATTRIBUTE_POD_TYPE_LENGTH, &length, sizeof length), "SetAttributeValue(width)");
		success_or_throw(root->SetAttributeValue(L"height", D2D1_SVG_ATTRIBUTE_POD_TYPE_LENGTH, &length, sizeof length), "SetAttributeValue(height)");
		success_or_throw(root->SetAttributeValue(L"preserveAspectRatio", D2D1_SVG_ATTRIBUTE_POD_TYPE_PRESERVE_ASPECT_RATIO, &aspectRatio, sizeof aspectRatio), "SetAttributeValue(preserveAspectRatio)");
		root->RemoveAttribute(L"x");
		root->RemoveAttribute(L"y");

		ctx->SetTransform(D2D1::Matrix3x2F::Translation(-SvgViewportHalfSize, -SvgViewportHalfSize) * to_d2d_matrix(m));
		ctx->DrawSvgDocument(document);
	}

	struct pixel_rect {
		int X1, Y1, X2, Y2;
	};

	// Pixels that a rectangle in user units covers after the transformation, with a pixel to spare for antialiasing.
	pixel_rect transform_bounds(const std::array<float, 6>& m, float x1, float y1, float x2, float y2) {
		auto minX = (std::numeric_limits<float>::max)(), minY = minX;
		auto maxX = (std::numeric_limits<float>::lowest)(), maxY = maxX;
		for (const auto [x, y] : {std::pair{x1, y1}, {x2, y1}, {x1, y2}, {x2, y2}}) {
			const auto px = m[0] * x + m[1] * y + m[2], py = m[3] * x + m[4] * y + m[5];
			minX = (std::min)(minX, px), maxX = (std::max)(maxX, px);
			minY = (std::min)(minY, py), maxY = (std::max)(maxY, py);
		}
		return {
			static_cast<int>(std::floor(minX)) - 1,
			static_cast<int>(std::floor(minY)) - 1,
			static_cast<int>(std::ceil(maxX)) + 1,
			static_cast<int>(std::ceil(maxY)) + 1,
		};
	}

	uint8_t bitmap_pixel_to_coverage(uint32_t bgra, FontChanger::FixedSizeFont::image_coverage_mode mode) {
		const auto b = static_cast<int>(bgra & 0xFF), g = static_cast<int>(bgra >> 8 & 0xFF), r = static_cast<int>(bgra >> 16 & 0xFF), a = static_cast<int>(bgra >> 24);
		const auto luminance = (2126 * r + 7152 * g + 722 * b + 5000) / 10000;
		switch (mode) {
			case FontChanger::FixedSizeFont::image_coverage_mode::Darkness:
				return static_cast<uint8_t>((255 - luminance) * a / 255);
			case FontChanger::FixedSizeFont::image_coverage_mode::Brightness:
				return static_cast<uint8_t>(luminance * a / 255);
			default:
				return static_cast<uint8_t>(a);
		}
	}
}

bool FontChanger::FixedSizeFont::image_fixed_size_font::try_read_svg_metrics(std::string_view svg, svg_metrics& metrics) {
	metrics = {};
	try {
		IXmlReaderPtr reader;
		success_or_throw(CreateXmlReader(__uuidof(IXmlReader), reinterpret_cast<void**>(&reader), nullptr), "CreateXmlReader");
		success_or_throw(reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Parse), "IXmlReader::SetProperty");
		success_or_throw(reader->SetInput(create_memory_stream(svg)), "IXmlReader::SetInput");

		XmlNodeType nodeType;
		while (reader->Read(&nodeType) == S_OK) {
			if (nodeType != XmlNodeType_Element)
				continue;

			const wchar_t* name;
			success_or_throw(reader->GetLocalName(&name, nullptr), "IXmlReader::GetLocalName");
			if (std::wstring_view(name) != L"svg")
				return false;

			std::optional<float> viewBoxWidth, width;
			for (auto hr = reader->MoveToFirstAttribute(); hr == S_OK; hr = reader->MoveToNextAttribute()) {
				const wchar_t* prefix;
				const wchar_t* value;
				success_or_throw(reader->GetPrefix(&prefix, nullptr), "IXmlReader::GetPrefix");
				success_or_throw(reader->GetLocalName(&name, nullptr), "IXmlReader::GetLocalName");
				success_or_throw(reader->GetValue(&value, nullptr), "IXmlReader::GetValue");
				if (*prefix)
					continue;

				const std::wstring_view n(name);
				if (n == L"data-xivfont-units-per-em") {
					if (const auto v = parse_length(value); v && *v > 0)
						metrics.UnitsPerEm = v;
				} else if (n == L"data-xivfont-baseline") {
					metrics.BaselineY = parse_length(value);
				} else if (n == L"data-xivfont-advance") {
					metrics.Advance = parse_length(value);
				} else if (n == L"viewBox") {
					if (const auto numbers = parse_numbers(value); numbers.size() == 4 && numbers[2] > 0)
						viewBoxWidth = numbers[2];
				} else if (n == L"width") {
					width = parse_length(value);
				}
			}
			if (!metrics.Advance)
				metrics.Advance = viewBoxWidth ? viewBoxWidth : width;
			return true;
		}
	} catch (const std::exception&) {
		// fall through
	}
	return false;
}

void FontChanger::FixedSizeFont::image_fixed_size_font::draw_svg_coverage(std::string_view svg, const std::array<float, 6>& m, int width, int height, std::span<uint8_t> coverage) {
	if (width <= 0 || height <= 0)
		return;
	if (coverage.size() < static_cast<size_t>(width) * height)
		throw std::invalid_argument("coverage is too small");

	std::vector<uint32_t> pixels;
	d2d_context::get().render(width, height, [&](ID2D1DeviceContext5* ctx) { draw_svg(ctx, svg, m); }, pixels);
	for (size_t i = 0; i < pixels.size(); i++)
		coverage[i] = pixel_to_coverage(pixels[i]);
}

FontChanger::FixedSizeFont::image_fixed_size_font::image_fixed_size_font(create_struct params, float size, float gamma, const font_render_transformation_matrix& matrix) {
	auto info = std::make_shared<struct info>();
	info->Size = size;
	info->Matrix = matrix;
	info->GammaTable = util::bitmap_copy::create_gamma_table(gamma);

	// Vertical metrics are subject to the vertical scale of the transformation, as the glyphs are.
	const auto scale = params.UnitsPerEm > 0 ? size / params.UnitsPerEm : 0.f;
	info->Ascent = static_cast<int>(std::lround(params.Ascent * scale * std::abs(matrix.M22)));
	info->LineHeight = static_cast<int>(std::lround(params.LineHeight * scale * std::abs(matrix.M22)));
	for (const auto& [pair, value] : params.KerningPairs) {
		if (const auto px = static_cast<int>(std::lround(value * scale * matrix.M11)))
			info->KerningPairs.emplace(pair, px);
	}
	for (const auto& [c, _] : params.Glyphs)
		info->Codepoints.insert(c);
	info->Params = std::move(params);
	m_info = std::move(info);
}

FontChanger::FixedSizeFont::image_fixed_size_font::image_fixed_size_font()
	: image_fixed_size_font({}, 0.f, 1.f, {1.f, 0.f, 0.f, 1.f}) {}

FontChanger::FixedSizeFont::image_fixed_size_font::image_fixed_size_font(const image_fixed_size_font& r) = default;
FontChanger::FixedSizeFont::image_fixed_size_font::image_fixed_size_font(image_fixed_size_font&& r) noexcept = default;
FontChanger::FixedSizeFont::image_fixed_size_font& FontChanger::FixedSizeFont::image_fixed_size_font::operator=(const image_fixed_size_font& r) = default;
FontChanger::FixedSizeFont::image_fixed_size_font& FontChanger::FixedSizeFont::image_fixed_size_font::operator=(image_fixed_size_font&& r) noexcept = default;

std::string FontChanger::FixedSizeFont::image_fixed_size_font::family_name() const {
	return m_info->Params.FamilyName;
}

std::string FontChanger::FixedSizeFont::image_fixed_size_font::subfamily_name() const {
	return {};
}

float FontChanger::FixedSizeFont::image_fixed_size_font::font_size() const {
	return m_info->Size;
}

int FontChanger::FixedSizeFont::image_fixed_size_font::ascent() const {
	return m_info->Ascent;
}

int FontChanger::FixedSizeFont::image_fixed_size_font::line_height() const {
	return m_info->LineHeight;
}

const std::set<char32_t>& FontChanger::FixedSizeFont::image_fixed_size_font::all_codepoints() const {
	return m_info->Codepoints;
}

bool FontChanger::FixedSizeFont::image_fixed_size_font::try_get_glyph_metrics(char32_t codepoint, glyph_metrics& gm) const {
	const auto glyph = get_rendered_glyph(codepoint);
	if (!glyph)
		return false;
	gm = glyph->Metrics;
	return true;
}

const std::map<std::pair<char32_t, char32_t>, int>& FontChanger::FixedSizeFont::image_fixed_size_font::all_kerning_pairs() const {
	return m_info->KerningPairs;
}

bool FontChanger::FixedSizeFont::image_fixed_size_font::draw(char32_t codepoint, util::b8g8r8a8* pBuf, int drawX, int drawY, int destWidth, int destHeight, util::b8g8r8a8 fgColor, util::b8g8r8a8 bgColor) const {
	const auto glyph = get_rendered_glyph(codepoint);
	if (!glyph)
		return false;

	auto dest = glyph->Metrics;
	dest.translate(drawX, drawY);
	auto src = dest;
	src.translate(-src.X1, -src.Y1);
	src.adjust_to_intersection(dest, src.width(), src.height(), destWidth, destHeight);
	if (src.is_effectively_empty() || dest.is_effectively_empty())
		return true;

	util::bitmap_copy::to_b8g8r8a8()
		.from(glyph->Alpha.data(), glyph->Metrics.width(), glyph->Metrics.height(), 1, util::bitmap_vertical_direction::TopRowFirst)
		.to(pBuf, destWidth, destHeight, util::bitmap_vertical_direction::TopRowFirst)
		.fore_color(fgColor)
		.back_color(bgColor)
		.gamma_table(m_info->GammaTable)
		.copy(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);
	return true;
}

bool FontChanger::FixedSizeFont::image_fixed_size_font::draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const {
	const auto glyph = get_rendered_glyph(codepoint);
	if (!glyph)
		return false;

	auto dest = glyph->Metrics;
	dest.translate(drawX, drawY);
	auto src = dest;
	src.translate(-src.X1, -src.Y1);
	src.adjust_to_intersection(dest, src.width(), src.height(), destWidth, destHeight);
	if (src.is_effectively_empty() || dest.is_effectively_empty())
		return true;

	util::bitmap_copy::to_l8()
		.from(glyph->Alpha.data(), glyph->Metrics.width(), glyph->Metrics.height(), 1, util::bitmap_vertical_direction::TopRowFirst)
		.to(pBuf, destWidth, destHeight, stride, util::bitmap_vertical_direction::TopRowFirst)
		.fore_color(fgColor)
		.fore_opacity(fgOpacity)
		.back_color(bgColor)
		.back_opacity(bgOpacity)
		.gamma_table(m_info->GammaTable)
		.copy(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);
	return true;
}

std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font> FontChanger::FixedSizeFont::image_fixed_size_font::get_threadsafe_view() const {
	return std::make_shared<image_fixed_size_font>(*this);
}

const FontChanger::FixedSizeFont::fixed_size_font* FontChanger::FixedSizeFont::image_fixed_size_font::get_base_font(char32_t codepoint) const {
	return m_info->Codepoints.contains(codepoint) ? this : nullptr;
}

std::shared_ptr<const FontChanger::FixedSizeFont::image_fixed_size_font::rendered_glyph> FontChanger::FixedSizeFont::image_fixed_size_font::get_rendered_glyph(char32_t codepoint) const {
	const auto it = m_info->Params.Glyphs.find(codepoint);
	if (it == m_info->Params.Glyphs.end())
		return nullptr;

	{
		const auto lock = std::scoped_lock(m_info->GlyphsMtx);
		if (const auto cached = m_info->Glyphs.find(codepoint); cached != m_info->Glyphs.end())
			return cached->second;
	}

	std::shared_ptr<const rendered_glyph> glyph;
	try {
		glyph = std::make_shared<rendered_glyph>(render(it->second));
	} catch (const std::exception&) {
		// A file that cannot be drawn leaves the glyph empty.
		glyph = std::make_shared<rendered_glyph>();
	}

	const auto lock = std::scoped_lock(m_info->GlyphsMtx);
	return m_info->Glyphs.emplace(codepoint, std::move(glyph)).first->second;
}

FontChanger::FixedSizeFont::image_fixed_size_font::rendered_glyph FontChanger::FixedSizeFont::image_fixed_size_font::render(const image_glyph_source& source) const {
	const auto& params = m_info->Params;
	const auto& matrix = m_info->Matrix;

	float unitsPerEm, baselineY, advance;
	float x1, y1, x2, y2;
	svg_metrics svgMetrics;
	if (source.Svg) {
		if (!try_read_svg_metrics(*source.Svg, svgMetrics))
			throw std::runtime_error("Invalid SVG document");
		unitsPerEm = params.UnitsPerEmOverridesAttribute ? params.UnitsPerEm : svgMetrics.UnitsPerEm.value_or(params.UnitsPerEm);
		baselineY = params.BaselineYOverridesAttribute ? params.BaselineY : svgMetrics.BaselineY.value_or(params.BaselineY);
		advance = svgMetrics.Advance.value_or(unitsPerEm);

		// Drawings may go past the line box; this much room is left around it, and the ink is trimmed afterwards.
		x1 = -unitsPerEm;
		x2 = (std::max)(advance, 0.f) + unitsPerEm;
		y1 = baselineY - 2 * unitsPerEm;
		y2 = baselineY + unitsPerEm;
	} else if (source.Pixels && source.Width > 0 && source.Height > 0) {
		unitsPerEm = params.BitmapUnitsPerEm;
		baselineY = params.BitmapBaselineY;
		advance = source.Advance.value_or(static_cast<float>(source.Width));
		x1 = static_cast<float>(-source.OriginX);
		y1 = static_cast<float>(-source.OriginY);
		x2 = x1 + static_cast<float>(source.Width);
		y2 = y1 + static_cast<float>(source.Height);
	} else {
		throw std::runtime_error("Glyph has nothing to draw");
	}
	if (!(unitsPerEm > 0))
		throw std::runtime_error("Invalid units per em");

	// Units to pixels relative to the origin on the top of the line: scaled, moved so that the baseline is at the
	// origin, transformed, and moved down by the ascent.
	const auto s = m_info->Size / unitsPerEm;
	std::array<float, 6> m{
		matrix.M11 * s, matrix.M12 * s, -matrix.M12 * s * baselineY,
		matrix.M21 * s, matrix.M22 * s, -matrix.M22 * s * baselineY + static_cast<float>(m_info->Ascent),
	};
	const auto advancePx = static_cast<int>(std::lround(advance * s * matrix.M11));

	const auto bounds = transform_bounds(m, x1, y1, x2, y2);
	const auto width = bounds.X2 - bounds.X1, height = bounds.Y2 - bounds.Y1;
	if (width <= 0 || height <= 0 || width > 8192 || height > 8192)
		return {.Metrics = {.AdvanceX = advancePx}};
	m[2] -= static_cast<float>(bounds.X1);
	m[5] -= static_cast<float>(bounds.Y1);

	std::vector<uint8_t> coverage(static_cast<size_t>(width) * height);
	if (source.Svg) {
		draw_svg_coverage(*source.Svg, m, width, height, coverage);
	} else {
		auto mode = params.BitmapCoverage;
		const auto& pixels = *source.Pixels;
		if (mode == image_coverage_mode::Auto)
			mode = std::ranges::any_of(pixels, [](uint32_t p) { return (p >> 24) != 0xFF; }) ? image_coverage_mode::Alpha : image_coverage_mode::Darkness;

		// The coverage is drawn as black with that opacity, premultiplied.
		std::vector<uint32_t> premultiplied(pixels.size());
		for (size_t i = 0; i < pixels.size(); i++)
			premultiplied[i] = static_cast<uint32_t>(bitmap_pixel_to_coverage(pixels[i], mode)) << 24;

		// Pixel art keeps its pixels when it is only moved, or scaled by whole numbers.
		const auto axisAligned = matrix.M12 == 0.f && matrix.M21 == 0.f;  // NOLINT(clang-diagnostic-float-equal)
		const auto wholeScale = axisAligned && std::abs(m[0] - std::round(m[0])) < 1e-4f && std::abs(m[4] - std::round(m[4])) < 1e-4f;
		const auto interpolation = wholeScale ? D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR : D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC;

		std::vector<uint32_t> rendered;
		d2d_context::get().render(width, height, [&](ID2D1DeviceContext5* ctx) {
			ID2D1Bitmap1Ptr bitmap;
			const auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
			success_or_throw(ctx->CreateBitmap({static_cast<UINT32>(source.Width), static_cast<UINT32>(source.Height)}, premultiplied.data(), static_cast<UINT32>(source.Width) * 4, props, &bitmap), "CreateBitmap");
			ctx->SetTransform(to_d2d_matrix(m));
			const auto dest = D2D1::RectF(x1, y1, x2, y2);
			ctx->DrawBitmap(bitmap, &dest, 1.f, interpolation, nullptr, nullptr);
		}, rendered);
		for (size_t i = 0; i < rendered.size(); i++)
			coverage[i] = static_cast<uint8_t>(rendered[i] >> 24);
	}

	// Trim the empty rows and columns.
	auto tx1 = width, ty1 = height, tx2 = 0, ty2 = 0;
	for (auto y = 0; y < height; y++) {
		for (auto x = 0; x < width; x++) {
			if (coverage[static_cast<size_t>(y) * width + x]) {
				tx1 = (std::min)(tx1, x), ty1 = (std::min)(ty1, y);
				tx2 = (std::max)(tx2, x + 1), ty2 = (std::max)(ty2, y + 1);
			}
		}
	}
	if (tx1 >= tx2 || ty1 >= ty2)
		return {.Metrics = {.AdvanceX = advancePx}};

	rendered_glyph res;
	res.Alpha.resize(static_cast<size_t>(tx2 - tx1) * (ty2 - ty1));
	for (auto y = ty1; y < ty2; y++)
		std::copy_n(&coverage[static_cast<size_t>(y) * width + tx1], tx2 - tx1, &res.Alpha[static_cast<size_t>(y - ty1) * (tx2 - tx1)]);
	res.Metrics = {
		.X1 = bounds.X1 + tx1,
		.Y1 = bounds.Y1 + ty1,
		.X2 = bounds.X1 + tx2,
		.Y2 = bounds.Y1 + ty2,
		.AdvanceX = advancePx,
	};
	return res;
}
