#include "../include/xivres/xivstring.h"

#include <algorithm>
#include <bit>

#include "../include/xivres/util.on_dtor.h"
#include "../include/xivres/util.span_cast.h"

namespace {
	// Unary and binary expressions nest one byte at a time, so the depth is limited to keep crafted input off the stack's end.
	constexpr size_t MaxExpressionDepth = 256;
	thread_local size_t s_expressionDepth = 0;
}

std::unique_ptr<xivres::xivstring::xivexpr> xivres::xivstring::xivexpr::parse(std::string_view s) {
	if (s_expressionDepth >= MaxExpressionDepth)
		throw std::invalid_argument("Expressions are nested too deep");
	++s_expressionDepth;
	const auto depthGuard = util::on_dtor([] { --s_expressionDepth; });

	if (s.empty())
		throw std::invalid_argument("Truncated expression");
	const auto marker = static_cast<uint8_t>(s[0]);
	if (marker < 0xD0)
		return std::make_unique<xivexpr_uint32>(s);
	if (marker < 0xE0)
		return std::make_unique<xivexpr_param>(s);
	if (marker < 0xE6)
		return std::make_unique<xivexpr_binary>(s);
	if (marker < 0xEC)
		return std::make_unique<xivexpr_unary>(s);
	if (marker == 0xEC)
		return std::make_unique<xivexpr_param>(s);
	if (marker < 0xF0)
		throw std::invalid_argument("Unsupported expression type");
	if (marker < 0xFF)
		return std::make_unique<xivexpr_uint32>(s);
	return std::make_unique<xivexpr_string>(s);
}

xivres::xivstring::xivpayload::xivpayload(std::string_view s)
	: m_type(static_cast<xivpayload_type>(s.at(0))) {
	const auto length = xivexpr_uint32(s.substr(1));
	if (length.value() > s.size() - 1 - length.size())
		throw std::invalid_argument("Payload length exceeds the remaining bytes");
	m_escaped = s.substr(0, 1 + length.size() + length.value());
}

xivres::xivstring::xivpayload::xivpayload(xivpayload_type payloadType, std::vector<std::unique_ptr<xivexpr>> expressions)
	: m_type(payloadType)
	, m_expressions(std::move(expressions)) {
	if (std::ranges::any_of(m_expressions, [](const auto& e) { return !e; }))
		throw std::invalid_argument("Null expression");
}

std::string xivres::xivstring::xivpayload::enclosed() const {
	const auto& e = escape();
	std::string res;
	res.reserve(size_t{2} + e.size());
	res.push_back(StartOfText);
	res += e;
	res.push_back(EndOfText);
	return res;
}

void xivres::xivstring::xivpayload::type(xivpayload_type t) {
	m_type = t;
	if (!m_escaped.empty())
		m_escaped[0] = static_cast<char>(t);
}

xivres::xivstring::xivpayload::xivpayload(const xivpayload& r)
	: m_type(r.m_type)
	, m_escaped(r.m_escaped) {
	m_expressions.reserve(r.m_expressions.size());
	for (const auto& expr : r.m_expressions)
		m_expressions.emplace_back(expr->clone());
}

xivres::xivstring::xivpayload& xivres::xivstring::xivpayload::operator=(const xivpayload& r) {
	if (this == &r)
		return *this;

	m_type = r.m_type;
	m_escaped = r.m_escaped;
	m_expressions.clear();
	m_expressions.reserve(r.m_expressions.size());
	for (const auto& expr : r.m_expressions)
		m_expressions.emplace_back(expr->clone());
	return *this;
}

