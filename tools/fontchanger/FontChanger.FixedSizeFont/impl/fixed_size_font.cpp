#include "../include/FontChanger.FixedSizeFont/fixed_size_font.h"

using namespace xivres;

void FontChanger::FixedSizeFont::glyph_outline::move_to(point p) {
	if (!Verbs.empty() && Verbs.back() != verb::Close)
		close();
	Verbs.push_back(verb::MoveTo);
	Points.push_back(p);
}

void FontChanger::FixedSizeFont::glyph_outline::line_to(point p) {
	Verbs.push_back(verb::LineTo);
	Points.push_back(p);
}

void FontChanger::FixedSizeFont::glyph_outline::quad_to(point c, point p) {
	Verbs.push_back(verb::QuadTo);
	Points.push_back(c);
	Points.push_back(p);
}

void FontChanger::FixedSizeFont::glyph_outline::cubic_to(point c1, point c2, point p) {
	Verbs.push_back(verb::CubicTo);
	Points.push_back(c1);
	Points.push_back(c2);
	Points.push_back(p);
}

void FontChanger::FixedSizeFont::glyph_outline::close() {
	if (!Verbs.empty() && Verbs.back() != verb::Close)
		Verbs.push_back(verb::Close);
}

void FontChanger::FixedSizeFont::glyph_outline::translate(float dx, float dy) {
	for (auto& p : Points) {
		p.X += dx;
		p.Y += dy;
	}
}

void FontChanger::FixedSizeFont::glyph_metrics::adjust_to_intersection(glyph_metrics& r, MetricType srcWidth, MetricType srcHeight, MetricType destWidth, MetricType destHeight) {
	if (X1 < 0) {
		r.X1 -= X1;
		X1 = 0;
	}
	if (r.X1 < 0) {
		X1 -= r.X1;
		r.X1 = 0;
	}
	if (Y1 < 0) {
		r.Y1 -= Y1;
		Y1 = 0;
	}
	if (r.Y1 < 0) {
		Y1 -= r.Y1;
		r.Y1 = 0;
	}
	if (X2 >= srcWidth) {
		r.X2 -= X2 - srcWidth;
		X2 = srcWidth;
	}
	if (r.X2 >= destWidth) {
		X2 -= r.X2 - destWidth;
		r.X2 = destWidth;
	}
	if (Y2 >= srcHeight) {
		r.Y2 -= Y2 - srcHeight;
		Y2 = srcHeight;
	}
	if (r.Y2 >= destHeight) {
		Y2 -= r.Y2 - destHeight;
		r.Y2 = destHeight;
	}

	if (X1 >= X2 || r.X1 >= r.X2 || Y1 >= Y2 || r.Y1 >= r.Y2)
		*this = r = {};
}

FontChanger::FixedSizeFont::glyph_metrics& FontChanger::FixedSizeFont::glyph_metrics::translate(MetricType x, MetricType y) {
	X1 += x;
	X2 += x;
	Y1 += y;
	Y2 += y;
	return *this;
}

FontChanger::FixedSizeFont::glyph_metrics& FontChanger::FixedSizeFont::glyph_metrics::expand_to_fit(const glyph_metrics& r) {
	const auto prevLeft = X1;
	X1 = (std::min)(X1, r.X1);
	Y1 = (std::min)(Y1, r.Y1);
	X2 = (std::max)(X2, r.X2);
	Y2 = (std::max)(Y2, r.Y2);
	if (prevLeft + AdvanceX > r.X1 + r.AdvanceX)
		AdvanceX = prevLeft + AdvanceX - X1;
	else
		AdvanceX = r.X1 + AdvanceX - X1;
	return *this;
}

const void* FontChanger::FixedSizeFont::default_abstract_fixed_size_font::get_base_font_glyph_uniqid(char32_t c) const {
	const auto& codepoints = all_codepoints();
	const auto it = codepoints.find(c);
	return it == codepoints.end() ? nullptr : &*it;
}

char32_t FontChanger::FixedSizeFont::default_abstract_fixed_size_font::uniqid_to_glyph(const void* pc) const {
	const auto c = *reinterpret_cast<const char32_t*>(pc);
	return all_codepoints().contains(c) ? c : 0;
}

