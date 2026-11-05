#ifndef XIVRES_UTIL_MODULERELATIVE_H_
#define XIVRES_UTIL_MODULERELATIVE_H_

#include <cstdint>
#include <format>
#include <string>
#include <type_traits>

namespace xivres::util {
	struct module_relative {
		const void* Address;

		explicit module_relative(const void* address)
			: Address(address) {
		}

		template<typename T> requires std::is_pointer_v<T>
		explicit module_relative(T address)
			: Address(reinterpret_cast<const void*>(address)) {
		}

		template<typename T> requires std::is_integral_v<T>
		explicit module_relative(T address)
			: Address(reinterpret_cast<const void*>(static_cast<uintptr_t>(address))) {
		}

		[[nodiscard]] std::string to_string() const;
	};
}

template<>
struct std::formatter<xivres::util::module_relative, char> : std::formatter<std::string> {
	template<class FormatContext>
	auto format(const xivres::util::module_relative& t, FormatContext& fc) const {
		return std::formatter<std::string>::format(t.to_string(), fc);
	}
};

#endif