std::string xivres::xivstring::xivpayload::repr() const {
	static_cast<void>(parse());
	std::string res = "<payload type=\"";
	switch (m_type) {
		case xivpayload_type::Unset: res += "Unset";
			break;
		case xivpayload_type::SetResetTime: res += "SetResetTime";
			break;
		case xivpayload_type::SetTime: res += "SetTime";
			break;
		case xivpayload_type::If: res += "If";
			break;
		case xivpayload_type::Switch: res += "Switch";
			break;
		case xivpayload_type::PcName: res += "PcName";
			break;
		case xivpayload_type::IfPcGender: res += "IfPcGender";
			break;
		case xivpayload_type::IfPcName: res += "IfPcName";
			break;
		case xivpayload_type::Josa: res += "Josa";
			break;
		case xivpayload_type::Josaro: res += "Josaro";
			break;
		case xivpayload_type::IfSelf: res += "IfSelf";
			break;
		case xivpayload_type::NewLine: res += "NewLine";
			break;
		case xivpayload_type::Wait: res += "Wait";
			break;
		case xivpayload_type::Icon: res += "Icon";
			break;
		case xivpayload_type::Color: res += "Color";
			break;
		case xivpayload_type::EdgeColor: res += "EdgeColor";
			break;
		case xivpayload_type::ShadowColor: res += "ShadowColor";
			break;
		case xivpayload_type::SoftHyphen: res += "SoftHyphen";
			break;
		case xivpayload_type::Key: res += "Key";
			break;
		case xivpayload_type::Scale: res += "Scale";
			break;
		case xivpayload_type::Bold: res += "Bold";
			break;
		case xivpayload_type::Italic: res += "Italic";
			break;
		case xivpayload_type::Edge: res += "Edge";
			break;
		case xivpayload_type::Shadow: res += "Shadow";
			break;
		case xivpayload_type::NonBreakingSpace: res += "NonBreakingSpace";
			break;
		case xivpayload_type::Icon2: res += "Icon2";
			break;
		case xivpayload_type::Hyphen: res += "Hyphen";
			break;
		case xivpayload_type::Num: res += "Num";
			break;
		case xivpayload_type::Hex: res += "Hex";
			break;
		case xivpayload_type::Kilo: res += "Kilo";
			break;
		case xivpayload_type::Byte: res += "Byte";
			break;
		case xivpayload_type::Sec: res += "Sec";
			break;
		case xivpayload_type::Time: res += "Time";
			break;
		case xivpayload_type::Float: res += "Float";
			break;
		case xivpayload_type::Link: res += "Link";
			break;
		case xivpayload_type::Sheet: res += "Sheet";
			break;
		case xivpayload_type::String: res += "String";
			break;
		case xivpayload_type::Caps: res += "Caps";
			break;
		case xivpayload_type::Head: res += "Head";
			break;
		case xivpayload_type::Split: res += "Split";
			break;
		case xivpayload_type::HeadAll: res += "HeadAll";
			break;
		case xivpayload_type::Fixed: res += "Fixed";
			break;
		case xivpayload_type::Lower: res += "Lower";
			break;
		case xivpayload_type::JaNoun: res += "JaNoun";
			break;
		case xivpayload_type::EnNoun: res += "EnNoun";
			break;
		case xivpayload_type::DeNoun: res += "DeNoun";
			break;
		case xivpayload_type::FrNoun: res += "FrNoun";
			break;
		case xivpayload_type::ChNoun: res += "ChNoun";
			break;
		case xivpayload_type::LowerHead: res += "LowerHead";
			break;
		case xivpayload_type::SheetSub: res += "SheetSub";
			break;
		case xivpayload_type::SwitchPlatform: res += "SwitchPlatform";
			break;
		case xivpayload_type::ColorType: res += "ColorType";
			break;
		case xivpayload_type::EdgeColorType: res += "EdgeColorType";
			break;
		case xivpayload_type::Ruby: res += "Ruby";
			break;
		case xivpayload_type::Digit: res += "Digit";
			break;
		case xivpayload_type::Ordinal: res += "Ordinal";
			break;
		case xivpayload_type::Sound: res += "Sound";
			break;
		case xivpayload_type::LevelPos: res += "LevelPos";
			break;
		default: res += std::format("0x{:02x}", static_cast<uint8_t>(m_type));
	}
	res += "\">";
	for (const auto& expr : m_expressions)
		res += expr->repr();
	res += "</payload>";
	return res;
}

