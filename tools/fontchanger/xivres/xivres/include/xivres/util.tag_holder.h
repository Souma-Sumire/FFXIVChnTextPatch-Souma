#ifndef XIVRES_UTIL_TAG_HOLDER_H_
#define XIVRES_UTIL_TAG_HOLDER_H_

#include <atomic>
#include <memory>
#include <typeindex>
#include <unordered_map>

namespace xivres::util {
	class tag_set {
	public:
		using map_type = std::unordered_map<std::type_index, std::shared_ptr<const void>>;

	private:
		std::shared_ptr<const map_type> m_tags;

	public:
		tag_set() = default;
		explicit tag_set(std::shared_ptr<const map_type> tags) : m_tags(std::move(tags)) {}

		[[nodiscard]] bool empty() const { return !m_tags || m_tags->empty(); }

		template<typename T>
		[[nodiscard]] std::shared_ptr<const T> get() const {
			if (!m_tags)
				return nullptr;
			const auto it = m_tags->find(typeid(T));
			return it == m_tags->end() ? nullptr : std::static_pointer_cast<const T>(it->second);
		}
	};

	class tag_holder {
		std::atomic<std::shared_ptr<const tag_set::map_type>> m_tags;

		template<typename Fn>
		void modify(Fn&& fn) {
			auto current = m_tags.load();
			std::shared_ptr<const tag_set::map_type> next;
			do {
				auto copy = current ? std::make_shared<tag_set::map_type>(*current) : std::make_shared<tag_set::map_type>();
				fn(*copy);
				next = std::move(copy);
			} while (!m_tags.compare_exchange_weak(current, next));
		}

	public:
		tag_holder() = default;
		tag_holder(const tag_holder& r) : m_tags(r.m_tags.load()) {}
		tag_holder(tag_holder&& r) noexcept : m_tags(r.m_tags.exchange(nullptr)) {}

		tag_holder& operator=(const tag_holder& r) {
			if (this != &r)
				m_tags.store(r.m_tags.load());
			return *this;
		}

		tag_holder& operator=(tag_holder&& r) noexcept {
			if (this != &r)
				m_tags.store(r.m_tags.exchange(nullptr));
			return *this;
		}

		template<typename T>
		void set_tag(std::shared_ptr<const T> tag) {
			modify([&tag](tag_set::map_type& tags) { tags[typeid(T)] = tag; });
		}

		template<typename T, typename... Args>
		void emplace_tag(Args&&... args) {
			set_tag<T>(std::shared_ptr<const T>(std::make_shared<T>(std::forward<Args>(args)...)));
		}

		template<typename T>
		void remove_tag() {
			modify([](tag_set::map_type& tags) { tags.erase(typeid(T)); });
		}

		template<typename T>
		[[nodiscard]] std::shared_ptr<const T> get_tag() const {
			return tags().get<T>();
		}

		[[nodiscard]] tag_set tags() const {
			return tag_set(m_tags.load());
		}
	};
}

#endif
