#include "pch.h"
#include "FontsFromSystemFont.Internal.h"

using namespace FontChanger::FontsFromSystemFont::Internal;

namespace {
	bool IsSameFace(const LookupStruct& a, const LookupStruct& b) {
		return a.FamilyName == b.FamilyName && a.Weight == b.Weight && a.Stretch == b.Stretch && a.Style == b.Style;
	}

	std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font> GetGameFont(const FontChanger::Structs::Font& font) {
		const auto it = std::ranges::find(font.Elements, FontChanger::Structs::ElementSourceEnum::Game, [](const auto& e) { return e->Source; });
		if (it == font.Elements.end())
			return nullptr;
		try {
			if (auto res = (*it)->GetBaseFont(); res && !res->all_codepoints().empty())
				return res;
		} catch (const std::exception&) {}
		return nullptr;
	}

	// Where an element's size isn't Auto, its vertical offset and render mode are fitted to its own size.
	bool FitElements(
		FontChanger::Structs::Font& font, const Traits& traits, float size, const Placement& placement, FontCache& open,
		const FontChanger::FixedSizeFont::fixed_size_font* gameFont, const std::vector<std::pair<FontElement*, ScriptGroup>>& fitted) {
		using FontChanger::CodepointRanges::Contains;
		using FontChanger::FontsFromSystemFont::CanAutoFit;

		// The element that draws each glyph after the merging tells which of the game's glyphs stay, and which font draws
		// the capitals that the other scripts are sized next to.
		font.OnElementChange();
		const auto merged = font.GetMergedFont();
		std::map<const CjkGroup*, bool> leavesGameGlyphs;
		const auto isLeftToGame = [&](const CjkGroup& group) {
			if (const auto it = leavesGameGlyphs.find(&group); it != leavesGameGlyphs.end())
				return it->second;

			size_t total = 0, left = 0;
			if (gameFont) {
				for (const auto c : gameFont->all_codepoints()) {
					if (!Contains(group.Codepoints, c))
						continue;
					total++;
					if (merged->get_base_font(c) == gameFont)
						left++;
				}
			}
			return leavesGameGlyphs[&group] = left * 20 > total;
		};

		const FontElement* baseElement = nullptr;
		for (const auto c : traits.ReferenceCharacters) {
			const auto drawer = merged->get_base_font(c);
			for (const auto& element : font.Elements) {
				if (CanAutoFit(*element) && Contains(element->WrapModifiers.Codepoints, c) && element->GetBaseFont().get() == drawer) {
					baseElement = element.get();
					break;
				}
			}
			if (baseElement)
				break;
		}

		const auto baseFace = baseElement ? open(baseElement->Lookup) : nullptr;
		std::optional<LatinPlacement> baseLatin;
		if (baseFace) {
			if (baseElement->Auto.Size && std::ranges::any_of(fitted, [baseElement](const auto& f) { return f.first == baseElement; }))
				baseLatin = FitLatin(*baseFace, traits, size);
			else
				baseLatin = LatinPlacement{baseElement->Size, GetSizeAndMode(*baseFace, baseElement->Size).second, baseElement->WrapModifiers.VerticalOffset};
		}

		// In AXIS, the base font's CJK scripts move up apart from its Latin letters as the game's do.
		const auto placeAsBase = [&](const InkRange& ink) {
			const auto [baseSize, renderMode] = GetSizeAndMode(*baseFace, baseLatin->Size);
			return KeepWithinLine(*baseFace, ink, {baseSize, renderMode, placement.LatinOffset, baseLatin->VerticalOffset + placement.CjkShift}, LineMetrics::Of(*merged), baseLatin->Size);
		};

		auto changed = false;
		const auto targetCapHeight = size * traits.ReferenceHeight;
		for (const auto& [pElement, groupId] : fitted) {
			const auto& face = *open(pElement->Lookup);
			const auto latin = FitLatin(face, traits, size);
			GroupPlacement placed{latin.Size, latin.RenderMode, placement.LatinOffset, latin.VerticalOffset};

			const auto group = CjkGroup::Of(groupId);
			const auto ink = group ? MeasureInk(face, group->Samples) : std::nullopt;
			if (ink) {
				// Drawn as the base font draws them, unless it lacks them or the game's glyphs of them stay, which are drawn as
				// the game draws them next to its own Latin letters.
				const auto latinSize = baseLatin ? baseLatin->Size : latin.Size;
				const auto baseInk = baseFace ? MeasureInk(*baseFace, group->Samples) : std::nullopt;
				if (isLeftToGame(*group) || !baseInk) {
					placed = FitInk(face, *ink, group->TopPerCapHeight * targetCapHeight, group->BottomPerCapHeight * targetCapHeight, latinSize, placement.LatinOffset);
				} else if (IsSameFace(pElement->Lookup, baseElement->Lookup)) {
					placed = placeAsBase(*ink);
				} else {
					const auto base = placeAsBase(*baseInk);
					placed = FitInk(face, *ink, baseInk->Top * base.Size - base.VerticalOffset, baseInk->Bottom * base.Size - base.VerticalOffset, latinSize, placement.LatinOffset);
				}
			}

			// A size given by the user keeps Latin letters on the baseline, and other scripts' ink centered where it would be.
			auto& e = *pElement;
			const auto actualSize = e.Auto.Size || e.Size <= 0 ? placed.Size : e.Size;
			auto verticalOffset = placed.VerticalOffset;
			if (actualSize != placed.Size) {  // NOLINT(clang-diagnostic-float-equal)
				if (ink) {
					verticalOffset = std::round(placed.VerticalOffset + (ink->Top + ink->Bottom) / 2 * (actualSize - placed.Size));
				} else if (const auto caps = MeasureInk(face, traits.ReferenceCharacters)) {
					verticalOffset = std::round((std::max)(0.f, caps->Bottom) * actualSize);
				}
			}

			auto createChanged = false, wrapChanged = false;
			const auto set = [](float& field, float value, bool& flag) {
				if (field != value) {  // NOLINT(clang-diagnostic-float-equal)
					field = value;
					flag = true;
				}
			};
			if (e.Auto.Size)
				set(e.Size, actualSize, createChanged);
			if (e.Auto.RenderMode && e.Source == FontChanger::Structs::ElementSourceEnum::DirectWrite) {
				if (const auto mode = GetSizeAndMode(face, actualSize).second; e.RendererSpecific.DirectWrite.RenderMode != mode) {
					e.RendererSpecific.DirectWrite.RenderMode = mode;
					createChanged = true;
				}
			}
			if (e.Auto.HorizontalOffset)
				set(e.WrapModifiers.HorizontalOffset, placed.HorizontalOffset, wrapChanged);
			if (e.Auto.VerticalOffset)
				set(e.WrapModifiers.VerticalOffset, verticalOffset, wrapChanged);

			if (createChanged)
				e.OnFontCreateParametersChange();
			else if (wrapChanged)
				e.OnFontWrappingParametersChange();
			changed |= createChanged || wrapChanged;
		}

		font.OnElementChange();
		return changed;
	}
}