const std::vector<std::unique_ptr<xivres::xivstring::xivexpr>>& xivres::xivstring::xivpayload::parse() const {
	if (!m_expressions.empty() || m_escaped.empty())
		return m_expressions;

	const auto length = xivexpr_uint32(std::string_view(m_escaped).substr(1));

	std::vector<std::unique_ptr<xivexpr>> expressions;
	for (auto remaining = std::string_view(m_escaped).substr(1 + length.size()); !remaining.empty();) {
		expressions.emplace_back(xivexpr::parse(remaining));
		remaining = remaining.substr(expressions.back()->size());
	}

	return m_expressions = std::move(expressions);
}

const std::string& xivres::xivstring::xivpayload::escape() const {
	if (!m_escaped.empty())
		return m_escaped;

	// The type, the length, then the expressions; a payload without expressions still has the first two.
	std::string body;
	for (const auto& expr : m_expressions) {
		const auto i = body.size();
		body.resize(i + expr->size());
		static_cast<void>(expr->encode(std::span(body).subspan(i)));
	}

	const auto length = xivexpr_uint32(static_cast<uint32_t>(body.size()));

	std::string res;
	res.reserve(1 + length.size() + length.value());
	res.resize(1 + length.size());
	res[0] = static_cast<char>(m_type);
	static_cast<void>(length.encode(std::span(res).subspan(1)));
	res.insert(res.end(), body.begin(), body.end());

	return m_escaped = std::move(res);
}

xivres::xivstring::xivstring() = default;

xivres::xivstring::xivstring(const xivstring&) = default;

xivres::xivstring::xivstring(xivstring&&) noexcept = default;

xivres::xivstring& xivres::xivstring::operator=(const xivstring&) = default;

xivres::xivstring& xivres::xivstring::operator=(xivstring&&) noexcept = default;

xivres::xivstring::xivstring(std::string s, std::vector<xivpayload> payloads) {
	parsed(std::move(s), std::move(payloads));
}

xivres::xivstring::xivstring(std::string s) {
	escaped(std::move(s));
}

bool xivres::xivstring::use_newline_payload() const {
	return m_bUseNewlinePayload;
}

void xivres::xivstring::use_newline_payload(bool enable) {
	static_cast<void>(escape());

	m_parsed.clear();
	m_payloads.clear();
	m_bUseNewlinePayload = enable;
}

bool xivres::xivstring::empty() const {
	return m_escaped.empty() && m_parsed.empty();
}

std::string xivres::xivstring::repr() const {
	static_cast<void>(parse());
	std::string res;

	size_t escapeIndex = 0;
	for (const auto chr : m_parsed) {
		if (chr != StartOfText) {
			res += chr;
		} else {
			const auto& payload = m_payloads[escapeIndex++];
			res += payload.repr();
		}
	}

	return res;
}

const std::string& xivres::xivstring::parsed() const {
	static_cast<void>(parse());
	return m_parsed;
}

xivres::xivstring& xivres::xivstring::parsed(std::string s, std::vector<xivpayload> payloads) {
	verify_component_validity_or_throw(s, payloads);

	m_parsed = std::move(s);
	m_payloads = std::move(payloads);
	m_escaped.clear();
	return *this;
}

xivres::xivstring& xivres::xivstring::parsed(std::string s) {
	static_cast<void>(parse());

	verify_component_validity_or_throw(s, m_payloads);
	m_parsed = std::move(s);
	m_escaped.clear();
	return *this;
}

xivres::xivstring& xivres::xivstring::escaped(std::string e) {
	m_escaped = std::move(e);
	m_parsed.clear();
	m_payloads.clear();
	return *this;
}

const std::string& xivres::xivstring::escaped() const {
	return escape();
}

const std::vector<xivres::xivstring::xivpayload>& xivres::xivstring::payloads() const {
	static_cast<void>(parse());
	return m_payloads;
}

