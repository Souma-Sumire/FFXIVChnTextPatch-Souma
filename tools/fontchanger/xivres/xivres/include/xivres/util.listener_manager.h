#ifndef XIVRES_INTERNAL_LISTENERMANAGER_H_
#define XIVRES_INTERNAL_LISTENERMANAGER_H_

#include <functional>
#include <map>
#include <memory>
#include <shared_mutex>

#include "util.on_dtor.h"

namespace xivres::util {
	namespace implementation_listener_manager {
		template <typename, typename, typename, typename...>
		class ListenerManagerImpl_;

		template <typename TCallbackReturn, typename ... TCallbackArgs>
		class ListenerManagerImplBase_ {
			template <typename, typename, typename, typename...>
			friend class ListenerManagerImpl_;

			typedef std::function<TCallbackReturn(TCallbackArgs ...)> CallbackType;

			size_t m_callbackId = 0;
			std::map<size_t, std::function<TCallbackReturn(TCallbackArgs ...)>> m_callbacks;
			std::map<size_t, std::atomic_size_t> m_callbackUseCounters;
			std::condition_variable_any m_cv;
			std::shared_ptr<std::shared_mutex> m_lock = std::make_shared<std::shared_mutex>();
			std::shared_ptr<bool> m_destructed = std::make_shared<bool>(false);

			const std::function<void(const CallbackType&)> m_onNewCallback;

		public:
			ListenerManagerImplBase_(std::function<void(const CallbackType&)> onNewCallback = nullptr)
				: m_onNewCallback(std::move(onNewCallback)) {
			}

			ListenerManagerImplBase_(ListenerManagerImplBase_&&) = delete;
			ListenerManagerImplBase_(const ListenerManagerImplBase_&) = delete;
			ListenerManagerImplBase_& operator=(ListenerManagerImplBase_&&) = delete;
			ListenerManagerImplBase_& operator=(const ListenerManagerImplBase_&) = delete;

			virtual ~ListenerManagerImplBase_() {
				std::scoped_lock lock(*m_lock);
				m_callbacks.clear();
				*m_destructed = true;
			}

			[[nodiscard]] bool empty() const {
				return m_callbacks.empty();
			}

			/// \brief Adds a callback function to call when an event has been fired.
			/// \returns An object that will remove the callback when destructed.
			[[nodiscard]] virtual on_dtor operator() (std::function<TCallbackReturn(TCallbackArgs ...)> fn, std::function<void()> onUnbind = {}) {
				std::scoped_lock lock(*m_lock);
				const auto callbackId = m_callbackId++;
				if (m_onNewCallback)
					m_onNewCallback(fn);
				m_callbacks.emplace(callbackId, std::move(fn));
				m_callbackUseCounters.emplace(callbackId, 0);

				return { [this, destructed = m_destructed, onUnbind = std::move(onUnbind), mutex = m_lock, callbackId] {
					std::unique_lock unbindLock(*mutex);

					if (!*destructed)
						m_callbacks.erase(callbackId);

					if (onUnbind)
						onUnbind();

					if (!*destructed) {
						m_cv.wait(unbindLock, [this, callbackId] { return m_callbackUseCounters[callbackId] == 0; });
						m_callbackUseCounters.erase(callbackId);
					}
				} };
			}

		protected:

			/// \brief Fires an event.
			/// \returns Number of callbacks called.
			virtual size_t operator() (TCallbackArgs ... t) {
				std::vector<std::function<TCallbackReturn(TCallbackArgs ...)>> callbacks;
				std::vector<size_t> callbackIds;
				callbacks.reserve(m_callbacks.size());
				callbackIds.reserve(m_callbacks.size());

				std::shared_lock lock(*m_lock);
				for (const auto& cbp : m_callbacks) {
					callbacks.push_back(cbp.second);
					callbackIds.push_back(cbp.first);
				}
				for (const auto& cid : callbackIds)
					++m_callbackUseCounters[cid];
				lock.unlock();

				const auto dtor = on_dtor([&] {
					lock.lock();
					for (const auto& cid : callbackIds)
						--m_callbackUseCounters[cid];
					lock.unlock();

					m_cv.notify_all();
				});

				size_t notified = 0;
				for (const auto& cb : callbacks) {
					cb(std::forward<TCallbackArgs>(t)...);
					notified++;
				}

				return notified;
			}
		};

