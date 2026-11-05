#include "../include/xivres/packed_stream.oplocking.h"

#include "../include/xivres/packed_stream.model.h"
#include "../include/xivres/packed_stream.standard.h"
#include "../include/xivres/packed_stream.texture.h"
#include "../include/xivres/util.unicode.h"

xivres::oplocking_packed_stream::oplocking_packed_stream(xivres::path_spec pathSpec, std::filesystem::path path)
	: packed_stream(std::move(pathSpec))
	, m_path(std::move(path)) {

	const auto ext = util::unicode::convert<std::string>(m_path.extension().wstring(), &util::unicode::lower);
	if (ext == ".tex")
		m_packedType = packed::type::texture;
	else if (ext == ".mdl")
		m_packedType = packed::type::model;
	else
		m_packedType = packed::type::standard;
}

xivres::packed::type xivres::oplocking_packed_stream::get_packed_type() const {
	return m_packedType;
}

std::streamsize xivres::oplocking_packed_stream::size() const {
	const auto packedStream = packed();
	return packedStream ? packedStream->size() : 0;
}

std::streamsize xivres::oplocking_packed_stream::read(std::streamoff offset, void* buf, std::streamsize length) const {
	const auto packedStream = packed();
	return packedStream ? packedStream->read(offset, buf, length) : 0;
}

void xivres::oplocking_packed_stream::hold_until(std::chrono::steady_clock::time_point until) const {
	const auto lock = std::scoped_lock(m_mtx);
	if (m_file)
		m_file->hold_until(until);
}

void xivres::oplocking_packed_stream::close() {
	const auto lock = std::scoped_lock(m_mtx);
	m_packedStream.reset();
	m_file.reset();
}

std::shared_ptr<xivres::packed_stream> xivres::oplocking_packed_stream::packed() const {
	const auto lock = std::scoped_lock(m_mtx);

	if (!m_file) {
		if (!exists(m_path))
			return nullptr;
		m_file = std::make_shared<oplocking_file_stream>(m_path, true);
	}

	if (m_file->done())
		return nullptr;

	if (m_packedStream && m_packedGeneration == m_file->generation())
		return m_packedStream;

	m_packedGeneration = m_file->generation();
	switch (m_packedType) {
		case packed::type::standard:
			m_packedStream = std::make_shared<passthrough_packed_stream<standard_passthrough_packer>>(path_spec(), m_file);
			break;
		case packed::type::model:
			m_packedStream = std::make_shared<passthrough_packed_stream<model_passthrough_packer>>(path_spec(), m_file);
			break;
		case packed::type::texture:
			m_packedStream = std::make_shared<passthrough_packed_stream<texture_passthrough_packer>>(path_spec(), m_file);
			break;
		default:
			return nullptr;
	}
	return m_packedStream;
}
