#include "../include/FontChanger.FixedSizeFont/freetype_fixed_size_font.h"

using namespace xivres;

#include <cmath>

#include <harfbuzz/hb-ft.h>

#include "xivres/util.bitmap_copy.h"

#include FT_BBOX_H
#include FT_BITMAP_H
#include FT_MULTIPLE_MASTERS_H
#include FT_OUTLINE_H
#include FT_TRUETYPE_TABLES_H

#include <functional>

static FT_Error success_or_throw(FT_Error error, std::initializer_list<FT_Error> acceptables = {}) {
	if (!error)
		return error;

	for (const auto& er : acceptables) {
		if (er == error)
			return error;
	}

	throw std::runtime_error(std::format("FreeType Error: 0x{:x}", error));
}

template<typename T>
static T return_first_arg_on_success(FT_Error (*pfn)(T*)) {
	T p{};
	success_or_throw(pfn(&p));
	return p;
}

class freetype_font_table {
	std::vector<uint8_t> m_buf;

public:
	freetype_font_table(FT_Face face, uint32_t tag);

	operator bool() const;

	template<typename T = uint8_t>
	[[nodiscard]] std::span<const T> get_span() const {
		if (m_buf.empty())
			return {};

		return {reinterpret_cast<const T*>(&m_buf[0]), m_buf.size() / sizeof(T)};
	}
};

class freetype_bitmap_wrapper {
	const FT_Library m_library;
	FT_Bitmap m_bitmap;

public:
	freetype_bitmap_wrapper(FT_Library library);
	freetype_bitmap_wrapper(freetype_bitmap_wrapper&&) = delete;
	freetype_bitmap_wrapper(const freetype_bitmap_wrapper&) = delete;
	freetype_bitmap_wrapper& operator=(freetype_bitmap_wrapper&&) = delete;
	freetype_bitmap_wrapper& operator=(const freetype_bitmap_wrapper&) = delete;

	~freetype_bitmap_wrapper();

	FT_Bitmap& operator*();

	FT_Bitmap* operator->();

	void convert_from(const FT_Bitmap& source, int alignment);

	[[nodiscard]] std::span<uint8_t> get_buffer() const;
};

FontChanger::FixedSizeFont::glyph_metrics FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_glyph_to_metrics(uint32_t glyphIndex, FT_Glyph glyph, int x, int y) const {
	const auto adjustment = m_face.get_glyph_adjustment(glyphIndex);

	FT_BBox cbox;
	FT_Glyph_Get_CBox(glyph, FT_GLYPH_BBOX_PIXELS, &cbox);
	glyph_metrics res;
	res.X1 = x + cbox.xMin + adjustment.PlacementX;
	res.Y1 = y + ascent() - cbox.yMax + adjustment.PlacementY;
	res.X2 = res.X1 + static_cast<int>(cbox.xMax - cbox.xMin);
	res.Y2 = res.Y1 + static_cast<int>(cbox.yMax - cbox.yMin);
	res.AdvanceX = static_cast<int>(std::lround(static_cast<double>(glyph->advance.x) / 0x10000)) + adjustment.AdvanceX;  // 16.16 fixed point
	return res;
}

FontChanger::FixedSizeFont::glyph_metrics FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_glyph_to_shaped_metrics(uint32_t glyphIndex, FT_Glyph glyph, int x, int y) const {
	const auto adjustment = m_face.get_glyph_adjustment(glyphIndex);
	auto res = freetype_glyph_to_metrics(glyphIndex, glyph, x, y - ascent());
	res.translate(-adjustment.PlacementX, -adjustment.PlacementY);
	res.AdvanceX -= adjustment.AdvanceX;
	return res;
}

std::optional<FontChanger::FixedSizeFont::shaped_line> FontChanger::FixedSizeFont::freetype_fixed_size_font::shape_line(std::u32string_view text, int letterSpacing) const {
	return m_face.shape_line(text, letterSpacing);
}

FontChanger::FixedSizeFont::freetype_fixed_size_font::glyph_ptr_t FontChanger::FixedSizeFont::freetype_fixed_size_font::load_positioned_glyph(uint32_t glyphIndex, float originX, float originY, bool render, int& x, int& y) const {
	auto glyph = m_face.load_glyph(glyphIndex, false);
	x = static_cast<int>(std::floor(originX));
	y = static_cast<int>(std::floor(originY));

	// Outlines can be moved by fractions of pixels; bitmaps are at the nearest pixel instead. y grows upwards here.
	if (glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
		FT_Vector delta{
			.x = static_cast<FT_Pos>(std::lround((originX - static_cast<float>(x)) * 64)),
			.y = -static_cast<FT_Pos>(std::lround((originY - static_cast<float>(y)) * 64)),
		};
		FT_Glyph_Transform(glyph.get(), nullptr, &delta);
	} else {
		x = static_cast<int>(std::lround(originX));
		y = static_cast<int>(std::lround(originY));
	}

	if (render) {
		auto raw = glyph.release();
		if (const auto error = FT_Glyph_To_Bitmap(&raw, m_face.render_mode(), nullptr, true)) {
			FT_Done_Glyph(raw);
			success_or_throw(error);
		}
		glyph.reset(raw);
	}
	return glyph;
}

