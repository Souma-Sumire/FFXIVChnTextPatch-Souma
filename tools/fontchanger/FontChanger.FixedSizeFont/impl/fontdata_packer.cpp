#include "../include/FontChanger.FixedSizeFont/fontdata_packer.h"

using namespace xivres;

#include <format>

#include "xivres/util.bitmap_copy.h"

#ifdef min
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#include "../include/FontChanger.FixedSizeFont/TeamHypersomnia-rectpack2D/src/finders_interface.h"
#pragma pop_macro("max")
#pragma pop_macro("min")
#else
#include "../include/FontChanger.FixedSizeFont/TeamHypersomnia-rectpack2D/src/finders_interface.h"
#endif

// Whether a codepoint must be left out of glyph and kerning tables.
// Game fonts contain none of these; they are control or invisible formatting characters that break text layout,
// or codepoints that cannot occur in valid text. RTL scripts are left out as the game cannot lay them out.
static bool is_excluded_codepoint(char32_t c) {
	// Format characters (General Category Cf) except U+00AD SOFT HYPHEN, which the game fonts do contain.
	static constexpr std::pair<char32_t, char32_t> FormatCharacters[]{
		{0x0600, 0x0605}, {0x061C, 0x061C}, {0x06DD, 0x06DD}, {0x070F, 0x070F}, {0x0890, 0x0891}, {0x08E2, 0x08E2},
		{0x180E, 0x180E}, {0x200B, 0x200F}, {0x202A, 0x202E}, {0x2060, 0x2064}, {0x2066, 0x206F}, {0xFEFF, 0xFEFF},
		{0xFFF9, 0xFFFB}, {0x110BD, 0x110BD}, {0x110CD, 0x110CD}, {0x13430, 0x1343F}, {0x1BCA0, 0x1BCA3},
		{0x1D173, 0x1D17A}, {0xE0001, 0xE0001}, {0xE0020, 0xE007F},
	};

	if (c < 0x20 || (0x7F <= c && c <= 0x9F))  // C0 controls, DEL, C1 controls
		return true;
	if (0xD800 <= c && c <= 0xDFFF)  // surrogates
		return true;
	if ((0xFDD0 <= c && c <= 0xFDEF) || (c & 0xFFFE) == 0xFFFE || c > 0x10FFFF)  // noncharacters
		return true;
	for (const auto& [first, last] : FormatCharacters) {
		if (first <= c && c <= last)
			return true;
	}
	return (xivres::util::unicode::blocks::block_for(c).Purpose & xivres::util::unicode::blocks::RTL) != 0;
}

FontChanger::FixedSizeFont::fontdata_packer::target_plan::target_glyph::target_glyph(fontdata::stream& font, const fontdata::glyph_entry& entry, size_t sourceFontIndex)
	: Font(font)
	, Entry(entry)
	, SourceFontIndex(sourceFontIndex) {
}

float FontChanger::FixedSizeFont::fontdata_packer::progress_scaled() const {
	return 1.f * static_cast<float>(m_nCurrentProgress) / static_cast<float>(m_nMaxProgress);
}

FontChanger::FixedSizeFont::fontdata_packer::progress_status FontChanger::FixedSizeFont::fontdata_packer::progress_description() const {
	return m_status;
}

bool FontChanger::FixedSizeFont::fontdata_packer::is_running() const {
	return m_status != progress_status::idle;
}

const std::vector<std::shared_ptr<xivres::texture::memory_mipmap_stream>>& FontChanger::FixedSizeFont::fontdata_packer::compiled_mipmap_streams() const {
	return m_targetMipmapStreams;
}

const std::vector<std::shared_ptr<xivres::fontdata::stream>>& FontChanger::FixedSizeFont::fontdata_packer::compiled_fontdatas() const {
	return m_targetFonts;
}

std::string FontChanger::FixedSizeFont::fontdata_packer::get_error_if_failed() const {
	return m_error;
}