bool FontChanger::FontsFromSystemFont::CanAutoFit(const Structs::FontElement& element) {
	using enum Structs::ElementSourceEnum;
	if (element.Source != DirectWrite && element.Source != FreeType)
		return false;
	if (element.GlyphMerging.IsEnabled())
		return false;
	return !CodepointRanges::Subtract(element.WrapModifiers.Codepoints, PrivateUseCodepoints).empty();
}

FontChanger::FontsFromSystemFont::AutoFitResult FontChanger::FontsFromSystemFont::AutoFit(
	Structs::Font& font, std::string_view family, float size, const std::vector<Structs::FontElement*>& targets) {
	const auto traits = Traits::Of(family);
	const auto nominal = GetGameFontFamilyAndSize(font.Name);
	const auto placement = Placement::Of(traits, size, nominal ? nominal->second : size);
	FontCache open;
	const auto gameFontHolder = GetGameFont(font);
	const auto gameFont = gameFontHolder.get();

	// Each script group is sized apart, so an element drawing more than one is replaced in place by one for each.
	AutoFitResult res;
	for (size_t i = 0; i < font.Elements.size(); i++) {
		if (std::ranges::find(targets, font.Elements[i].get()) == targets.end() || !CanAutoFit(*font.Elements[i]))
			continue;
		const auto face = open(font.Elements[i]->Lookup);
		if (!face)
			continue;

		auto parts = SplitByScriptGroup(*face, font.Elements[i]->WrapModifiers.Codepoints, gameFont, traits.HasText);
		if (parts.size() == 1) {
			res.Fitted.emplace_back(font.Elements[i].get(), parts.front().first);
			continue;
		}

		const auto original = std::move(font.Elements[i]);
		res.Replaced.insert(original.get());
		font.Elements.erase(font.Elements.begin() + static_cast<ptrdiff_t>(i));
		for (auto& [group, ranges] : parts) {
			auto& part = *font.Elements.emplace(font.Elements.begin() + static_cast<ptrdiff_t>(i++), std::make_unique<Structs::FontElement>(*original));
			part->WrapModifiers.Codepoints = std::move(ranges);
			part->OnFontWrappingParametersChange();
			res.Fitted.emplace_back(part.get(), group);
		}
		i--;
	}
	if (res.Fitted.empty())
		return res;

	for (const auto& [pElement, group] : res.Fitted) {
		pElement->Auto.Size = pElement->Auto.HorizontalOffset = pElement->Auto.VerticalOffset = true;
		pElement->Auto.RenderMode = pElement->Source == Structs::ElementSourceEnum::DirectWrite;
	}
	FitElements(font, traits, size, placement, open, gameFont, res.Fitted);
	return res;
}

