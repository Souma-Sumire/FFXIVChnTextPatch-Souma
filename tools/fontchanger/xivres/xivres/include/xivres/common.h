#ifndef XIVRES_COMMON_H_
#define XIVRES_COMMON_H_

#include <functional>
#include <stdexcept>

#include <nlohmann/json_fwd.hpp>

namespace xivres {
	// when used as game launch parameter, subtract by one.
	enum class game_language : uint16_t {
		Unspecified = 0,
		Japanese = 1,
		English = 2,
		German = 3,
		French = 4,
		ChineseSimplified = 5,
		ChineseTraditional = 6,
		Korean = 7,
		TraditionalChinese = 8,
	};

	void to_json(nlohmann::json& j, const game_language& value);
	void from_json(const nlohmann::json& j, game_language& newValue);
	const char* game_language_code(game_language);

	enum class game_publisher : uint16_t {
		Unspecified = 0,
		SquareEnixJapan = 1,
		SquareEnixAmerica = 2,
		SquareEnixEurope = 3,
		ShengquGames = 4,
		ActozSoft = 5,
		UserjoyGames = 6,

		ShandaGames = ShengquGames,
	};

	void to_json(nlohmann::json&, const game_publisher&);
	void from_json(const nlohmann::json&, game_publisher&);

	enum class game_release_publisher {
		Unspecified,
		SquareEnix,
		ShengquGames,
		ActozSoft,
		UserjoyGames,
	};

	void to_json(nlohmann::json&, const game_release_publisher&);
	void from_json(const nlohmann::json&, game_release_publisher&);

	static constexpr uint32_t EntryAlignment = 128;

	class bad_data_error : public std::runtime_error {
	public:
		using std::runtime_error::runtime_error;
	};

	template<typename T, typename CountT = T>
	struct align_result {
		CountT Count;
		T Value;
		T By;
		T Alloc;
		T Pad;
		T Last;

		operator T() const {
			return Alloc;
		}

		void iterate_chunks_breakable(std::function<bool(CountT, T, T)> cb, T baseOffset = 0, CountT baseIndex = 0) const {
			if (Pad == 0) {
				for (CountT i = baseIndex; i < Count; ++i)
					if (!cb(i, baseOffset + i * By, By))
						return;
			} else {
				CountT i = baseIndex;
				for (; i < Count - 1; ++i)
					if (!cb(i, baseOffset + i * By, By))
						return;
				if (i == Count - 1)
					cb(i, baseOffset + i * By, Value - i * By);
			}
		}

		void iterate_chunks(std::function<void(CountT, T, T)> cb, T baseOffset = 0, CountT baseIndex = 0) const {
			if (Pad == 0) {
				for (CountT i = baseIndex; i < Count; ++i)
					cb(i, baseOffset + i * By, By);
			} else {
				CountT i = baseIndex;
				for (; i < Count - 1; ++i)
					cb(i, baseOffset + i * By, By);
				if (i == Count - 1)
					cb(i, baseOffset + i * By, Value - i * By);
			}
		}
	};

	template<typename T, typename CountT = T>
	align_result<T, CountT> align(T value, T by = static_cast<T>(EntryAlignment)) {
		const auto count = (value + by - 1) / by;
		const auto alloc = count * by;
		const auto pad = alloc - value;
		return {
			.Count = static_cast<CountT>(count),
			.Value = value,
			.By = by,
			.Alloc = static_cast<T>(alloc),
			.Pad = static_cast<T>(pad),
			.Last = value - (count - 1) * by,
		};
	}
}

#endif
