#ifndef XIVRES_PIXELFORMATS_H_
#define XIVRES_PIXELFORMATS_H_

#include <cinttypes>
#include <cmath>

#include "util.h"

namespace xivres::util {
	union float_components {
		float Value;
		uint32_t UintValue;

		struct {
			uint32_t Sign : 1;
			uint32_t Exponent : 8;
			uint32_t Mantissa : 23;
		} Bits;

		struct {
			uint32_t Sign : 1;
			uint32_t Exponent : 8;
			uint32_t Mantissa : 10;
			uint32_t MantissaPad : 13;
		} HalfCompatibleBits;

		constexpr float_components(float f) : Value(f) {}

		constexpr float_components(uint32_t sign, uint32_t exponent, uint32_t mantissa): Bits{
			.Sign = sign,
			.Exponent = exponent,
			.Mantissa = mantissa,
		} {}

		constexpr float_components(uint32_t sign, uint32_t exponent, uint32_t mantissa, uint32_t mantissaPad): HalfCompatibleBits{
			.Sign = sign,
			.Exponent = exponent,
			.Mantissa = mantissa,
			.MantissaPad = mantissaPad,
		} {}
	};

	union half {
		uint16_t UintValue;

		struct {
			uint16_t Sign : 1;
			uint16_t Exponent : 5;
			uint16_t Mantissa : 10;
		} Bits;

		constexpr half() : UintValue(0) {}

		constexpr half(uint16_t sign, uint16_t exponent, uint16_t mantissa): Bits{
			.Sign = sign,
			.Exponent = exponent,
			.Mantissa = mantissa,
		} {}

		constexpr half(float_components f) : half(
			static_cast<uint16_t>(f.HalfCompatibleBits.Sign),
			static_cast<uint16_t>(f.HalfCompatibleBits.Exponent + 15U - 127U),
			static_cast<uint16_t>(f.HalfCompatibleBits.Mantissa)) {}

		constexpr half(float lf) : half(float_components(lf)) {}
		constexpr half(double lf) : half(float_components(static_cast<float>(lf))) {}

		friend half operator+(half l, half r) {
			return {static_cast<float>(l) + static_cast<float>(r)};
		}

		friend half operator-(half l, half r) {
			return {static_cast<float>(l) - static_cast<float>(r)};
		}

		friend half operator*(half l, half r) {
			return {static_cast<float>(l) * static_cast<float>(r)};
		}

		friend half operator/(half l, half r) {
			return {static_cast<float>(l) / static_cast<float>(r)};
		}

		operator float() const {
			return float_components(
				Bits.Sign,
				Bits.Exponent - 15U + 127U,
				Bits.Mantissa,
				0
			).Value;
		}

		constexpr static half one();
	};

	constexpr half half::one() { return {0, 0b01111, 0}; }

	template<typename TElem, int RB, int GB, int BB, int AB>
	struct bgra_bitfields_base;

	template<typename TElem, TElem TElemMax>
	struct rgba_floating_point_base;

	template<typename TSelf, typename TElem, TElem TMaxR, TElem TMaxG, TElem TMaxB, TElem TMaxA>
	struct four_channel_pixel_format {
		static constexpr TElem MaxR = TMaxR;
		static constexpr TElem MaxG = TMaxG;
		static constexpr TElem MaxB = TMaxB;
		static constexpr TElem MaxA = TMaxA;

		four_channel_pixel_format() {
			memset(this, 0, sizeof(*this));
		}

		four_channel_pixel_format(TElem r, TElem g, TElem b, TElem a = MaxA) {
			set_components(r, g, b, a);
		}