		template <typename TInvoker, typename TCallbackReturn, typename ... TCallbackArgs>
			requires (!std::is_same_v<TCallbackReturn, void>)
		class ListenerManagerImpl_<TInvoker, TCallbackReturn, void, TCallbackArgs...> :
			public ListenerManagerImplBase_<TCallbackReturn, TCallbackArgs...> {

			friend TInvoker;

		public:
			using ListenerManagerImplBase_<TCallbackReturn, TCallbackArgs...>::ListenerManagerImplBase_;

			/// \brief Adds a callback function to call when an event has been fired.
			/// \returns An object that will remove the callback when destructed.
			[[nodiscard]] on_dtor operator() (std::function<TCallbackReturn(TCallbackArgs ...)> fn, std::function<void()> onUnbind = {}) override {
				return ListenerManagerImplBase_<TCallbackReturn, TCallbackArgs...>::operator()(std::move(fn), std::move(onUnbind));
			}

		protected:

			/// \brief Fires an event.
			/// \returns Number of callbacks called.
			size_t operator() (TCallbackArgs ... t) override {
				return ListenerManagerImplBase_<TCallbackReturn, TCallbackArgs...>::operator()(std::forward<TCallbackArgs>(t)...);
			}

			/// \brief Fires an event.
			/// \returns Number of callbacks called.
			size_t operator() (TCallbackArgs ... t, const std::function<bool(size_t, TCallbackReturn)>& stopNotifying) {
				std::vector<std::function<TCallbackReturn(TCallbackArgs ...)>> callbacks;
				std::vector<size_t> callbackIds;
				callbacks.reserve(this->m_callbacks.size());
				callbackIds.reserve(this->m_callbacks.size());

				std::shared_lock lock(*this->m_lock);
				for (const auto& cbp : this->m_callbacks) {
					callbacks.push_back(cbp.second);
					callbackIds.push_back(cbp.first);
					++this->m_callbackUseCounters[cbp.first];
				}
				lock.unlock();

				const auto dtor = on_dtor([&] {
					lock.lock();
					for (const auto& cid : callbackIds)
						--this->m_callbackUseCounters[cid];
					lock.unlock();

					this->m_cv.notify_all();
				});

				size_t notified = 0;
				for (const auto& cb : callbacks) {
					const auto r = cb(std::forward<TCallbackArgs>(t)...);
					if (stopNotifying && stopNotifying(notified, r))
						break;
					notified++;
				}
				return notified;
			}
		};

		template <typename TInvoker, typename TCallbackReturn, typename...TCallbackArgs>
			requires std::is_same_v<TCallbackReturn, void>
		class ListenerManagerImpl_<TInvoker, TCallbackReturn, void, TCallbackArgs...> :
			public ListenerManagerImplBase_<TCallbackReturn, TCallbackArgs...> {

			friend TInvoker;

		public:
			using ListenerManagerImplBase_<TCallbackReturn, TCallbackArgs...>::ListenerManagerImplBase_;

			/// \brief Adds a callback function to call when an event has been fired.
			/// \returns An object that will remove the callback when destructed.
			[[nodiscard]] on_dtor operator() (std::function<TCallbackReturn(TCallbackArgs ...)> fn, std::function<void()> onUnbind = {}) override {
				return ListenerManagerImplBase_<TCallbackReturn, TCallbackArgs...>::operator()(std::move(fn), std::move(onUnbind));
			}

		protected:
			/// \brief Fires an event.
			/// \returns Number of callbacks called.
			size_t operator() (TCallbackArgs ... t) override {
				return ListenerManagerImplBase_<TCallbackReturn, TCallbackArgs...>::operator()(std::forward<TCallbackArgs>(t)...);
			}
		};
	}

	/// Event callback management class.
	template <typename TInvoker, typename TCallbackReturn, typename...TCallbackArgs>
	class listener_manager : public implementation_listener_manager::ListenerManagerImpl_<TInvoker, TCallbackReturn, void, TCallbackArgs...> {
	public:
		using implementation_listener_manager::ListenerManagerImpl_<TInvoker, TCallbackReturn, void, TCallbackArgs...>::ListenerManagerImpl_;
	};

}

#endif