bool FontChanger::FixedSizeFont::freetype_fixed_size_font::try_get_glyph_index_metrics(uint32_t glyphIndex, float originX, float originY, glyph_metrics& gm) const {
	if (!glyphIndex || glyphIndex >= static_cast<uint32_t>(m_face->num_glyphs))
		return false;

	int x, y;
	const auto glyph = load_positioned_glyph(glyphIndex, originX, originY, false, x, y);
	gm = freetype_glyph_to_shaped_metrics(glyphIndex, glyph.get(), x, y);
	return true;
}

bool FontChanger::FixedSizeFont::freetype_fixed_size_font::try_get_glyph_index_ink_extent(uint32_t glyphIndex, float& x1, float& x2) const {
	if (!glyphIndex || glyphIndex >= static_cast<uint32_t>(m_face->num_glyphs))
		return false;

	const auto glyph = m_face.load_glyph(glyphIndex, false);
	FT_BBox box;
	if (glyph->format == FT_GLYPH_FORMAT_OUTLINE)
		FT_Outline_Get_BBox(&reinterpret_cast<FT_OutlineGlyph>(glyph.get())->outline, &box);
	else
		FT_Glyph_Get_CBox(glyph.get(), FT_GLYPH_BBOX_SUBPIXELS, &box);
	x1 = static_cast<float>(box.xMin) / 64;
	x2 = static_cast<float>(box.xMax) / 64;
	return true;
}