const std::string& xivres::xivstring::parse() const {
	if (!m_parsed.empty() || m_escaped.empty())
		return m_parsed;

	std::string parsed;
	std::vector<xivpayload> payloads;
	parsed.reserve(m_escaped.size());

	std::string_view remaining(m_escaped);
	while (!remaining.empty()) {
		if (remaining[0] == StartOfText) {
			if (remaining.size() < 3)
				throw std::invalid_argument("STX occurred but there are less than 3 remaining bytes");
			remaining = remaining.substr(1);

			payloads.emplace_back(remaining);
			remaining = remaining.substr(payloads.back().size());

			if (remaining.empty() || remaining[0] != EndOfText)
				throw std::invalid_argument("ETX not found");
			remaining = remaining.substr(1);

			if (m_bUseNewlinePayload && payloads.back().type() == xivpayload_type::NewLine) {
				parsed.push_back('\r');
				payloads.pop_back();
			} else {
				parsed.push_back(StartOfText);
			}
		} else {
			parsed.push_back(remaining.front());
			remaining = remaining.substr(1);
		}
	}

	m_parsed = std::move(parsed);
	m_payloads = std::move(payloads);
	return m_parsed;
}

const std::string& xivres::xivstring::escape() const {
	if (!m_escaped.empty() || m_parsed.empty())
		return m_escaped;

	size_t reserveSize = m_parsed.size();
	for (const auto& payload : m_payloads) {
		reserveSize += size_t()
			+ 1 // STX
			+ payload.size() // payload type, length, and expressions
			+ 1; // ETX
	}

	std::string res;
	res.reserve(reserveSize);

	size_t escapeIndex = 0;
	for (const auto chr : m_parsed) {
		if (chr == '\r' && m_bUseNewlinePayload) {
			res += "\x02\x10\x01\x03"; // STX, SeExpressionUint32(PayloadType(NewLine)), SeExpressionUint32(0), ETX
		} else if (chr != StartOfText) {
			res += chr;
		} else {
			// The payload's escaped form already starts with its type and length.
			const auto& payload = m_payloads[escapeIndex++];
			res += StartOfText;
			res += payload.escaped();
			res += EndOfText;
		}
	}

	m_escaped = std::move(res);
	return m_escaped;
}

void xivres::xivstring::verify_component_validity_or_throw(const std::string& parsed, const std::vector<xivpayload>& components) const {
	const auto cnt = static_cast<size_t>(std::ranges::count(parsed, StartOfText));
	if (cnt != components.size())
		throw std::invalid_argument(std::format("number of sentinel characters({}) != expected number of sentinel characters({})", cnt, components.size()));
}

size_t xivres::xivstring::read_integer(std::span<const uint8_t> s, uint32_t& value) {
	value = 0;
	if (s.empty() || s[0] == 0x00 || s[0] == 0xFF)
		return 0;

	if (s[0] < 0xF0) {
		value = s[0] - 1u;
		return 1;
	}

	// The marker's low bits say which bytes of the value (highest first) follow; the others are zero.
	const auto flags = s[0] + 1u;
	uint32_t res = 0;
	size_t n = 1;
	for (auto bit = 3; bit >= 0; bit--) {
		if (!(flags & (1u << bit)))
			continue;
		if (n >= s.size())
			return 0;
		res |= static_cast<uint32_t>(s[n++]) << (8 * bit);
	}

	value = res;
	return n;
}

std::optional<xivres::xivstring::macro_info> xivres::xivstring::read_macro(std::span<const uint8_t> s) {
	// StartOfText, the type, at least a byte of length, and EndOfText.
	if (s.size() < 4 || s[0] != static_cast<uint8_t>(StartOfText))
		return std::nullopt;

	uint32_t payloadSize;
	const auto lengthSize = read_integer(s.subspan(2), payloadSize);
	if (!lengthSize)
		return std::nullopt;

	// The payload and EndOfText have to fit in what follows the length.
	const auto payloadOffset = 2 + lengthSize;
	if (payloadSize >= s.size() - payloadOffset)
		return std::nullopt;

	const auto endOffset = payloadOffset + payloadSize;
	if (s[endOffset] != static_cast<uint8_t>(EndOfText))
		return std::nullopt;

	return macro_info{
		.type = static_cast<xivpayload_type>(s[1]),
		.payload = s.subspan(payloadOffset, payloadSize),
		.size = endOffset + 1,
	};
}