void FontChanger::FixedSizeFont::fontdata_packer::compile() {
	if (m_status != progress_status::idle)
		throw std::runtime_error("Compile already in progress");

	m_nMaxProgress = 1;
	m_nCurrentProgress = 0;
	m_bCancelRequested = false;

	std::mutex startMtx;
	auto startLock = std::unique_lock(startMtx);
	std::condition_variable cv;
	m_workerThread = std::thread([this, &cv]() {
		{
			const auto lock = std::scoped_lock(m_runningMtx);
			cv.notify_all();
			try {
				m_status = progress_status::prepare_source_fonts;
				prepare_threadsafe_source_fonts();
				if (m_bCancelRequested) {
					m_status = progress_status::idle;
					return;
				}

				m_status = progress_status::prepare_target_fonts;
				prepare_target_font_basic_info();
				if (m_bCancelRequested) {
					m_status = progress_status::idle;
					return;
				}

				m_status = progress_status::discover_glyphs;
				prepare_target_codepoints();
				if (m_bCancelRequested) {
					m_status = progress_status::idle;
					return;
				}

				m_nMaxProgress = 3 * m_targetPlans.size();
				m_status = progress_status::measure_glyphs;
				measure_glyphs();
				if (m_bCancelRequested) {
					m_status = progress_status::idle;
					return;
				}

				m_status = progress_status::layout_and_draw;
				layout_glyphs();

				m_error.clear();
			} catch (const std::exception& e) {
				m_error = e.what();
			}
			m_status = progress_status::idle;
		}
		m_workerThread.detach();
	});
	cv.wait(startLock);
}

std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font> FontChanger::FixedSizeFont::fontdata_packer::get_font(size_t index) const {
	return m_sourceFonts.at(index);
}

size_t FontChanger::FixedSizeFont::fontdata_packer::add_font(std::shared_ptr<fixed_size_font> font) {
	m_sourceFonts.emplace_back(std::move(font));
	return m_sourceFonts.size() - 1;
}

FontChanger::FixedSizeFont::fontdata_packer::~fontdata_packer() {
	m_bCancelRequested = true;
	wait();
	if (m_workerThread.joinable())
		m_workerThread.join();
}

void FontChanger::FixedSizeFont::fontdata_packer::set_side_length(int n) {
	if (n > 4096)
		throw std::out_of_range("Side length can be up to 4096");
	m_nSideLength = n;
}

void FontChanger::FixedSizeFont::fontdata_packer::set_discard_step(int n) {
	m_nDiscardStep = n;
}

void FontChanger::FixedSizeFont::fontdata_packer::set_thread_count(size_t n) {
	m_nThreads = n;
}

void FontChanger::FixedSizeFont::fontdata_packer::layout_glyphs() {
	using namespace rectpack2D;
	using spaces_type = empty_spaces<false, default_empty_spaces>;
	using rect_type = output_rect_t<spaces_type>;

	std::vector<rect_type> pendingRectangles;
	std::vector<target_plan*> plansInProgress;
	std::vector<target_plan*> plansToTryAgain;
	pendingRectangles.reserve(m_targetPlans.size());
	plansInProgress.reserve(m_targetPlans.size());
	plansToTryAgain.reserve(m_targetPlans.size());

	util::thread_pool::task_waiter waiter;

	for (auto& rectangleInfo : m_targetPlans)
		plansToTryAgain.emplace_back(&rectangleInfo);

	for (size_t planeIndex = 0; !m_bCancelRequested && !plansToTryAgain.empty(); planeIndex++) {
		std::vector<target_plan*> successfulPlans;
		successfulPlans.reserve(m_targetPlans.size());

		pendingRectangles.clear();
		plansInProgress.clear();
		for (const auto pInfo : plansToTryAgain) {
			pendingRectangles.emplace_back(
				0,
				0,
				pInfo->BaseEntry.BoundingWidth + 1,
				pInfo->BaseEntry.BoundingHeight + 1 + pInfo->PadUp + pInfo->PadDown);
			plansInProgress.emplace_back(pInfo);
		}
		plansToTryAgain.clear();

		const auto onPackedRectangle = [this, planeIndex, &successfulPlans, &pendingRectangles, &plansInProgress](rect_type& r) {
			if (m_bCancelRequested)
				return callback_result::ABORT_PACKING;

			++m_nCurrentProgress;
			const auto index = &r - &pendingRectangles.front();
			auto& info = *plansInProgress[index];

			info.BaseEntry.TextureOffsetX = util::range_check_cast<uint16_t>(r.x + 1);
			info.BaseEntry.TextureOffsetY = util::range_check_cast<uint16_t>(r.y + 1);
			info.BaseEntry.TextureIndex = util::range_check_cast<uint16_t>(planeIndex);

			for (auto& target : info.Targets) {
				target.Entry.TextureOffsetX = util::range_check_cast<uint16_t>(r.x + 1 + info.BaseEntry.BoundingWidth - *target.Entry.BoundingWidth);
				target.Entry.TextureOffsetY = static_cast<uint16_t>(r.y + 1 + info.PadUp - target.Entry.TextureOffsetY);
				target.Entry.TextureIndex = static_cast<uint16_t>(planeIndex);
				target.Font.add_glyph(target.Entry);
			}

			successfulPlans.emplace_back(&info);

			return callback_result::CONTINUE_PACKING;
		};

		const auto onFailedRectangle = [this, &plansToTryAgain, &pendingRectangles, &plansInProgress](rect_type& r) {
			if (m_bCancelRequested)
				return callback_result::ABORT_PACKING;

			plansToTryAgain.emplace_back(plansInProgress[&r - &pendingRectangles.front()]);
			return callback_result::CONTINUE_PACKING;
		};

		if (planeIndex == 0) {
			find_best_packing<spaces_type>(
				pendingRectangles,
				make_finder_input(
					m_nSideLength - 1,
					m_nDiscardStep,
					onPackedRectangle,
					onFailedRectangle,
					flipping_option::DISABLED
				)
				);

		} else {
			// Already sorted from above
			find_best_packing_dont_sort<spaces_type>(
				pendingRectangles,
				make_finder_input(
					m_nSideLength - 1,
					m_nDiscardStep,
					onPackedRectangle,
					onFailedRectangle,
					flipping_option::DISABLED
				)
				);
		}

		if (successfulPlans.empty())
			throw std::runtime_error("Failed to pack some characters");

		draw_layoutted_glyphs(waiter, planeIndex, std::move(successfulPlans));
	}

	waiter.wait_all();
}

