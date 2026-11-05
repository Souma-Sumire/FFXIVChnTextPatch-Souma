#ifndef XIVRES_INTERNAL_BYTEORDER_H_
#define XIVRES_INTERNAL_BYTEORDER_H_

#include <algorithm>
#include <type_traits>

namespace xivres::util {
	template<typename T>
	union byte_order_storage {
		T Value;
		char Bytes[sizeof(T)];

		static byte_order_storage from_without_swap(T value) {
			return { .Value = value };
		}

		static T to_without_swap(byte_order_storage storage) {
			return storage.Value;
		}

		static byte_order_storage from_with_swap(T value);

		static T to_with_swap(byte_order_storage storage);
	};

	template<typename T>
	byte_order_storage<T> byte_order_storage<T>::from_with_swap(T value) {
		if constexpr (std::is_same_v<T, uint8_t> || std::is_same_v<T, int8_t>)
			return { .Value = value };

		else if constexpr (std::is_same_v<T, uint16_t> || std::is_same_v<T, int16_t>)
			return { .Value = static_cast<T>(_byteswap_ushort(static_cast<uint16_t>(value))) };

		else if constexpr (std::is_same_v<T, uint32_t> || std::is_same_v<T, int32_t>)
			return { .Value = static_cast<T>(_byteswap_ulong(static_cast<uint32_t>(value))) };

		else if constexpr (std::is_same_v<T, uint64_t> || std::is_same_v<T, int64_t>)
			return { .Value = static_cast<T>(_byteswap_uint64(static_cast<uint64_t>(value))) };

		else {
			byte_order_storage storage{ .Value = value };
			std::reverse(storage.Bytes, storage.Bytes + sizeof(T));
			return storage;
		}
	}

	template<typename T>
	T byte_order_storage<T>::to_with_swap(byte_order_storage storage) {
		if constexpr (std::is_same_v<T, uint8_t> || std::is_same_v<T, int8_t>)
			return storage.Value;

		else if constexpr (std::is_same_v<T, uint16_t> || std::is_same_v<T, int16_t>)
			return static_cast<T>(_byteswap_ushort(static_cast<uint16_t>(storage.Value)));

		else if constexpr (std::is_same_v<T, uint32_t> || std::is_same_v<T, int32_t>)
			return static_cast<T>(_byteswap_ulong(static_cast<uint32_t>(storage.Value)));

		else if constexpr (std::is_same_v<T, uint64_t> || std::is_same_v<T, int64_t>)
			return static_cast<T>(_byteswap_uint64(static_cast<uint64_t>(storage.Value)));

		else {
			std::reverse(storage.Bytes, storage.Bytes + sizeof(T));
			return storage.Value;
		}
	}

	template<typename T, byte_order_storage<T> FromNative(T), T ToNative(byte_order_storage<T>)>
	union byte_order_base {
		using Type = T;

		byte_order_storage<T> Storage;

		byte_order_base(T defaultValue = static_cast<T>(0))
			: Storage(FromNative(defaultValue)) {
		}

		byte_order_base& operator=(T newValue) {
			Storage = FromNative(newValue);
			return *this;
		}

		byte_order_base& operator+=(T newValue) {
			Storage = FromNative(ToNative(Storage) + newValue);
			return *this;
		}

		byte_order_base& operator-=(T newValue) {
			Storage = FromNative(ToNative(Storage) - newValue);
			return *this;
		}

		byte_order_base& operator*=(T newValue) {
			Storage = FromNative(ToNative(Storage) * newValue);
			return *this;
		}

		byte_order_base& operator/=(T newValue) {
			Storage = FromNative(ToNative(Storage) / newValue);
			return *this;
		}

		byte_order_base& operator%=(T newValue) {
			Storage = FromNative(ToNative(Storage) % newValue);
			return *this;
		}

		byte_order_base& operator&=(T newValue) {
			Storage = FromNative(ToNative(Storage) & newValue);
			return *this;
		}

		byte_order_base& operator|=(T newValue) {
			Storage = FromNative(ToNative(Storage) | newValue);
			return *this;
		}

		byte_order_base& operator^=(T newValue) {
			Storage = FromNative(ToNative(Storage) ^ newValue);
			return *this;
		}

		byte_order_base& operator<<=(T newValue) {
			Storage = FromNative(ToNative(Storage) << newValue);
			return *this;
		}

		byte_order_base& operator>>=(T newValue) {
			Storage = FromNative(ToNative(Storage) >> newValue);
			return *this;
		}

		byte_order_base& operator++() {
			Storage = FromNative(ToNative(Storage) + 1);
			return *this;
		}

		byte_order_base& operator--() {
			Storage = FromNative(ToNative(Storage) - 1);
			return *this;
		}

		T operator++(int) {
			const auto v = ToNative(Storage);
			Storage = FromNative(v + 1);
			return v;
		}

		T operator--(int) {
			const auto v = ToNative(Storage);
			Storage = FromNative(v - 1);
			return v;
		}

		T operator+() const {
			return ToNative(Storage);
		}

		T operator-() const {
			return -ToNative(Storage);
		}

		T operator~() const {
			return ~ToNative(Storage);
		}

		T operator!() const {
			return !ToNative(Storage);
		}

		operator T() const {
			return ToNative(Storage);
		}

		T operator *() const {
			return ToNative(Storage);
		}
	};
}

namespace xivres {
	template<typename T>
	using LE = util::byte_order_base<T, util::byte_order_storage<T>::from_without_swap, util::byte_order_storage<T>::to_without_swap>;
	template<typename T>
	using BE = util::byte_order_base<T, util::byte_order_storage<T>::from_with_swap, util::byte_order_storage<T>::to_with_swap>;
	template<typename T>
	using NE = LE<T>;
	template<typename T>
	using RNE = BE<T>;
}

#endif