bool FontChanger::FixedSizeFont::freetype_fixed_size_font::draw_glyph_index(uint32_t glyphIndex, uint8_t* pBuf, size_t stride, float drawX, float drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const {
	if (!glyphIndex || glyphIndex >= static_cast<uint32_t>(m_face->num_glyphs))
		return false;

	int x, y;
	auto glyph = load_positioned_glyph(glyphIndex, drawX, drawY, true, x, y);
	auto bitmapGlyph = reinterpret_cast<FT_BitmapGlyph>(glyph.get());
	auto dest = freetype_glyph_to_shaped_metrics(glyphIndex, glyph.get(), x, y);
	auto src = dest;
	src.translate(-src.X1, -src.Y1);
	src.adjust_to_intersection(dest, src.width(), src.height(), destWidth, destHeight);
	if (src.is_effectively_empty() || dest.is_effectively_empty())
		return true;

	freetype_bitmap_wrapper bitmapWrapper(m_face.library());
	bitmapWrapper.convert_from(bitmapGlyph->bitmap, 1);

	util::bitmap_copy::to_l8()
		.from(bitmapWrapper->buffer, bitmapWrapper->pitch, bitmapWrapper->rows, 1, util::bitmap_vertical_direction::TopRowFirst)
		.to(pBuf, destWidth, destHeight, stride, util::bitmap_vertical_direction::TopRowFirst)
		.fore_color(fgColor)
		.fore_opacity(fgOpacity)
		.back_color(bgColor)
		.back_opacity(bgOpacity)
		.gamma_table(m_face.gamma_table())
		.copy(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);
	return true;
}

bool FontChanger::FixedSizeFont::freetype_fixed_size_font::try_get_glyph_outline(char32_t codepoint, glyph_outline& outline) const {
	const auto glyphIndex = m_face.get_char_index(codepoint);
	if (!glyphIndex)
		return false;

	const auto adjustment = m_face.get_glyph_adjustment(glyphIndex);
	return try_get_glyph_index_outline(glyphIndex, static_cast<float>(adjustment.PlacementX), static_cast<float>(ascent() + adjustment.PlacementY), outline);
}

bool FontChanger::FixedSizeFont::freetype_fixed_size_font::try_get_glyph_index_outline(uint32_t glyphIndex, float originX, float originY, glyph_outline& outline) const {
	if (!glyphIndex || glyphIndex >= static_cast<uint32_t>(m_face->num_glyphs))
		return false;

	const auto glyph = m_face.load_unhinted_glyph(glyphIndex);
	if (glyph->format != FT_GLYPH_FORMAT_OUTLINE)
		return false;

	// The outline is in 26.6 fixed point pixels from the origin, with y growing upwards.
	struct context {
		glyph_outline& Outline;
		float OffsetX;
		float BaselineY;

		[[nodiscard]] glyph_outline::point convert(const FT_Vector* v) const {
			return {OffsetX + static_cast<float>(v->x) / 64.f, BaselineY - static_cast<float>(v->y) / 64.f};
		}
	};

	outline = {};
	context ctx{outline, originX, originY};
	static constexpr FT_Outline_Funcs Funcs{
		.move_to = [](const FT_Vector* to, void* user) {
			const auto& c = *static_cast<context*>(user);
			c.Outline.move_to(c.convert(to));
			return 0;
		},
		.line_to = [](const FT_Vector* to, void* user) {
			const auto& c = *static_cast<context*>(user);
			c.Outline.line_to(c.convert(to));
			return 0;
		},
		.conic_to = [](const FT_Vector* control, const FT_Vector* to, void* user) {
			const auto& c = *static_cast<context*>(user);
			c.Outline.quad_to(c.convert(control), c.convert(to));
			return 0;
		},
		.cubic_to = [](const FT_Vector* control1, const FT_Vector* control2, const FT_Vector* to, void* user) {
			const auto& c = *static_cast<context*>(user);
			c.Outline.cubic_to(c.convert(control1), c.convert(control2), c.convert(to));
			return 0;
		},
		.shift = 0,
		.delta = 0,
	};
	auto& ftOutline = reinterpret_cast<FT_OutlineGlyph>(glyph.get())->outline;
	success_or_throw(FT_Outline_Decompose(&ftOutline, &Funcs, &ctx));
	outline.close();
	outline.EvenOdd = (ftOutline.flags & FT_OUTLINE_EVEN_ODD_FILL) != 0;
	return true;
}

const FontChanger::FixedSizeFont::fixed_size_font* FontChanger::FixedSizeFont::freetype_fixed_size_font::get_base_font(char32_t codepoint) const {
	return this;
}

std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font> FontChanger::FixedSizeFont::freetype_fixed_size_font::get_threadsafe_view() const {
	return std::make_shared<freetype_fixed_size_font>(*this);
}

bool FontChanger::FixedSizeFont::freetype_fixed_size_font::draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const {
	const auto glyphIndex = m_face.get_char_index(codepoint);
	if (!glyphIndex)
		return false;

	auto glyph = m_face.load_glyph(glyphIndex, true);
	auto bitmapGlyph = reinterpret_cast<FT_BitmapGlyph>(glyph.get());
	auto dest = freetype_glyph_to_metrics(glyphIndex, glyph.get(), drawX, drawY);
	auto src = dest;
	src.translate(-src.X1, -src.Y1);
	src.adjust_to_intersection(dest, src.width(), src.height(), destWidth, destHeight);
	if (src.is_effectively_empty() || dest.is_effectively_empty())
		return true;

	freetype_bitmap_wrapper bitmapWrapper(m_face.library());
	bitmapWrapper.convert_from(bitmapGlyph->bitmap, 1);

	util::bitmap_copy::to_l8()
		.from(bitmapWrapper->buffer, bitmapWrapper->pitch, bitmapWrapper->rows, 1, util::bitmap_vertical_direction::TopRowFirst)
		.to(pBuf, destWidth, destHeight, stride, util::bitmap_vertical_direction::TopRowFirst)
		.fore_color(fgColor)
		.fore_opacity(fgOpacity)
		.back_color(bgColor)
		.back_opacity(bgOpacity)
		.gamma_table(m_face.gamma_table())
		.copy(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);
	return true;
}

bool FontChanger::FixedSizeFont::freetype_fixed_size_font::draw(char32_t codepoint, util::b8g8r8a8* pBuf, int drawX, int drawY, int destWidth, int destHeight, util::b8g8r8a8 fgColor, util::b8g8r8a8 bgColor) const {
	const auto glyphIndex = m_face.get_char_index(codepoint);
	if (!glyphIndex)
		return false;

	auto glyph = m_face.load_glyph(glyphIndex, true);
	auto bitmapGlyph = reinterpret_cast<FT_BitmapGlyph>(glyph.get());
	auto dest = freetype_glyph_to_metrics(glyphIndex, glyph.get(), drawX, drawY);
	auto src = dest;
	src.translate(-src.X1, -src.Y1);
	src.adjust_to_intersection(dest, src.width(), src.height(), destWidth, destHeight);
	if (src.is_effectively_empty() || dest.is_effectively_empty())
		return true;

	freetype_bitmap_wrapper bitmapWrapper(m_face.library());
	bitmapWrapper.convert_from(bitmapGlyph->bitmap, 1);

	util::bitmap_copy::to_b8g8r8a8()
		.from(bitmapWrapper->buffer, bitmapWrapper->pitch, bitmapWrapper->rows, 1, util::bitmap_vertical_direction::TopRowFirst)
		.to(pBuf, destWidth, destHeight, util::bitmap_vertical_direction::TopRowFirst)
		.fore_color(fgColor)
		.back_color(bgColor)
		.gamma_table(m_face.gamma_table())
		.copy(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);
	return true;
}

int FontChanger::FixedSizeFont::freetype_fixed_size_font::get_adjusted_advance_width(char32_t left, char32_t right) const {
	glyph_metrics gm;
	if (!try_get_glyph_metrics(left, gm))
		return 0;

	if (const auto it = m_face.all_kerning_pairs().find(std::make_pair(left, right)); it != m_face.all_kerning_pairs().end())
		return gm.AdvanceX + it->second;

	return gm.AdvanceX;
}

const std::map<std::pair<char32_t, char32_t>, int>& FontChanger::FixedSizeFont::freetype_fixed_size_font::all_kerning_pairs() const {
	return m_face.all_kerning_pairs();
}

bool FontChanger::FixedSizeFont::freetype_fixed_size_font::try_get_glyph_metrics(char32_t codepoint, glyph_metrics& gm) const {
	const auto glyphIndex = m_face.get_char_index(codepoint);
	if (!glyphIndex)
		return false;

	gm = freetype_glyph_to_metrics(glyphIndex, m_face.load_glyph(glyphIndex, false).get());
	return true;
}

const std::set<char32_t>& FontChanger::FixedSizeFont::freetype_fixed_size_font::all_codepoints() const {
	return m_face.all_characters();
}

// Vertical metrics are subject to the vertical scale of the transformation, as the glyphs are.
int FontChanger::FixedSizeFont::freetype_fixed_size_font::line_height() const {
	return static_cast<int>(std::ceil(static_cast<double>(m_face->size->metrics.height) / 64 * std::abs(m_face.matrix().yy / 65536.)));
}

int FontChanger::FixedSizeFont::freetype_fixed_size_font::ascent() const {
	return static_cast<int>(std::ceil(static_cast<double>(m_face->size->metrics.ascender) / 64 * std::abs(m_face.matrix().yy / 65536.)));
}

float FontChanger::FixedSizeFont::freetype_fixed_size_font::font_size() const {
	return m_face.font_size();
}

std::string FontChanger::FixedSizeFont::freetype_fixed_size_font::subfamily_name() const {
	freetype_font_table nameDataRef(*m_face, truetype::Name::DirectoryTableTag.ReverseNativeValue);
	truetype::Name::View name(nameDataRef.get_span<char>());
	if (!name)
		return {};

	return name.GetPreferredSubfamilyName<std::string>(0);
}

std::string FontChanger::FixedSizeFont::freetype_fixed_size_font::family_name() const {
	freetype_font_table nameDataRef(*m_face, truetype::Name::DirectoryTableTag.ReverseNativeValue);
	truetype::Name::View name(nameDataRef.get_span<char>());
	if (!name)
		return {};

	return name.GetPreferredFamilyName<std::string>(0);
}

FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_fixed_size_font() = default;

FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_fixed_size_font(std::vector<uint8_t> data, int faceIndex, float fSize, float gamma, const font_render_transformation_matrix& matrix, create_struct createStruct)
	: m_face(std::move(data), faceIndex, fSize, gamma, matrix, createStruct) {}

FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_fixed_size_font(stream& strm, int faceIndex, float fSize, float gamma, const font_render_transformation_matrix& matrix, create_struct createStruct)
	: m_face(strm.read_vector<uint8_t>(), faceIndex, fSize, gamma, matrix, createStruct) {}

FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_fixed_size_font(const std::filesystem::path& path, int faceIndex, float size, float gamma, const font_render_transformation_matrix& matrix, create_struct createStruct)
	: freetype_fixed_size_font(file_stream(path).read_vector<uint8_t>(), faceIndex, size, gamma, matrix, createStruct) {}

FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_fixed_size_font(freetype_fixed_size_font&& r) noexcept = default;

FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_fixed_size_font(const freetype_fixed_size_font& r) = default;

FontChanger::FixedSizeFont::freetype_fixed_size_font& FontChanger::FixedSizeFont::freetype_fixed_size_font::operator=(freetype_fixed_size_font&& r) noexcept = default;

FontChanger::FixedSizeFont::freetype_fixed_size_font& FontChanger::FixedSizeFont::freetype_fixed_size_font::operator=(const freetype_fixed_size_font& r) = default;

std::span<uint8_t> freetype_bitmap_wrapper::get_buffer() const {
	return {m_bitmap.buffer, static_cast<size_t>(1) * m_bitmap.rows * m_bitmap.pitch};
}

void freetype_bitmap_wrapper::convert_from(const FT_Bitmap& source, int alignment) {
	success_or_throw(FT_Bitmap_Convert(m_library, &source, &m_bitmap, alignment));
	switch (m_bitmap.num_grays) {
		case 2:
			for (auto& b : get_buffer())
				b = b ? 255 : 0;
			break;

		case 4:
			for (auto& b : get_buffer())
				b = b * 85;
			break;

		case 16:
			for (auto& b : get_buffer())
				b = b * 17;
			break;

		case 256:
			break;

		default:
			throw std::runtime_error("Invalid num_grays");
	}
}

FT_Bitmap* freetype_bitmap_wrapper::operator->() {
	return &m_bitmap;
}

FT_Bitmap& freetype_bitmap_wrapper::operator*() {
	return m_bitmap;
}

freetype_bitmap_wrapper::~freetype_bitmap_wrapper() {
	success_or_throw(FT_Bitmap_Done(m_library, &m_bitmap));
}

freetype_bitmap_wrapper::freetype_bitmap_wrapper(FT_Library library)
	: m_library(library) {
	FT_Bitmap_Init(&m_bitmap);
}

std::unique_ptr<std::remove_pointer_t<FT_Glyph>, decltype(&FT_Done_Glyph)> FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_face_wrapper::load_glyph(uint32_t glyphIndex, bool render) const {
	if (m_face->glyph->glyph_index != glyphIndex)
		success_or_throw(FT_Load_Glyph(m_face, glyphIndex, m_info->LoadFlags));

	FT_Glyph glyph;
	success_or_throw(FT_Get_Glyph(m_face->glyph, &glyph));
	auto uniqueGlyphPtr = std::unique_ptr<std::remove_pointer_t<FT_Glyph>, decltype(&FT_Done_Glyph)>(glyph, FT_Done_Glyph);

	// The copy is emboldened, not the slot, which is reused for the next load of the same glyph.
	if (const auto strength = m_info->EmboldenStrength; strength && glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
		success_or_throw(FT_Outline_EmboldenXY(&reinterpret_cast<FT_OutlineGlyph>(glyph)->outline, strength, strength));
		glyph->advance.x += strength * 1024;  // 26.6 to 16.16 fixed point
	}

	FT_Vector zeroDelta{};
	FT_Glyph_Transform(glyph, &m_info->Matrix, &zeroDelta); // failing this is acceptable

	if (render) {
		success_or_throw(FT_Glyph_To_Bitmap(&glyph, m_info->Params.RenderMode, nullptr, false));
		void(uniqueGlyphPtr.release());
		uniqueGlyphPtr = {glyph, FT_Done_Glyph};
	}

	return std::move(uniqueGlyphPtr);
}

std::unique_ptr<std::remove_pointer_t<FT_Glyph>, decltype(&FT_Done_Glyph)> FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_face_wrapper::load_unhinted_glyph(uint32_t glyphIndex) const {
	const auto flags = (m_info->LoadFlags & ~(FT_LOAD_FORCE_AUTOHINT | FT_LOAD_TARGET_(0xF))) | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP;
	const auto loadError = FT_Load_Glyph(m_face, glyphIndex, flags);

	FT_Glyph glyph = nullptr;
	const auto getError = loadError ? loadError : FT_Get_Glyph(m_face->glyph, &glyph);

	// load_glyph reuses the glyph in the slot by its index, which must not be this one loaded with other flags.
	if (FT_Load_Glyph(m_face, glyphIndex, m_info->LoadFlags))
		m_face->glyph->glyph_index = 0;
	success_or_throw(getError);

	auto uniqueGlyphPtr = std::unique_ptr<std::remove_pointer_t<FT_Glyph>, decltype(&FT_Done_Glyph)>(glyph, FT_Done_Glyph);
	FT_Vector zeroDelta{};
	FT_Glyph_Transform(glyph, &m_info->Matrix, &zeroDelta);
	return uniqueGlyphPtr;
}

FT_Face FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_face_wrapper::create_face(FT_Library library, const info& info) {
	FT_Face face;
	success_or_throw(FT_New_Memory_Face(library, &info.Data[0], static_cast<FT_Long>(info.Data.size()), info.FaceIndex, &face));
	try {
		// Variable fonts: start from the instance selected by the face index, and apply the requested axis values.
		// Unless requested otherwise, the optical size follows the font size.
		if (FT_HAS_MULTIPLE_MASTERS(face)) {
			FT_MM_Var* mm = nullptr;
			success_or_throw(FT_Get_MM_Var(face, &mm));
			const auto mmFree = std::unique_ptr<FT_MM_Var, std::function<void(FT_MM_Var*)>>(mm, [library](FT_MM_Var* p) { FT_Done_MM_Var(library, p); });

			std::vector<FT_Fixed> coordinates(mm->num_axis);
			success_or_throw(FT_Get_Var_Design_Coordinates(face, mm->num_axis, coordinates.data()));
			for (FT_UInt i = 0; i < mm->num_axis; i++) {
				const auto& axis = mm->axis[i];
				const auto tag = _byteswap_ulong(static_cast<uint32_t>(axis.tag));
				if (const auto it = info.Params.Variations.find(tag); it != info.Params.Variations.end())
					coordinates[i] = static_cast<FT_Fixed>(std::lround(it->second * 65536.));
				else if (tag == truetype::Fvar::OpticalSizeAxisTag.NativeValue)
					coordinates[i] = static_cast<FT_Fixed>(std::lround(info.Size * 65536.));
				coordinates[i] = std::clamp(coordinates[i], axis.minimum, axis.maximum);
			}
			success_or_throw(FT_Set_Var_Design_Coordinates(face, mm->num_axis, coordinates.data()));
		}

		success_or_throw(FT_Set_Char_Size(face, 0, static_cast<FT_F26Dot6>(64.f * info.Size), 72, 72));
		return face;
	} catch (...) {
		success_or_throw(FT_Done_Face(face));
		throw;
	}
}

std::optional<float> FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_face_wrapper::get_baseline(uint32_t baselineTag) const {
	const auto it = m_info->Baselines.find(baselineTag);
	return it == m_info->Baselines.end() ? std::nullopt : std::optional(it->second);
}

FontChanger::FixedSizeFont::shaped_line FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_face_wrapper::shape_line(std::u32string_view text, int letterSpacing) const {
	const auto blob = std::unique_ptr<hb_blob_t, decltype(&hb_blob_destroy)>(
		hb_blob_create(reinterpret_cast<const char*>(m_info->Data.data()), static_cast<unsigned>(m_info->Data.size()), HB_MEMORY_MODE_READONLY, nullptr, nullptr),
		&hb_blob_destroy);
	const auto face = std::unique_ptr<hb_face_t, decltype(&hb_face_destroy)>(hb_face_create(blob.get(), static_cast<unsigned>(m_info->FaceIndex) & 0xFFFF), &hb_face_destroy);
	const auto font = std::unique_ptr<hb_font_t, decltype(&hb_font_destroy)>(hb_font_create(face.get()), &hb_font_destroy);

	// Positions are in 26.6 fixed point pixels, at the instance of the face.
	const auto scale = static_cast<int>(std::lround(m_info->Size * 64));
	hb_font_set_scale(font.get(), scale, scale);
	if (const auto coordinates = get_design_coordinates(m_library.get(), m_face); !coordinates.empty()) {
		std::vector<hb_variation_t> variations;
		for (const auto& [tag, value] : coordinates)
			variations.push_back({.tag = _byteswap_ulong(tag), .value = value});
		hb_font_set_variations(font.get(), variations.data(), static_cast<unsigned>(variations.size()));
	}

	const auto buffer = std::unique_ptr<hb_buffer_t, decltype(&hb_buffer_destroy)>(hb_buffer_create(), &hb_buffer_destroy);
	hb_buffer_add_utf32(buffer.get(), reinterpret_cast<const uint32_t*>(text.data()), static_cast<int>(text.size()), 0, static_cast<int>(text.size()));
	if (!m_info->Params.LanguageCode.empty())
		hb_buffer_set_language(buffer.get(), hb_language_from_string(m_info->Params.LanguageCode.c_str(), static_cast<int>(m_info->Params.LanguageCode.size())));
	hb_buffer_guess_segment_properties(buffer.get());
	hb_shape(font.get(), buffer.get(), m_info->Params.Features.data(), static_cast<unsigned>(m_info->Params.Features.size()));

	unsigned count = 0;
	const auto* infos = hb_buffer_get_glyph_infos(buffer.get(), &count);
	const auto* positions = hb_buffer_get_glyph_positions(buffer.get(), &count);

	// The glyphs are transformed by the matrix as they are drawn, and so are their positions; y grows upwards here.
	const auto& m = m_info->Matrix;
	const auto transformX = [&m](double x, double y) { return (m.xx * x + m.xy * y) / 0x10000 / 64; };
	const auto transformY = [&m](double x, double y) { return (m.yx * x + m.yy * y) / 0x10000 / 64; };

	shaped_line res;
	double penX = 0, penY = 0;
	for (unsigned i = 0; i < count; i++) {
		const auto x = penX + positions[i].x_offset, y = penY + positions[i].y_offset;
		res.Glyphs.push_back({
			.GlyphIndex = infos[i].codepoint,
			.X = static_cast<float>(transformX(x, y)) + static_cast<float>(static_cast<int>(i) * letterSpacing),
			.Y = -static_cast<float>(transformY(x, y)),
		});
		// Emboldened glyphs advance further, as they do when drawn one by one.
		penX += positions[i].x_advance + m_info->EmboldenStrength;
		penY += positions[i].y_advance;
	}
	res.AdvanceWidth = static_cast<int>(std::lround(transformX(penX, penY))) + (count ? static_cast<int>(count - 1) * letterSpacing : 0);
	return res;
}

std::optional<float> FontChanger::FixedSizeFont::freetype_fixed_size_font::get_baseline(uint32_t baselineTag) const {
	return m_face.get_baseline(baselineTag);
}

FontChanger::FixedSizeFont::glyph_adjustment FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_face_wrapper::get_glyph_adjustment(uint32_t glyphIndex) const {
	if (glyphIndex > 0xFFFF)
		return {};
	const auto it = m_info->GlyphAdjustments.find(static_cast<uint16_t>(glyphIndex));
	return it == m_info->GlyphAdjustments.end() ? glyph_adjustment{} : it->second;
}

const std::map<std::pair<char32_t, char32_t>, int>& FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_face_wrapper::all_kerning_pairs() const {
	return m_info->KerningPairs;
}

const std::set<char32_t>& FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_face_wrapper::all_characters() const {
	return m_info->Characters;
}

std::span<const uint8_t> FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_face_wrapper::gamma_table() const {
	return {m_info->GammaTable};
}

float FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_face_wrapper::font_size() const {
	return m_info->Size;
}

const FT_Matrix& FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_face_wrapper::matrix() const {
	return m_info->Matrix;
}

FT_Render_Mode FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_face_wrapper::render_mode() const {
	return m_info->Params.RenderMode;
}

FT_Library FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_face_wrapper::library() const {
	return m_library.get();
}

int FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_face_wrapper::get_char_index(char32_t codepoint) const {
	return resolve_glyph_index(m_face, m_hbFont, codepoint, m_info->Params);
}

int FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_face_wrapper::resolve_glyph_index(FT_Face face, hb_font_t* hbFont, char32_t codepoint, const create_struct& params) {
	if (!hbFont)
		return static_cast<int>(FT_Get_Char_Index(face, codepoint));

	const auto buf = hb_buffer_create();
	const auto bufFree = std::unique_ptr<hb_buffer_t, decltype(&hb_buffer_destroy)>(buf, hb_buffer_destroy);
	hb_buffer_add_codepoints(buf, reinterpret_cast<const hb_codepoint_t*>(&codepoint), 1, 0, 1);
	if (!params.LanguageCode.empty())
		hb_buffer_set_language(buf, hb_language_from_string(params.LanguageCode.c_str(), static_cast<int>(params.LanguageCode.size())));
	hb_buffer_guess_segment_properties(buf);
	hb_shape(hbFont, buf, params.Features.data(), static_cast<unsigned>(params.Features.size()));
	unsigned count = 0;
	const auto* infos = hb_buffer_get_glyph_infos(buf, &count);
	return (count > 0) ? static_cast<int>(infos[0].codepoint) : 0;
}

FT_Face FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_face_wrapper::operator->() const {
	return m_face;
}

FT_Face FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_face_wrapper::operator*() const {
	return m_face;
}

FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_face_wrapper::~freetype_face_wrapper() {
	*this = nullptr;
}

FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_face_wrapper& FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_face_wrapper::operator=(const std::nullptr_t&) {
	if (!m_face)
		return *this;

	if (m_hbFont) {
		hb_font_destroy(m_hbFont);
		m_hbFont = nullptr;
	}

	success_or_throw(FT_Done_Face(m_face));

	m_face = nullptr;
	m_info = nullptr;
	m_library = nullptr;

	return *this;
}

FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_face_wrapper& FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_face_wrapper::operator=(const freetype_face_wrapper& r) {
	if (this == &r)
		return *this;

	auto library = library_ptr_t(return_first_arg_on_success<FT_Library>(FT_Init_FreeType), &FT_Done_FreeType);
	auto face = create_face(library.get(), *r.m_info);

	hb_font_t* newHbFont = nullptr;
	if (r.m_info->Params.requires_shaping())
		newHbFont = hb_ft_font_create(face, nullptr);

	*this = nullptr;

	m_library = std::move(library);
	m_info = r.m_info;
	m_face = face;
	m_hbFont = newHbFont;

	return *this;
}

FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_face_wrapper& FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_face_wrapper::operator=(freetype_face_wrapper&& r) noexcept {
	if (this == &r)
		return *this;

	*this = nullptr;

	m_library = std::move(r.m_library);
	m_info = std::move(r.m_info);
	m_face = r.m_face;
	r.m_face = nullptr;
	m_hbFont = r.m_hbFont;
	r.m_hbFont = nullptr;

	return *this;
}

FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_face_wrapper::freetype_face_wrapper(const freetype_face_wrapper& r)
	: m_library(return_first_arg_on_success<FT_Library>(FT_Init_FreeType), &FT_Done_FreeType)
	, m_info(r.m_info) {
	if (r.m_face) {
		m_face = create_face(m_library.get(), *m_info);
		if (m_info->Params.requires_shaping())
			m_hbFont = hb_ft_font_create(m_face, nullptr);
	}
}

FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_face_wrapper::freetype_face_wrapper(freetype_face_wrapper&& r) noexcept
	: m_library(std::move(r.m_library))
	, m_info(std::move(r.m_info))
	, m_face(r.m_face)
	, m_hbFont(r.m_hbFont) {
	r.m_face = nullptr;
	r.m_hbFont = nullptr;
}

FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_face_wrapper::freetype_face_wrapper(std::vector<uint8_t> data, int faceIndex, float size, float gamma, const font_render_transformation_matrix& matrix, create_struct createStruct)
	: m_library(return_first_arg_on_success<FT_Library>(FT_Init_FreeType), &FT_Done_FreeType) {
	auto info = std::make_shared<struct info>();
	info->Data = std::move(data);
	info->Size = size;
	info->GammaTable = util::bitmap_copy::create_gamma_table(gamma);
	info->Params = createStruct;
	info->FaceIndex = faceIndex;
	info->Matrix.xx = static_cast<FT_Fixed>(matrix.M11 * static_cast<float>(0x10000));
	info->Matrix.xy = static_cast<FT_Fixed>(matrix.M12 * static_cast<float>(0x10000));
	info->Matrix.yx = static_cast<FT_Fixed>(matrix.M21 * static_cast<float>(0x10000));
	info->Matrix.yy = static_cast<FT_Fixed>(matrix.M22 * static_cast<float>(0x10000));
	info->Params.LoadFlags &= (0 |
		FT_LOAD_NO_HINTING |
		FT_LOAD_NO_BITMAP |
		FT_LOAD_FORCE_AUTOHINT |
		FT_LOAD_NO_AUTOHINT);
	info->EmboldenStrength = static_cast<FT_Pos>(std::lround(createStruct.Embolden * size * 64));
	if (info->EmboldenStrength)
		info->Params.LoadFlags |= FT_LOAD_NO_BITMAP;

	m_face = create_face(m_library.get(), *info);
	info->LoadFlags = get_load_flags(m_face, info->Params);

	FT_UInt glyphIndex;
	for (char32_t c = FT_Get_First_Char(m_face, &glyphIndex); glyphIndex; c = FT_Get_Next_Char(m_face, c, &glyphIndex))
		info->Characters.insert(c);

	if (info->Params.requires_shaping())
		m_hbFont = hb_ft_font_create(m_face, nullptr);

	freetype_font_table kernDataRef(m_face, truetype::Kern::DirectoryTableTag.ReverseNativeValue);
	freetype_font_table gposDataRef(m_face, truetype::Gpos::DirectoryTableTag.ReverseNativeValue);
	if (kernDataRef || gposDataRef) {
		// Positioning is keyed by the glyphs that are actually drawn, which may be substituted by the selected features.
		std::vector<std::set<char32_t>> glyphToCharMap((std::min<size_t>)(m_face->num_glyphs, 65536));
		for (const auto c : info->Characters) {
			if (const auto glyphIndex = resolve_glyph_index(m_face, m_hbFont, c, info->Params); glyphIndex > 0 && glyphIndex < glyphToCharMap.size())
				glyphToCharMap[glyphIndex].insert(c);
		}

		freetype_font_table gdefDataRef(m_face, truetype::Gdef::DirectoryTableTag.ReverseNativeValue);
		freetype_font_table fvarDataRef(m_face, truetype::Fvar::DirectoryTableTag.ReverseNativeValue);
		freetype_font_table avarDataRef(m_face, truetype::Avar::DirectoryTableTag.ReverseNativeValue);
		opentype_positioning_params params{
			.Gpos = gposDataRef.get_span<char>(),
			.Kern = kernDataRef.get_span<char>(),
			.Gdef = gdefDataRef.get_span<char>(),
			.Fvar = fvarDataRef.get_span<char>(),
			.Avar = avarDataRef.get_span<char>(),
			.DesignCoordinates = get_design_coordinates(m_library.get(), m_face),
			.LanguageCode = info->Params.LanguageCode,
			.Size = info->Size,
			.UnitsPerEm = m_face->units_per_EM,
			.ScaleX = matrix.M11,
			.ScaleY = matrix.M22,
		};
		for (const auto& feature : info->Params.Features) {
			if (feature.value)
				params.FeatureTags.insert(_byteswap_ulong(feature.tag));
			else
				params.DisabledFeatureTags.insert(_byteswap_ulong(feature.tag));
		}

		const auto hbBlob = std::unique_ptr<hb_blob_t, decltype(&hb_blob_destroy)>(
			hb_blob_create(reinterpret_cast<const char*>(info->Data.data()), static_cast<unsigned>(info->Data.size()), HB_MEMORY_MODE_READONLY, nullptr, nullptr),
			&hb_blob_destroy);
		const auto hbFace = std::unique_ptr<hb_face_t, decltype(&hb_face_destroy)>(hb_face_create(hbBlob.get(), static_cast<unsigned>(info->FaceIndex) & 0xFFFF), &hb_face_destroy);
		params.HarfBuzzFace = hbFace.get();

		auto positioning = extract_opentype_positioning(params, glyphToCharMap);
		info->KerningPairs = std::move(positioning.KerningPairs);
		info->GlyphAdjustments = std::move(positioning.GlyphAdjustments);
	}

	// Baselines are subject to the vertical scale of the transformation, as the glyphs are.
	if (freetype_font_table baseDataRef(m_face, truetype::Base::DirectoryTableTag.ReverseNativeValue); baseDataRef) {
		const auto scale = static_cast<double>(info->Size) * matrix.M22 / m_face->units_per_EM;
		for (const auto& [tag, value] : read_baselines(baseDataRef.get_span<char>()))
			info->Baselines.emplace(tag, static_cast<float>(value * scale));
	}

	m_info = std::move(info);
}

int FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_face_wrapper::get_load_flags(FT_Face face, const create_struct& params) {
	auto flags = params.LoadFlags;
	if (flags & FT_LOAD_NO_HINTING)
		return flags;

	switch (params.RenderMode) {
		case FT_RENDER_MODE_MONO:
			return flags | FT_LOAD_TARGET_MONO;

		case FT_RENDER_MODE_LIGHT: {
			if (flags & FT_LOAD_NO_AUTOHINT)
				return flags;
			const auto hasTable = [face](FT_ULong tag) {
				FT_ULong length = 0;
				return FT_Load_Sfnt_Table(face, tag, 0, nullptr, &length) == FT_Err_Ok && length != 0;
			};
			const auto hasTrueTypeInstructions = hasTable(FT_MAKE_TAG('g', 'l', 'y', 'f'))
				&& (hasTable(FT_MAKE_TAG('f', 'p', 'g', 'm')) || hasTable(FT_MAKE_TAG('p', 'r', 'e', 'p')));
			if ((flags & FT_LOAD_FORCE_AUTOHINT) || !hasTrueTypeInstructions)
				flags |= FT_LOAD_TARGET_LIGHT;
			return flags;
		}

		default:
			return flags;
	}
}

FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_face_wrapper::freetype_face_wrapper()
	: m_library(return_first_arg_on_success<FT_Library>(FT_Init_FreeType), &FT_Done_FreeType) {}

std::wstring FontChanger::FixedSizeFont::freetype_fixed_size_font::create_struct::get_render_mode_string() const {
	switch (RenderMode) {
		case FT_RENDER_MODE_NORMAL: return L"Normal";
		case FT_RENDER_MODE_LIGHT: return L"Light";
		case FT_RENDER_MODE_MONO: return L"Mono";
		case FT_RENDER_MODE_LCD: return L"LCD";
		case FT_RENDER_MODE_LCD_V: return L"LCD_V";
		case FT_RENDER_MODE_SDF: return L"SDF";
		default: return L"Invalid";
	}
}

std::wstring FontChanger::FixedSizeFont::freetype_fixed_size_font::create_struct::get_load_flags_string() const {
	std::wstring res;
	if (LoadFlags & FT_LOAD_NO_HINTING) res += L", no hinting";
	if (LoadFlags & FT_LOAD_NO_BITMAP) res += L", no bitmap";
	if (LoadFlags & FT_LOAD_FORCE_AUTOHINT) res += L", force autohint";
	if (LoadFlags & FT_LOAD_NO_AUTOHINT) res += L", no autohint";

	if (res.empty())
		return L"Default";
	return res.substr(2);
}

freetype_font_table::operator bool() const {
	return !m_buf.empty();
}

freetype_font_table::freetype_font_table(FT_Face face, uint32_t tag) {
	FT_ULong len = 0;
	if (success_or_throw(FT_Load_Sfnt_Table(face, tag, 0, nullptr, &len), {FT_Err_Table_Missing}))
		return;

	m_buf.resize(len);
	if (success_or_throw(FT_Load_Sfnt_Table(face, tag, 0, &m_buf[0], &len), {FT_Err_Table_Missing}))
		return;
}

bool FontChanger::FixedSizeFont::freetype_fixed_size_font::create_struct::requires_shaping() const {
	return !Features.empty() || !LanguageCode.empty();
}
std::map<uint32_t, float> FontChanger::FixedSizeFont::freetype_fixed_size_font::freetype_face_wrapper::get_design_coordinates(FT_Library library, FT_Face face) {
	std::map<uint32_t, float> result;
	if (!FT_HAS_MULTIPLE_MASTERS(face))
		return result;

	FT_MM_Var* mm = nullptr;
	if (FT_Get_MM_Var(face, &mm))
		return result;
	const auto mmFree = std::unique_ptr<FT_MM_Var, std::function<void(FT_MM_Var*)>>(mm, [library](FT_MM_Var* p) { FT_Done_MM_Var(library, p); });

	std::vector<FT_Fixed> coordinates(mm->num_axis);
	if (FT_Get_Var_Design_Coordinates(face, mm->num_axis, coordinates.data()))
		return result;

	for (FT_UInt i = 0; i < mm->num_axis; i++)
		result.emplace(_byteswap_ulong(static_cast<uint32_t>(mm->axis[i].tag)), static_cast<float>(coordinates[i]) / 65536.f);
	return result;
}