		template<typename TR, typename TG, typename TB, typename TA>
			requires std::is_integral_v<TR> && std::is_integral_v<TG> && std::is_integral_v<TB> && std::is_integral_v<TA>
		void set_components(TR r, TG g, TB b, TA a = MaxA) {
			reinterpret_cast<TSelf*>(this)->R = static_cast<TElem>(r);
			reinterpret_cast<TSelf*>(this)->G = static_cast<TElem>(g);
			reinterpret_cast<TSelf*>(this)->B = static_cast<TElem>(b);
			reinterpret_cast<TSelf*>(this)->A = static_cast<TElem>(a);
		}

		template<typename TR, typename TG, typename TB, typename TA>
			requires std::is_floating_point_v<TR> && std::is_floating_point_v<TG> && std::is_floating_point_v<TB> && std::is_floating_point_v<TA>
		void set_components_normalized(TR r, TG g, TB b, TA a = 1.) {
			set_components(
				static_cast<TElem>(std::round(static_cast<TR>(MaxR) * util::clamp<TR>(r, 0, 1))),
				static_cast<TElem>(std::round(static_cast<TG>(MaxG) * util::clamp<TG>(g, 0, 1))),
				static_cast<TElem>(std::round(static_cast<TB>(MaxB) * util::clamp<TB>(b, 0, 1))),
				static_cast<TElem>(std::round(static_cast<TA>(MaxA) * util::clamp<TA>(a, 0, 1))));
		}

		template<typename TElem2, int RB, int GB, int BB, int AB>
		void set_components_from(const bgra_bitfields_base<TElem2, RB, GB, BB, AB>& r);

		template<typename TElem2, TElem2 TElemMax2>
		void set_components_from(const rgba_floating_point_base<TElem2, TElemMax2>& r);

		[[nodiscard]] double normalized_r() const {
			return static_cast<double>(reinterpret_cast<const TSelf*>(this)->R) / static_cast<double>(MaxR);
		}

		void normalized_r(double v) {
			reinterpret_cast<TSelf*>(this)->R = static_cast<TElem>(std::round(static_cast<double>(MaxR) * util::clamp<double>(v, 0, 1)));
		}

		[[nodiscard]] double normalized_g() const {
			return static_cast<double>(reinterpret_cast<const TSelf*>(this)->G) / static_cast<double>(MaxG);
		}

		void normalized_g(double v) {
			reinterpret_cast<TSelf*>(this)->G = static_cast<TElem>(std::round(static_cast<double>(MaxG) * util::clamp<double>(v, 0, 1)));
		}

		[[nodiscard]] double normalized_b() const {
			return static_cast<double>(reinterpret_cast<const TSelf*>(this)->B) / static_cast<double>(MaxB);
		}

		void normalized_b(double v) {
			reinterpret_cast<TSelf*>(this)->B = static_cast<TElem>(std::round(static_cast<double>(MaxB) * util::clamp<double>(v, 0, 1)));
		}

		[[nodiscard]] double normalized_a() const {
			return static_cast<double>(reinterpret_cast<const TSelf*>(this)->A) / static_cast<double>(MaxA);
		}

		void normalized_a(double v) {
			reinterpret_cast<TSelf*>(this)->A = static_cast<TElem>(std::round(static_cast<double>(MaxA) * util::clamp<double>(v, 0, 1)));
		}
	};

	template<typename TElem, int RB, int GB, int BB, int AB>
	struct bgra_bitfields_base : four_channel_pixel_format<
			bgra_bitfields_base<TElem, RB, GB, BB, AB>,
			TElem,
			(1 << RB) - 1,
			(1 << GB) - 1,
			(1 << BB) - 1,
			(1 << AB) - 1
		> {
		using self_type = bgra_bitfields_base;
		using base_type = four_channel_pixel_format<self_type, TElem, (1 << RB) - 1, (1 << GB) - 1, (1 << BB) - 1, (1 << AB) - 1>;

		TElem B : BB;
		TElem G : GB;
		TElem R : RB;
		TElem A : AB; // actually opacity

		bgra_bitfields_base(TElem value) {
			std::memcpy(this, &value, sizeof(*this));
		}

		using base_type::MaxR;
		using base_type::MaxG;
		using base_type::MaxB;
		using base_type::MaxA;
		using base_type::four_channel_pixel_format;
		using base_type::set_components;
		using base_type::set_components_normalized;
		using base_type::set_components_from;
	};