int FontChanger::FixedSizeFont::default_abstract_fixed_size_font::get_adjusted_advance_width(char32_t left, char32_t right) const {
	glyph_metrics gm;
	if (!try_get_glyph_metrics(left, gm))
		return 0;

	const auto& kerningPairs = all_kerning_pairs();
	if (const auto it = kerningPairs.find(std::make_pair(left, right)); it != kerningPairs.end())
		return gm.AdvanceX + it->second;

	return gm.AdvanceX;
}

const FontChanger::FixedSizeFont::fixed_size_font* FontChanger::FixedSizeFont::empty_fixed_size_font::get_base_font(char32_t codepoint) const {
	return this;
}

std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font> FontChanger::FixedSizeFont::empty_fixed_size_font::get_threadsafe_view() const {
	return std::make_shared<empty_fixed_size_font>(*this);
}

bool FontChanger::FixedSizeFont::empty_fixed_size_font::draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const {
	return false;
}

bool FontChanger::FixedSizeFont::empty_fixed_size_font::draw(char32_t codepoint, util::b8g8r8a8* pBuf, int drawX, int drawY, int destWidth, int destHeight, util::b8g8r8a8 fgColor, util::b8g8r8a8 bgColor) const {
	return false;
}

int FontChanger::FixedSizeFont::empty_fixed_size_font::get_adjusted_advance_width(char32_t left, char32_t right) const {
	return 0;
}

const std::map<std::pair<char32_t, char32_t>, int>& FontChanger::FixedSizeFont::empty_fixed_size_font::all_kerning_pairs() const {
	static const std::map<std::pair<char32_t, char32_t>, int> s_empty;
	return s_empty;
}

char32_t FontChanger::FixedSizeFont::empty_fixed_size_font::uniqid_to_glyph(const void* pc) const {
	return 0;
}

const void* FontChanger::FixedSizeFont::empty_fixed_size_font::get_base_font_glyph_uniqid(char32_t c) const {
	return nullptr;
}

bool FontChanger::FixedSizeFont::empty_fixed_size_font::try_get_glyph_metrics(char32_t codepoint, glyph_metrics& gm) const {
	gm.clear();
	return false;
}

const std::set<char32_t>& FontChanger::FixedSizeFont::empty_fixed_size_font::all_codepoints() const {
	static const std::set<char32_t> s_empty;
	return s_empty;
}

int FontChanger::FixedSizeFont::empty_fixed_size_font::line_height() const {
	return static_cast<int>(std::lround(m_fontDef.LineHeight));
}

int FontChanger::FixedSizeFont::empty_fixed_size_font::ascent() const {
	return static_cast<int>(std::lround(m_fontDef.Ascent));
}

float FontChanger::FixedSizeFont::empty_fixed_size_font::font_size() const {
	return m_size;
}

std::string FontChanger::FixedSizeFont::empty_fixed_size_font::subfamily_name() const {
	return "Regular";
}

std::string FontChanger::FixedSizeFont::empty_fixed_size_font::family_name() const {
	return "Empty";
}

FontChanger::FixedSizeFont::empty_fixed_size_font::empty_fixed_size_font(float size, create_struct fontDef)
	: m_size(size)
	, m_fontDef(fontDef) {
}

FontChanger::FixedSizeFont::empty_fixed_size_font::empty_fixed_size_font() = default;

FontChanger::FixedSizeFont::empty_fixed_size_font::empty_fixed_size_font(empty_fixed_size_font&&) noexcept = default;

FontChanger::FixedSizeFont::empty_fixed_size_font::empty_fixed_size_font(const empty_fixed_size_font & r) = default;

FontChanger::FixedSizeFont::empty_fixed_size_font& FontChanger::FixedSizeFont::empty_fixed_size_font::operator=(empty_fixed_size_font&&) noexcept = default;

FontChanger::FixedSizeFont::empty_fixed_size_font& FontChanger::FixedSizeFont::empty_fixed_size_font::operator=(const empty_fixed_size_font&) = default;

void FontChanger::FixedSizeFont::font_render_transformation_matrix::SetIdentity() {
	M11 = M22 = 1.f;
	M12 = M21 = 0.f;
}
