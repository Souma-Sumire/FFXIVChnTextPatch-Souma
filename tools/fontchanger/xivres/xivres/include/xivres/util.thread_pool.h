#ifndef XIVRES_INTERNAL_THREADPOOL_H_
#define XIVRES_INTERNAL_THREADPOOL_H_

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <optional>
#include <queue>
#include <ranges>
#include <thread>
#include <type_traits>

#include "util.listener_manager.h"
#include "util.on_dtor.h"

namespace xivres::util::thread_pool {
	class pool;

	class cancelled_error : public std::runtime_error {
	public:
		cancelled_error() : runtime_error("Execution cancelled.") {}
	};

	struct invoke_path_type {
		static constexpr size_t StackSize = 7;
		uint16_t Stack[StackSize]{};
		uint16_t Depth{};

		friend auto operator<=>(const invoke_path_type&, const invoke_path_type&) = default;
	};

	class base_task {
		friend class pool;

	protected:
		invoke_path_type m_invokePath;
		pool& m_pool;
		bool m_bCancelled;

	public:
		base_task(pool& pool)
			: m_pool(pool)
			, m_bCancelled(false) {
		}

		base_task(base_task&&) = delete;
		base_task(const base_task&) = delete;
		base_task& operator=(base_task&&) = delete;
		base_task& operator=(const base_task&) = delete;

		virtual ~base_task() = default;

		virtual void operator()() = 0;

		void cancel() { m_bCancelled = true; }

		[[nodiscard]] virtual bool finished() const = 0;

		[[nodiscard]] bool cancelled() const { return m_bCancelled; }

		void throw_if_cancelled() const {
			if (cancelled())
				throw cancelled_error();
		}

		bool operator<(const base_task& r) const;

	protected:
		template<typename TFn>
		decltype(std::declval<TFn>()()) release_working_status(const TFn& fn) const;
	};

	template<typename R>
	class task : public base_task {
		std::packaged_task<R(task&)> m_task;
		std::future<R> m_future;

	public:
		task(pool& pool, std::function<R(task&)> fn)
			: base_task(pool)
			, m_task(std::move(fn))
			, m_future(m_task.get_future()) {
		}

		void operator()() override {
			m_task(*this);
		}

		[[nodiscard]] bool finished() const override {
			return m_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
		}

		void wait() const {
			return release_working_status([&] { return m_future.wait(); });
		}

		template <class Rep, class Per>
		std::future_status wait_for(const std::chrono::duration<Rep, Per>& relTime) const {
			return release_working_status([&] { return m_future.wait_for(relTime); });
		}

		template <class Clock, class Dur>
		std::future_status wait_until(const std::chrono::time_point<Clock, Dur>& absTime) const {
			return release_working_status([&] { return m_future.wait_until(absTime); });
		}

		[[nodiscard]] R& get() {
			return m_future.get();
		}
	};

	template<>
	class task<void> : public base_task {
		std::packaged_task<void(task&)> m_task;
		std::future<void> m_future;

	public:
		task(pool& pool, std::function<void(task&)> fn)
			: base_task(pool)
			, m_task(std::move(fn))
			, m_future(m_task.get_future()) {
		}

		void operator()() override {
			m_task(*this);
		}

		[[nodiscard]] bool finished() const override {
			return m_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
		}

		void wait() const {
			return release_working_status([&] { return m_future.wait(); });
		}

		template <class Rep, class Per>
		std::future_status wait_for(const std::chrono::duration<Rep, Per>& relTime) const {
			return release_working_status([&] { return m_future.wait_for(relTime); });
		}

		template <class Clock, class Dur>
		std::future_status wait_until(const std::chrono::time_point<Clock, Dur>& absTime) const {
			return release_working_status([&] { return m_future.wait_until(absTime); });
		}

		void get() {
			m_future.get();
		}
	};

	template<typename T>
	class object_pool {
		std::mutex m_mutex;
		std::vector<std::unique_ptr<T>> m_objects;
		std::function<bool(size_t, T&)> m_fnKeepCheck;