	template<typename TElem, TElem TElemMax>
	struct rgba_floating_point_base : four_channel_pixel_format<
			rgba_floating_point_base<TElem, TElemMax>,
			TElem,
			TElemMax,
			TElemMax,
			TElemMax,
			TElemMax
		> {
		using self_type = rgba_floating_point_base;
		using base_type = four_channel_pixel_format<self_type, TElem, TElemMax, TElemMax, TElemMax, TElemMax>;

		TElem R;
		TElem G;
		TElem B;
		TElem A;

		using base_type::MaxR;
		using base_type::MaxG;
		using base_type::MaxB;
		using base_type::MaxA;
		using base_type::four_channel_pixel_format;
		using base_type::set_components;
		using base_type::set_components_normalized;
		using base_type::set_components_from;
	};

	template<typename TSelf, typename TElem, TElem TMaxR, TElem TMaxG, TElem TMaxB, TElem TMaxA>
	template<typename TElem2, int RB, int GB, int BB, int AB>
	void four_channel_pixel_format<TSelf, TElem, TMaxR, TMaxG, TMaxB, TMaxA>::set_components_from(const bgra_bitfields_base<TElem2, RB, GB, BB, AB>& r) {
		reinterpret_cast<TSelf*>(this)->R = r.R * MaxR / std::remove_cvref_t<decltype(r)>::MaxR;
		reinterpret_cast<TSelf*>(this)->G = r.G * MaxG / std::remove_cvref_t<decltype(r)>::MaxG;
		reinterpret_cast<TSelf*>(this)->B = r.B * MaxB / std::remove_cvref_t<decltype(r)>::MaxB;
		reinterpret_cast<TSelf*>(this)->A = r.A * MaxA / std::remove_cvref_t<decltype(r)>::MaxA;
	}

	template<typename TSelf, typename TElem, TElem TMaxR, TElem TMaxG, TElem TMaxB, TElem TMaxA>
	template<typename TElem2, TElem2 TElemMax2>
	void four_channel_pixel_format<TSelf, TElem, TMaxR, TMaxG, TMaxB, TMaxA>::set_components_from(const rgba_floating_point_base<TElem2, TElemMax2>& r) {
		normalized_r(r.normalized_r());
		normalized_g(r.normalized_g());
		normalized_b(r.normalized_b());
		normalized_a(r.normalized_a());
	}

	struct b4g4r4a4 : bgra_bitfields_base<uint16_t, 4, 4, 4, 4> {
		using self_type::bgra_bitfields_base;
	};

	struct b5g5r5a1 : bgra_bitfields_base<uint16_t, 5, 5, 5, 1> {
		using self_type::bgra_bitfields_base;
	};

	struct b8g8r8a8 : bgra_bitfields_base<uint32_t, 8, 8, 8, 8> {
		using self_type::bgra_bitfields_base;
		using self_type::set_components_from;
		
		void set_components_from(const b4g4r4a4& r){
			R = r.R * 17;
			G = r.G * 17;
			B = r.B * 17;
			A = r.A * 17;
		}
		
		void set_components_from(const b5g5r5a1& r){
			R = r.R << 3 | r.R >> 2;
			G = r.G << 3 | r.G >> 2;
			B = r.B << 3 | r.B >> 2;
			A = r.A * 0xFF;
		}
	};

	struct r16g16b16a16f : rgba_floating_point_base<half, half::one()> {
		using self_type::rgba_floating_point_base;
	};

	struct r32g32b32a32f : rgba_floating_point_base<float, 1.f> {
		using self_type::rgba_floating_point_base;
	};
}

#endif