size_t xivres::xivstring::character_length(std::span<const uint8_t> s) {
	if (s.empty())
		return 0;
	const auto b = s[0];
	const size_t n = b < 0xC0 ? 1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : b < 0xF8 ? 4 : 1;
	return n <= s.size() ? n : 0;
}

xivres::xivstring::xivexpr_uint32::xivexpr_uint32(std::string_view s) {
	if (s.empty())
		throw std::invalid_argument("Empty uint32 expression");
	const auto data = util::span_cast<uint8_t>(s);
	if (0x00 < data[0] && data[0] < 0xD0) {
		m_value = data[0] - 1u;
	} else if (0xF0 <= data[0] && data[0] <= 0xFE) {
		const auto marker = data[0] + 1u;
		uint32_t res = 0;
		size_t offset = 0;
		for (auto bit = 3; bit >= 0; bit--) {
			if (!(marker & (1u << bit)))
				continue;
			if (++offset >= data.size())
				throw std::invalid_argument("Truncated uint32 expression");
			res |= static_cast<uint32_t>(data[offset]) << (8 * bit);
		}
		m_value = res;

		// Kept so size(), which parsing steps by, counts the bytes read even when the value was encoded in more bytes than needed.
		m_marker = data[0];
	} else {
		throw std::invalid_argument("Not an uint32 expression");
	}
}

uint8_t xivres::xivstring::xivexpr_uint32::encoding_marker() const {
	if (!m_marker)
		return 0;

	// The value may have been changed through operator*; the parsed form is kept only while it still holds the value.
	const auto flags = m_marker + 1u;
	uint32_t mask = 0;
	for (auto bit = 0; bit < 4; bit++) {
		if (flags & (1u << bit))
			mask |= 0xFFu << (8 * bit);
	}
	return (m_value & ~mask) ? 0 : m_marker;
}

xivres::xivstring::xivexpr_type xivres::xivstring::xivexpr_uint32::type() const {
	if (const auto marker = encoding_marker())
		return static_cast<xivexpr_type>(marker);
	if (m_value < 0xCF)
		return static_cast<xivexpr_type>(m_value + 1);
	uint32_t flags = 0;
	for (auto bit = 0; bit < 4; bit++) {
		if (m_value & (0xFFu << (8 * bit)))
			flags |= 1u << bit;
	}
	return static_cast<xivexpr_type>(0xF0 + flags - 1);
}

size_t xivres::xivstring::xivexpr_uint32::size() const {
	if (const auto marker = encoding_marker())
		return size_t{1} + std::popcount((marker + 1u) & 0xFu);
	if (m_value < 0xCF)
		return 1;
	return size_t{1}
		+ !!(m_value & 0xFF000000)
		+ !!(m_value & 0x00FF0000)
		+ !!(m_value & 0x0000FF00)
		+ !!(m_value & 0x000000FF);
}

size_t xivres::xivstring::xivexpr_uint32::encode(std::span<char> s) const {
	const auto res = util::span_cast<uint8_t>(s);
	if (const auto marker = encoding_marker()) {
		res[0] = marker;
		const auto flags = marker + 1u;
		size_t offset = 1;
		for (auto bit = 3; bit >= 0; bit--) {
			if (flags & (1u << bit))
				res[offset++] = static_cast<uint8_t>(m_value >> (8 * bit));
		}
		return offset;
	}
	if (m_value < 0xCF) {
		res[0] = static_cast<uint8_t>(m_value + 1);
		return 1;
	} else {
		res[0] = 0xF0;
		size_t offset = 1;
		if (const auto v = static_cast<uint8_t>(m_value >> 24)) {
			res[0] |= 8;
			res[offset++] = v;
		}
		if (const auto v = static_cast<uint8_t>(m_value >> 16)) {
			res[0] |= 4;
			res[offset++] = v;
		}
		if (const auto v = static_cast<uint8_t>(m_value >> 8)) {
			res[0] |= 2;
			res[offset++] = v;
		}
		if (const auto v = static_cast<uint8_t>(m_value)) {
			res[0] |= 1;
			res[offset++] = v;
		}
		res[0] -= 1;
		return offset;
	}
}