	public:
		class scoped_pooled_object {
			object_pool* m_parent;
			std::unique_ptr<T> m_object;

			friend class object_pool;

			scoped_pooled_object(object_pool* parent)
				: m_parent(parent) {

				if (m_parent->m_objects.empty())
					return;

				const auto lock = std::scoped_lock(m_parent->m_mutex);
				if (m_parent->m_objects.empty())
					return;

				m_object = std::move(m_parent->m_objects.back());
				m_parent->m_objects.pop_back();
			}

		public:
			scoped_pooled_object() : m_parent(nullptr) {}

			scoped_pooled_object(scoped_pooled_object&& r) noexcept
				: m_parent(r.m_parent)
				, m_object(std::move(r.m_object)) {
				r.m_parent = nullptr;
				r.m_object.reset();
			}

			scoped_pooled_object& operator=(scoped_pooled_object&& r) noexcept {
				if (this == &r)
					return *this;

				if (m_object && m_parent) {
					const auto lock = std::scoped_lock(m_parent->m_mutex);
					if (!m_parent->m_fnKeepCheck || m_parent->m_fnKeepCheck(m_parent->m_objects.size(), *m_object))
						m_parent->m_objects.emplace_back(std::move(m_object));
				}

				m_parent = r.m_parent;
				m_object = std::move(r.m_object);

				r.m_parent = nullptr;
				r.m_object.reset();

				return *this;
			}

			scoped_pooled_object(const scoped_pooled_object&) = delete;
			scoped_pooled_object& operator=(const scoped_pooled_object&) = delete;

			~scoped_pooled_object() {
				if (m_object && m_parent) {
					const auto lock = std::scoped_lock(m_parent->m_mutex);
					if (!m_parent->m_fnKeepCheck || m_parent->m_fnKeepCheck(m_parent->m_objects.size(), *m_object))
						m_parent->m_objects.emplace_back(std::move(m_object));
				}
			}

			operator bool() const {
				return !!m_object;
			}

			template<typename...TArgs>
			T& emplace(TArgs&&...args) {
				m_object = std::make_unique<T>(std::forward<TArgs>(args)...);
				return *m_object;
			}

			T& operator*() const {
				return *m_object;
			}

			T* operator->() const {
				return m_object.get();
			}
		};

		object_pool(std::function<bool(size_t, T&)> keepCheck = {})
			: m_fnKeepCheck(std::move(keepCheck)) {
		}
		object_pool(object_pool&&) = delete;
		object_pool(const object_pool&) = delete;
		object_pool& operator=(object_pool&&) = delete;
		object_pool& operator=(const object_pool&) = delete;
		~object_pool() = default;

		scoped_pooled_object operator*() {
			return { this };
		}
	};

	object_pool<std::vector<uint8_t>>::scoped_pooled_object pooled_byte_buffer();

	struct untyped_task_shared_ptr_comparator {
		bool operator()(const std::shared_ptr<base_task>& l, const std::shared_ptr<base_task>& r) const {
			return *l < *r;
		}
	};

	class pool {
		friend class base_task;

		struct thread_info {
			std::thread Thread;
			std::shared_ptr<base_task> Task;
		};
		std::map<std::thread::id, thread_info> m_mapThreads;
		std::shared_ptr<std::shared_mutex> m_pmtxThread;
		std::condition_variable_any m_cvThread;
		size_t m_nConcurrency;
		std::atomic_size_t m_nWaitingThreads;
		size_t m_nFreeThreads;
		bool m_bQuitting;

		std::chrono::nanoseconds m_durMaxThreadInactivity{ 5000000000LL };  // 5 seconds

		uint64_t m_nTaskCounter = 0;
		std::priority_queue<std::shared_ptr<base_task>, std::vector<std::shared_ptr<base_task>>, untyped_task_shared_ptr_comparator> m_pqTasks;
		std::shared_ptr<std::mutex> m_pmtxTask;
		std::condition_variable m_cvTask;

