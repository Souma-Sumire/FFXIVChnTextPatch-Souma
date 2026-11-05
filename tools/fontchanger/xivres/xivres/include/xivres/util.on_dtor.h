#ifndef XIVRES_INTERNAL_CALLONDESTRUCTION_H_
#define XIVRES_INTERNAL_CALLONDESTRUCTION_H_

#include <functional>

namespace xivres::util {
	/// \brief Calls a function on destruction.
	/// Used in places where finally is required (when using C-style functions)
	class on_dtor {
	protected:
		std::function<void()> m_fn;

	public:
		on_dtor() noexcept = default;
		on_dtor(const on_dtor&) = delete;
		on_dtor& operator=(const on_dtor&) = delete;

		on_dtor(std::function<void()> fn)
			: m_fn(std::move(fn)) {
		}

		on_dtor(on_dtor&& r) noexcept {
			m_fn = std::move(r.m_fn);
			r.m_fn = nullptr;
		}

		on_dtor& operator=(on_dtor&& r) noexcept {
			if (m_fn)
				m_fn();
			m_fn = std::move(r.m_fn);
			r.m_fn = nullptr;
			return *this;
		}

		on_dtor(std::nullptr_t) noexcept {
		}

		on_dtor& operator=(std::nullptr_t) noexcept {
			clear();
			return *this;
		}

		on_dtor& operator=(std::function<void()>&& fn) noexcept {
			if (m_fn)
				m_fn();
			m_fn = std::move(fn);
			return *this;
		}

		on_dtor& operator=(const std::function<void()>& fn) {
			if (m_fn)
				m_fn();
			m_fn = fn;
			return *this;
		}

		on_dtor& wrap(std::function<void(std::function<void()>)> wrapper) {
			m_fn = [fn = std::move(m_fn), wrapper = std::move(wrapper)] {
				wrapper(fn);
			};
			return *this;
		}

		virtual ~on_dtor() {
			if (m_fn)
				m_fn();
		}

		on_dtor& clear() {
			if (m_fn) {
				m_fn();
				m_fn = nullptr;
			}
			return *this;
		}

		void cancel() {
			m_fn = nullptr;
		}

		operator bool() const {
			return !!m_fn;
		}

		class multi {
			std::vector<on_dtor> m_list;

		public:
			multi() = default;
			multi(const multi&) = delete;
			multi(multi&&) = delete;
			multi& operator=(const multi&) = delete;
			multi& operator=(multi&&) = delete;

			~multi() {
				clear();
			}

			multi& operator+=(on_dtor o) {
				if (o)
					m_list.emplace_back(std::move(o));
				return *this;
			}

			multi& operator+=(std::function<void()> f) {
				m_list.emplace_back(std::move(f));
				return *this;
			}

			multi& operator+=(multi r) {
				m_list.insert(m_list.end(), std::make_move_iterator(r.m_list.begin()), std::make_move_iterator(r.m_list.end()));
				r.m_list.clear();
				return *this;
			}

			void clear() {
				while (!m_list.empty()) {
					m_list.back().clear();
					m_list.pop_back();
				}
			}
		};
	};

	template<typename T>
	class wrap_value_with_context final : public on_dtor {
		T m_value;

	public:
		wrap_value_with_context(T value, std::function<void()> fn) noexcept
			: on_dtor(std::move(fn))
			, m_value(std::move(value)) {
		}

		wrap_value_with_context() noexcept requires std::is_default_constructible_v<T>
			: on_dtor(nullptr)
			, m_value{} {
		}

		wrap_value_with_context(wrap_value_with_context&& r) noexcept requires std::is_default_constructible_v<T>
			: on_dtor(std::move(r))
			, m_value(std::move(r.m_value)) {
			if constexpr (std::is_trivial_v<T>)
				r.m_value = {};
		}

		wrap_value_with_context& operator=(wrap_value_with_context&& r) noexcept requires std::is_default_constructible_v<T> {
			m_value = std::move(r.m_value);
			on_dtor::operator=(std::move(r));
			if constexpr (std::is_trivial_v<T>)
				r.m_value = {};
			return *this;
		}

		wrap_value_with_context(std::nullptr_t) noexcept requires std::is_default_constructible_v<T>
			: on_dtor() {
		}

		wrap_value_with_context& operator=(std::nullptr_t) noexcept requires std::is_default_constructible_v<T> {
			on_dtor::operator=(nullptr);
			return *this;
		}

		~wrap_value_with_context() override = default;

		operator T& () {
			return m_value;
		}

		operator const T& () const {
			return m_value;
		}

		T& operator*() {
			return m_value;
		}

		const T& operator*() const {
			return m_value;
		}
	};
}

#endif