void FontChanger::FixedSizeFont::fontdata_packer::draw_layoutted_glyphs(util::thread_pool::task_waiter<>& waiter, size_t planeIndex, std::vector<target_plan*> successfulPlans) {
	if (m_bCancelRequested)
		return;

	const auto mipmapIndex = planeIndex >> 2;
	const auto channelIndex = fontdata::glyph_entry::ChannelMap[planeIndex % 4];

	while (m_targetMipmapStreams.size() <= mipmapIndex)
		m_targetMipmapStreams.emplace_back(std::make_shared<texture::memory_mipmap_stream>(m_nSideLength, m_nSideLength, 1, texture::formats::B8G8R8A8));
	const auto& pStream = m_targetMipmapStreams[mipmapIndex];
	const auto pCurrentTargetBuffer = &pStream->as_span<uint8_t>()[channelIndex];

	auto pSuccesses = std::make_shared<std::vector<target_plan*>>(std::move(successfulPlans));

	const auto divideUnit = (std::max<size_t>)(1, static_cast<size_t>(std::sqrt(static_cast<double>(pSuccesses->size()))));

	for (size_t nBase = 0; nBase < divideUnit; nBase++) {
		waiter.submit([this, divideUnit, pSuccesses, nBase, pCurrentTargetBuffer](auto& task) {
			for (size_t i = nBase; i < pSuccesses->size() && !m_bCancelRequested; i += divideUnit) {
				task.throw_if_cancelled();
				++m_nCurrentProgress;
				const auto& info = *(*pSuccesses)[i];

				auto pooledBaseFont = *m_threadSafeBaseFonts.at(info.BaseFont);
				if (!pooledBaseFont)
					pooledBaseFont.emplace(m_baseFonts.at(info.BaseFont)->get_threadsafe_view());
				const auto& baseFont = **pooledBaseFont;

				baseFont.draw(
					info.Codepoint,
					pCurrentTargetBuffer,
					4,
					info.BaseEntry.TextureOffsetX - info.CurrentOffsetX,
					info.BaseEntry.TextureOffsetY - info.BaseEntry.CurrentOffsetY + info.PadUp,
					m_nSideLength,
					m_nSideLength,
					255, 0, 255, 255
				);
			}
		});
	}
}

void FontChanger::FixedSizeFont::fontdata_packer::measure_glyphs() {
	util::thread_pool::task_waiter waiter;

	// The waiter does not pass on what the tasks throw, so the first error is kept here and thrown after all tasks end.
	std::mutex errorMtx;
	std::exception_ptr error;

	const auto divideUnit = (std::max<size_t>)(1, static_cast<size_t>(std::sqrt(static_cast<double>(m_targetPlans.size()))));
	for (size_t nBase = 0; nBase < divideUnit; nBase++) {
		waiter.submit([this, divideUnit, nBase, &errorMtx, &error](auto& task) {
			try {
				measure_glyphs_task(task, nBase, divideUnit);
			} catch (...) {
				const auto lock = std::scoped_lock(errorMtx);
				if (!error)
					error = std::current_exception();
				m_bCancelRequested = true;
			}
		});
	}

	waiter.wait_all();
	if (error)
		std::rethrow_exception(error);
}