	public:
		pool(size_t nConcurrentExecutions = (std::numeric_limits<size_t>::max)());

		pool(pool&&) = delete;
		pool(const pool&) = delete;
		pool& operator=(pool&&) = delete;
		pool& operator=(const pool&) = delete;

		~pool();

		static pool& instance();

		static pool& current();
		
		static void throw_if_current_task_cancelled();

		base_task* current_task() const;

		template<class Rep, class Period>
		void max_thread_inactivity(std::chrono::duration<Rep, Period> dur) {
			m_durMaxThreadInactivity = dur;
			m_cvTask.notify_all();
		}

		std::chrono::nanoseconds max_thread_inactivity() const {
			return m_durMaxThreadInactivity;
		}

		void concurrency(size_t newConcurrency);

		[[nodiscard]] size_t concurrency() const;

		template<typename TReturn = void>
		std::shared_ptr<task<TReturn>> submit(std::function<TReturn(task<TReturn>&)> fn) {
			std::unique_lock lock(*m_pmtxTask);

			auto pTask = std::make_shared<task<TReturn>>(*this, std::move(fn));

			invoke_path_type& invokePath = pTask->m_invokePath;
			if (const auto it = m_mapThreads.find(std::this_thread::get_id()); it != m_mapThreads.end())
				invokePath = it->second.Task->m_invokePath;
			invokePath.Stack[invokePath.Depth] = static_cast<uint16_t>(m_nTaskCounter++);
			if (invokePath.Depth < invoke_path_type::StackSize)
				++invokePath.Depth;

			m_pqTasks.emplace(pTask);
			lock.unlock();

			dispatch_task_to_worker();
			return pTask;
		}

		[[nodiscard]] on_dtor release_working_status();

		template<typename TFn>
		// ReSharper disable once CppNotAllPathsReturnValue
		decltype(std::declval<TFn>()()) release_working_status(const TFn& fn) {
			if (!current_task())
				return fn();

			m_nWaitingThreads += 1;
			dispatch_task_to_worker();
			if constexpr (std::is_same_v<decltype(fn()), void>) {
				fn();
				m_nWaitingThreads -= 1;
			} else {
				auto res = fn();
				m_nWaitingThreads -= 1;
				return res;
			}
		}

	private:
		void worker_body();

		void dispatch_task_to_worker();

		bool take_task(std::shared_ptr<base_task>& pTask) {
			if (m_mapThreads.size() - m_nWaitingThreads >= m_nConcurrency || m_pqTasks.empty())
				return false;

			pTask = std::move(const_cast<std::shared_ptr<base_task>&>(m_pqTasks.top()));
			m_pqTasks.pop();
			return true;
		}
	};

	template<typename TFn>
	decltype(std::declval<TFn>()()) base_task::release_working_status(const TFn& fn) const {
		return m_pool.release_working_status(fn);
	}

	template<typename TReturn = void>
	class task_waiter {
		using TPackagedTask = task<void>;

		pool& m_pool;
		std::mutex m_mtx;
		std::map<void*, std::shared_ptr<TPackagedTask>> m_mapPending;

		std::deque<std::future<TReturn>> m_dqFinished;
		std::condition_variable m_cvFinished;

	public:
		task_waiter(pool& pool = pool::current())
			: m_pool(pool) {
		}

		task_waiter(task_waiter&&) = delete;
		task_waiter(const task_waiter&) = delete;
		task_waiter& operator=(task_waiter&&) = delete;
		task_waiter& operator=(const task_waiter&) = delete;

		~task_waiter() {
			if (m_mapPending.empty())
				return;

			std::unique_lock lock(m_mtx);
			for (const auto& task : m_mapPending | std::views::values)
				task->cancel();

			m_pool.release_working_status([&] { m_cvFinished.wait(lock, [this] { return m_mapPending.empty(); }); });
		}

