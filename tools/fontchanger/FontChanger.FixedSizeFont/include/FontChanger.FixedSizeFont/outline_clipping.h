#ifndef XIVRES_FONTGENERATOR_OUTLINECLIPPING_H_
#define XIVRES_FONTGENERATOR_OUTLINECLIPPING_H_

// Combines outlines with Clipper2, which works on polygons; curves are flattened into lines first.

#include <algorithm>
#include <cmath>

#include <clipper2/clipper.h>

#include "fixed_size_font.h"

namespace FontChanger::FixedSizeFont::outline_clipping {
	// Decimal places that the coordinates in pixels are kept at while combining outlines.
	constexpr int CombinePrecision = 4;

	// Flattened curves stray from the outlines by up to an em divided by this.
	constexpr float FlatteningUnitsPerEm = 4000.f;

	// Flattens the contours of an outline into polygons, splitting curves into lines that stray from them by up to tolerance.
	inline Clipper2Lib::PathsD flatten(const glyph_outline& outline, float tolerance) {
		Clipper2Lib::PathsD res;
		glyph_outline::point cur{};
		const auto add = [&](glyph_outline::point p) {
			if (res.empty())
				res.emplace_back();
			res.back().emplace_back(p.X, p.Y);
			cur = p;
		};

		// Lines through n evenly spaced points of a curve stray from it by up to the largest second derivative / (8 n^2).
		const auto segmentCount = [tolerance](float ddx, float ddy) {
			return std::clamp(static_cast<int>(std::ceil(std::sqrt(std::hypot(ddx, ddy) / (8 * tolerance)))), 1, 256);
		};

		size_t i = 0;
		for (const auto verb : outline.Verbs) {
			switch (verb) {
				case glyph_outline::verb::MoveTo:
					res.emplace_back();
					add(outline.Points[i++]);
					break;

				case glyph_outline::verb::LineTo:
					add(outline.Points[i++]);
					break;

				case glyph_outline::verb::QuadTo: {
					const auto p0 = cur, c = outline.Points[i], p1 = outline.Points[i + 1];
					i += 2;
					const auto n = segmentCount(2 * (p0.X - 2 * c.X + p1.X), 2 * (p0.Y - 2 * c.Y + p1.Y));
					for (int k = 1; k <= n; k++) {
						const auto t = static_cast<float>(k) / static_cast<float>(n), u = 1 - t;
						add({u * u * p0.X + 2 * u * t * c.X + t * t * p1.X, u * u * p0.Y + 2 * u * t * c.Y + t * t * p1.Y});
					}
					break;
				}

				case glyph_outline::verb::CubicTo: {
					const auto p0 = cur, c1 = outline.Points[i], c2 = outline.Points[i + 1], p1 = outline.Points[i + 2];
					i += 3;
					const auto n = (std::max)(
						segmentCount(6 * (p0.X - 2 * c1.X + c2.X), 6 * (p0.Y - 2 * c1.Y + c2.Y)),
						segmentCount(6 * (c1.X - 2 * c2.X + p1.X), 6 * (c1.Y - 2 * c2.Y + p1.Y)));
					for (int k = 1; k <= n; k++) {
						const auto t = static_cast<float>(k) / static_cast<float>(n), u = 1 - t;
						const auto a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
						add({a * p0.X + b * c1.X + c * c2.X + d * p1.X, a * p0.Y + b * c1.Y + c * c2.Y + d * p1.Y});
					}
					break;
				}

				case glyph_outline::verb::Close:
					break;
			}
		}
		return res;
	}

	// Flattens an outline into polygons that do not overlap, so that they fill alike by any fill rule.
	inline Clipper2Lib::PathsD flatten_to_regions(const glyph_outline& outline, float tolerance) {
		return Clipper2Lib::Union(flatten(outline, tolerance), outline.EvenOdd ? Clipper2Lib::FillRule::EvenOdd : Clipper2Lib::FillRule::NonZero, CombinePrecision);
	}

	template<typename TPaths>
	glyph_outline polygons_to_outline(const TPaths& polygons) {
		glyph_outline res;
		for (const auto& polygon : polygons) {
			if (polygon.size() < 3)
				continue;
			res.move_to({static_cast<float>(polygon[0].x), static_cast<float>(polygon[0].y)});
			for (size_t i = 1; i < polygon.size(); i++)
				res.line_to({static_cast<float>(polygon[i].x), static_cast<float>(polygon[i].y)});
			res.close();
		}
		return res;
	}
}

#endif