std::string xivres::xivstring::xivexpr_uint32::repr() const {
	return std::format("<expr type=\"uint32\">{}</expr>", m_value);
}

xivres::xivstring::xivexpr_string::xivexpr_string(std::string_view s) {
	if (s.empty() || s.front() != static_cast<char>(xivexpr_type::XivString))
		throw std::invalid_argument("Not a xivstring expression");
	const xivexpr_uint32 length(s.substr(1));
	if (*length > s.size() - 1 - length.size())
		throw std::invalid_argument("String expression length exceeds the remaining bytes");
	m_value.escaped(std::string(s.substr(1 + length.size(), *length)));
	m_length = length;

	// Parsed now, so that the nesting depth limit also covers strings nested in strings.
	for (const auto& payload : m_value.payloads())
		static_cast<void>(payload.params());
}

xivres::xivstring::xivexpr_uint32 xivres::xivstring::xivexpr_string::encoding_length() const {
	// Assigning through operator* keeps the parsed encoding for as long as it can hold the current length.
	auto length = m_length;
	*length = static_cast<uint32_t>(m_value.escaped().size());
	return length;
}

size_t xivres::xivstring::xivexpr_string::size() const {
	const auto length = encoding_length();
	return 1 + length.size() + *length;
}

size_t xivres::xivstring::xivexpr_string::encode(std::span<char> s) const {
	const auto original = s;
	s.front() = static_cast<char>(xivexpr_type::XivString);
	s = s.subspan(1);

	const auto length = encoding_length();
	s = s.subspan(length.encode(s));

	static_cast<void>(std::ranges::copy(m_value.escaped(), s.begin()));
	s = s.subspan(m_value.escaped().size());
	return original.size() - s.size();
}

std::string xivres::xivstring::xivexpr_string::repr() const {
	return std::format("<expr type=\"string\">{}</expr>", m_value.repr());
}

xivres::xivstring::xivexpr_param::xivexpr_param(std::string_view s) {
	m_type = static_cast<xivexpr_type>(s.at(0));
	if (m_type != xivexpr_type::Xec && (static_cast<uint8_t>(m_type) < 0xd0 || static_cast<uint8_t>(m_type) > 0xdf))
		throw std::invalid_argument("Not a parameter expression");
}

size_t xivres::xivstring::xivexpr_param::size() const {
	return 1;
}

size_t xivres::xivstring::xivexpr_param::encode(std::span<char> s) const {
	s.front() = static_cast<char>(m_type);
	return 1;
}

std::string xivres::xivstring::xivexpr_param::repr() const {
	switch (m_type) {
		case xivexpr_type::Hour: return "<expr type=\"Param\">hour</expr>";
		case xivexpr_type::Weekday: return "<expr type=\"Param\">weekday</expr>";
		case xivexpr_type::Xd0:
		case xivexpr_type::Xd1:
		case xivexpr_type::Xd2:
		case xivexpr_type::Xd3:
		case xivexpr_type::Xd4:
		case xivexpr_type::Xd5:
		case xivexpr_type::Xd6:
		case xivexpr_type::Xd7:
		case xivexpr_type::Xd8:
		case xivexpr_type::Xd9:
		case xivexpr_type::Xda:
		case xivexpr_type::Xdc:
		case xivexpr_type::Xde:
		case xivexpr_type::Xdf:
		case xivexpr_type::Xec:
			return std::format("<expr type=\"Param\">unknown:0x{:02x}</expr>", static_cast<int>(m_type));
		default:
			return std::format("<expr type=\"Param\">invalid:0x{:02x}</expr>", static_cast<int>(m_type));
	}
}

xivres::xivstring::xivexpr_unary::xivexpr_unary(std::string_view s) {
	m_type = static_cast<xivexpr_type>(s.at(0));
	if (static_cast<uint8_t>(m_type) < 0xe8 || static_cast<uint8_t>(m_type) > 0xeb)
		throw std::invalid_argument("Not an unary expression");

	m_operand = parse(s.substr(1));
}