std::optional<FontChanger::FontsFromSystemFont::AutoFitResult> FontChanger::FontsFromSystemFont::AutoFit(Structs::Font& font, const std::vector<Structs::FontElement*>& targets) {
	const auto familyAndSize = GetGameFontFamilyAndSize(font.Name);
	const auto size = GetGameFontSize(font);
	if (!familyAndSize || !size)
		return std::nullopt;
	return AutoFit(font, familyAndSize->first, *size, targets);
}

bool FontChanger::FontsFromSystemFont::RefitAuto(Structs::Font& font) {
	if (std::ranges::none_of(font.Elements, [](const auto& e) { return e->Auto.Any() && CanAutoFit(*e); }))
		return false;
	const auto familyAndSize = GetGameFontFamilyAndSize(font.Name);
	const auto size = GetGameFontSize(font);
	if (!familyAndSize || !size)
		return false;

	const auto traits = Traits::Of(familyAndSize->first);
	const auto placement = Placement::Of(traits, *size, familyAndSize->second);
	FontCache open;
	const auto gameFontHolder = GetGameFont(font);

	// An element drawing more than one script group is fitted as the rest, as elements aren't split here.
	std::vector<std::pair<FontElement*, ScriptGroup>> fitted;
	for (const auto& element : font.Elements) {
		if (!element->Auto.Any() || !CanAutoFit(*element))
			continue;
		const auto face = open(element->Lookup);
		if (!face)
			continue;
		const auto parts = SplitByScriptGroup(*face, element->WrapModifiers.Codepoints, gameFontHolder.get(), traits.HasText);
		fitted.emplace_back(element.get(), parts.size() == 1 ? parts.front().first : ScriptGroup::NonCjk);
	}
	return !fitted.empty() && FitElements(font, traits, *size, placement, open, gameFontHolder.get(), fitted);
}