void FontChanger::FixedSizeFont::fontdata_packer::measure_glyphs_task(util::thread_pool::base_task& task, size_t nBase, size_t divideUnit) {
	for (size_t i = nBase; i < m_targetPlans.size() && !m_bCancelRequested; i += divideUnit) {
		++m_nCurrentProgress;
		task.throw_if_cancelled();

		auto& info = m_targetPlans[i];

		auto pooledBaseFont = *m_threadSafeBaseFonts.at(info.BaseFont);
		if (!pooledBaseFont)
			pooledBaseFont.emplace(m_baseFonts.at(info.BaseFont)->get_threadsafe_view());
		const auto& baseFont = **pooledBaseFont;

		glyph_metrics gm;
		if (!baseFont.try_get_glyph_metrics(info.Codepoint, gm))
			throw std::runtime_error("Base font reported to have a codepoint but it's failing to report glyph metrics");

		info.CurrentOffsetX = util::range_check_cast<int16_t>((std::min<int>)(0, gm.X1));
		info.BaseEntry.BoundingWidth = util::range_check_cast<uint8_t>(gm.X2 - info.CurrentOffsetX);

		info.PadUp = info.PadDown = 0;
		info.BaseEntry.CurrentOffsetY = util::range_check_cast<int8_t>(gm.Y1);
		info.BaseEntry.BoundingHeight = util::range_check_cast<uint8_t>(gm.height());

		for (auto& target : info.Targets) {
			auto pooledSourceFont = **m_threadSafeSourceFonts[target.SourceFontIndex];
			if (!pooledSourceFont)
				pooledSourceFont.emplace(m_sourceFonts[target.SourceFontIndex]->get_threadsafe_view());

			auto& sourceFont = **pooledSourceFont;

			if (!sourceFont.try_get_glyph_metrics(info.Codepoint, gm))
				throw std::runtime_error("Font reported to have a codepoint but it's failing to report glyph metrics");
			if (gm.X1 < 0)
				throw std::runtime_error("Glyphs for target fonts cannot have negative LSB");
			if (gm.height() != *info.BaseEntry.BoundingHeight) {
				throw std::runtime_error(std::format(
					"Target font has a glyph with different bounding height from the source (U+{:04X}: {} in the target, {} in the source)",
					static_cast<uint32_t>(info.Codepoint), gm.height(), *info.BaseEntry.BoundingHeight));
			}

			if (gm.Y1 > 0)
				target.Entry.TextureOffsetY = util::range_check_cast<uint16_t>(gm.Y1);
			else
				target.Entry.CurrentOffsetY = util::range_check_cast<int8_t>(gm.Y1);
			target.Entry.BoundingHeight = util::range_check_cast<uint8_t>((std::max<int>)(gm.Y2, static_cast<int>(target.Font.line_height())) - (std::min)(0, gm.Y1));
			if (gm.X2 - (std::min)(0, gm.X1) > (std::numeric_limits<uint8_t>::max)()) {
				throw std::runtime_error(std::format(
					"The glyph of U+{:04X} is {} pixels wide, which is more than the font data can store (255).",
					static_cast<uint32_t>(target.Entry.codepoint()), gm.X2 - (std::min)(0, gm.X1)));
			}
			target.Entry.BoundingWidth = static_cast<uint8_t>(gm.X2 - (std::min)(0, gm.X1));

			// The font data stores the advance as the difference from the bounding width in a signed byte.
			if (const auto nextOffsetX = gm.AdvanceX - *target.Entry.BoundingWidth; nextOffsetX < (std::numeric_limits<int8_t>::min)() || nextOffsetX > (std::numeric_limits<int8_t>::max)()) {
				throw std::runtime_error(std::format(
					"The glyph of U+{:04X} advances {} pixels but is {} pixels wide; the advance must be within -128 to 127 pixels of the width. "
					"Reduce the letter spacing or the monospacing width of the element that has it.",
					static_cast<uint32_t>(target.Entry.codepoint()), gm.AdvanceX, *target.Entry.BoundingWidth));
			}
			target.Entry.NextOffsetX = static_cast<int8_t>(gm.AdvanceX - *target.Entry.BoundingWidth);

			if (*info.BaseEntry.BoundingWidth < *target.Entry.BoundingWidth) {
				info.CurrentOffsetX = util::range_check_cast<int16_t>(info.CurrentOffsetX - *target.Entry.BoundingWidth + *info.BaseEntry.BoundingWidth);
				info.BaseEntry.BoundingWidth = *target.Entry.BoundingWidth;
			}

			if (gm.Y1 > info.PadUp)
				info.PadUp = util::range_check_cast<int8_t>(gm.Y1);
			if (info.PadDown + info.PadUp + *info.BaseEntry.BoundingHeight < target.Entry.BoundingHeight)
				info.PadDown = util::range_check_cast<int8_t>(target.Entry.BoundingHeight - info.PadUp - *info.BaseEntry.BoundingHeight);
		}
	}
}