xivres::xivstring::xivexpr_unary::xivexpr_unary(const xivexpr_unary& r)
	: m_type(r.m_type)
	, m_operand(r.m_operand ? r.m_operand->clone() : nullptr) {
}

xivres::xivstring::xivexpr_unary& xivres::xivstring::xivexpr_unary::operator=(const xivexpr_unary& r) {
	if (this == &r)
		return *this;

	m_type = r.m_type;
	m_operand = r.m_operand ? r.m_operand->clone() : nullptr;
	return *this;
}

size_t xivres::xivstring::xivexpr_unary::size() const {
	return 1 + m_operand->size();
}

size_t xivres::xivstring::xivexpr_unary::encode(std::span<char> s) const {
	s.front() = static_cast<char>(m_type);
	return 1 + m_operand->encode(s.subspan(1));
}

std::string xivres::xivstring::xivexpr_unary::repr() const {
	std::string res;
	switch (m_type) {
		case xivexpr_type::IntegerParameter:
			res = "<expr type=\"IntParam\">";
			break;
		case xivexpr_type::PlayerParameter:
			res = "<expr type=\"PlayerParam\">";
			break;
		case xivexpr_type::StringParameter:
			res = "<expr type=\"StringParam\">";
			break;
		case xivexpr_type::ObjectParameter:
			res = "<expr type=\"ObjectParam\">";
			break;
		default:
			res = std::format("<expr type=\"InvalidParam:0x{:02x}\">", static_cast<int>(m_type));
	}
	res += m_operand->repr();
	res += "</expr>";
	return res;
}

xivres::xivstring::xivexpr_binary::xivexpr_binary(std::string_view s) {
	m_type = static_cast<xivexpr_type>(s.at(0));
	if (static_cast<uint8_t>(m_type) < 0xe0 || static_cast<uint8_t>(m_type) > 0xe5)
		throw std::invalid_argument("Not an unary expression");

	m_operand1 = parse(s.substr(1));
	m_operand2 = parse(s.substr(1 + m_operand1->size()));
}

xivres::xivstring::xivexpr_binary::xivexpr_binary(const xivexpr_binary& r)
	: m_type(r.m_type)
	, m_operand1(r.m_operand1 ? r.m_operand1->clone() : nullptr)
	, m_operand2(r.m_operand2 ? r.m_operand2->clone() : nullptr) {
}

xivres::xivstring::xivexpr_binary& xivres::xivstring::xivexpr_binary::operator=(const xivexpr_binary& r) {
	if (this == &r)
		return *this;

	m_type = r.m_type;
	m_operand1 = r.m_operand1 ? r.m_operand1->clone() : nullptr;
	m_operand2 = r.m_operand2 ? r.m_operand2->clone() : nullptr;
	return *this;
}

size_t xivres::xivstring::xivexpr_binary::size() const {
	return 1 + m_operand1->size() + m_operand2->size();
}

size_t xivres::xivstring::xivexpr_binary::encode(std::span<char> s) const {
	s.front() = static_cast<char>(m_type);

	size_t len = 1;
	len += m_operand1->encode(s.subspan(len));
	len += m_operand2->encode(s.subspan(len));
	return len;
}

std::string xivres::xivstring::xivexpr_binary::repr() const {
	std::string res;
	switch (m_type) {
		case xivexpr_type::GreaterThanOrEqualTo:
			res = "<expr type=\"gte\">";
			break;
		case xivexpr_type::GreaterThan:
			res = "<expr type=\"gt\">";
			break;
		case xivexpr_type::LessThanOrEqualTo:
			res = "<expr type=\"lte\">";
			break;
		case xivexpr_type::LessThan:
			res = "<expr type=\"lt\">";
			break;
		case xivexpr_type::Equal:
			res = "<expr type=\"eq\">";
			break;
		case xivexpr_type::NotEqual:
			res = "<expr type=\"ne\">";
			break;
		default:
			res = std::format("<expr type=\"InvalidBinary:0x{:02x}\">", static_cast<int>(m_type));
	}
	res += m_operand1->repr();
	res += m_operand2->repr();
	res += "</expr>";
	return res;
}