		[[nodiscard]] size_t pending() {
			std::scoped_lock lock(m_mtx);
			return m_mapPending.size();
		}

		[[nodiscard]] pool& pool() const {
			return m_pool;
		}

		void submit(std::function<TReturn(base_task&)> fn) {
			std::scoped_lock lock(m_mtx);
			auto newTask = m_pool.submit<void>([this, fn = std::move(fn)](task<void>& currentTask) {
				std::packaged_task<TReturn(base_task&)> task(fn);
				task(currentTask);

				std::scoped_lock innerLock(m_mtx);
				m_mapPending.erase(&currentTask);
				m_dqFinished.emplace_back(task.get_future());
				m_cvFinished.notify_one();
			});
			m_mapPending.emplace(newTask.get(), std::move(newTask));
		}

		[[nodiscard]] auto get() {
			if (m_mapPending.empty() && m_dqFinished.empty()) {
				if constexpr (std::is_void_v<TReturn>)
					return false;
				else
					return std::optional<TReturn>();
			}

			std::unique_lock lock(m_mtx);
			if (m_mapPending.empty() && m_dqFinished.empty()) {
				if constexpr (std::is_void_v<TReturn>)
					return false;
				else
					return std::optional<TReturn>();
			}

			m_pool.release_working_status([&] { m_cvFinished.wait(lock, [this] { return !m_dqFinished.empty(); }); });
			auto obj = std::move(m_dqFinished.front());
			m_dqFinished.pop_front();
			lock.unlock();
			
			if constexpr (std::is_void_v<TReturn>)
				return true;
			else
				return std::optional<TReturn>(obj.get());
		}

		template<class Rep, class Period>
		[[nodiscard]] auto get(const std::chrono::duration<Rep, Period>& waitDuration) requires (!std::is_void_v<TReturn>) {
			if (m_mapPending.empty() && m_dqFinished.empty()) {
				if constexpr (std::is_void_v<TReturn>)
					return false;
				else
					return std::optional<TReturn>();
			}

			std::unique_lock lock(m_mtx);
			if (m_mapPending.empty() && m_dqFinished.empty()) {
				if constexpr (std::is_void_v<TReturn>)
					return false;
				else
					return std::optional<TReturn>();
			}

			m_pool.release_working_status([&] { m_cvFinished.wait_for(lock, waitDuration, [this] { return !m_dqFinished.empty(); }); });
			if (m_dqFinished.empty())
				return std::nullopt;
			auto obj = std::move(m_dqFinished.front());
			m_dqFinished.pop_front();
			lock.unlock();

			if constexpr (std::is_void_v<TReturn>)
				return true;
			else
				return std::optional<TReturn>(obj.get());
		}

		template <class Clock, class Duration>
		[[nodiscard]] auto get(const std::chrono::time_point<Clock, Duration>& waitUntil) requires (!std::is_void_v<TReturn>) {
			if (m_mapPending.empty() && m_dqFinished.empty()) {
				if constexpr (std::is_void_v<TReturn>)
					return false;
				else
					return std::optional<TReturn>();
			}

			std::unique_lock lock(m_mtx);
			if (m_mapPending.empty() && m_dqFinished.empty()) {
				if constexpr (std::is_void_v<TReturn>)
					return false;
				else
					return std::optional<TReturn>();
			}

			m_pool.release_working_status([&] { m_cvFinished.wait_until(lock, waitUntil, [this] { return !m_dqFinished.empty(); }); });
			if (m_dqFinished.empty()) {
				if constexpr (std::is_void_v<TReturn>)
					return false;
				else
					return std::optional<TReturn>();
			}
			auto obj = std::move(m_dqFinished.front());
			m_dqFinished.pop_front();
			lock.unlock();

			if constexpr (std::is_void_v<TReturn>)
				return true;
			else
				return std::optional<TReturn>(obj.get());
		}

		void wait_all() {
			while (get())
				void();
		}
	};
}

#endif