void FontChanger::FixedSizeFont::fontdata_packer::prepare_target_codepoints() {
	std::map<const void*, target_plan*> rectangleInfoMap;
	for (size_t i = 0; i < m_sourceFonts.size(); i++) {
		const auto& font = m_sourceFonts[i];
		for (const auto& codepoint : font->all_codepoints()) {
			if (is_excluded_codepoint(codepoint))
				continue;

			auto& block = util::unicode::blocks::block_for(codepoint);

			const auto uniqid = font->get_base_font_glyph_uniqid(codepoint);
			auto& pInfo = rectangleInfoMap[uniqid];
			if (!pInfo) {
				m_targetPlans.emplace_back();
				pInfo = &m_targetPlans.back();
				pInfo->BaseFont = font->get_base_font(codepoint);
				pInfo->Codepoint = pInfo->BaseFont->uniqid_to_glyph(uniqid);
				if (!m_baseFonts[pInfo->BaseFont])
					m_baseFonts[pInfo->BaseFont] = pInfo->BaseFont->get_threadsafe_view();

				// allocate slot in the map in advance
				(void) m_threadSafeBaseFonts[pInfo->BaseFont];
				pInfo->UnicodeBlock = &block;
				pInfo->BaseEntry.codepoint(pInfo->Codepoint);
			}
			pInfo->Targets.emplace_back(*m_targetFonts[i], fontdata::glyph_entry(), i);
			pInfo->Targets.back().Entry.codepoint(codepoint);
			pInfo->Targets.back().Font.add_glyph(codepoint, 0, 0, 0, 0, 0, 0, 0);
		}
	}
}

void FontChanger::FixedSizeFont::fontdata_packer::prepare_target_font_basic_info() {
	m_targetFonts.clear();
	m_targetFonts.reserve(m_sourceFonts.size());
	for (auto& pSourceFont : m_sourceFonts) {
		m_targetFonts.emplace_back(std::make_shared<fontdata::stream>());

		auto& targetFont = *m_targetFonts.back();
		const auto& sourceFont = *pSourceFont;

		targetFont.texture_width(static_cast<uint16_t>(m_nSideLength));
		targetFont.texture_height(static_cast<uint16_t>(m_nSideLength));
		targetFont.font_size(sourceFont.font_size());
		targetFont.line_height(sourceFont.line_height());
		targetFont.ascent(sourceFont.ascent());
		targetFont.reserve_glyphs(sourceFont.all_codepoints().size());

		const auto& kerningPairs = sourceFont.all_kerning_pairs();
		targetFont.reserve_kernings(kerningPairs.size());
		for (const auto& kp : kerningPairs) {
			if (!is_excluded_codepoint(kp.first.first) && !is_excluded_codepoint(kp.first.second))
				targetFont.add_kerning(kp.first.first, kp.first.second, kp.second);
		}
	}
}

void FontChanger::FixedSizeFont::fontdata_packer::prepare_threadsafe_source_fonts() {
	m_threadSafeSourceFonts.reserve(m_sourceFonts.size());
	size_t nMaxCharacterCount = 0;
	for (const auto& font : m_sourceFonts) {
		nMaxCharacterCount += font->all_codepoints().size();
		m_threadSafeSourceFonts.emplace_back(std::make_unique<util::thread_pool::object_pool<std::shared_ptr<fixed_size_font>>>());
	}

	m_targetPlans.reserve(nMaxCharacterCount);
}

void FontChanger::FixedSizeFont::fontdata_packer::request_cancel() {
	m_bCancelRequested = true;
}
