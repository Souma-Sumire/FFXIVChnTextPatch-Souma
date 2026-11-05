#include "../include/xivres/util.thread_pool.h"

xivres::util::thread_pool::pool::pool(size_t nConcurrentExecutions)
	: m_pmtxThread(std::make_shared<std::shared_mutex>())
	, m_nConcurrency(nConcurrentExecutions == (std::numeric_limits<size_t>::max)() ? std::thread::hardware_concurrency() : (std::max<size_t>)(1, nConcurrentExecutions))
	, m_nWaitingThreads(0)
	, m_nFreeThreads(0)
	, m_bQuitting(false)
	, m_pmtxTask(std::make_shared<std::mutex>()) {
}

xivres::util::thread_pool::pool::~pool() {
	m_bQuitting = true;
	m_cvTask.notify_one();

	std::unique_lock lock(*m_pmtxThread);
	while (!m_mapThreads.empty())
		m_cvThread.wait(lock);
}

xivres::util::thread_pool::pool& xivres::util::thread_pool::pool::instance() {
	static pool s_instance;
	return s_instance;
}

xivres::util::thread_pool::pool& xivres::util::thread_pool::pool::current() {
	return instance();
}

void xivres::util::thread_pool::pool::throw_if_current_task_cancelled() {
	if (const auto pTask = instance().current_task())
		pTask->throw_if_cancelled(); 
}

xivres::util::thread_pool::base_task* xivres::util::thread_pool::pool::current_task() const {
	std::shared_lock lock(*m_pmtxThread);
	if (const auto it = m_mapThreads.find(std::this_thread::get_id()); it != m_mapThreads.end())
		return it->second.Task.get();
	return nullptr;
}

void xivres::util::thread_pool::pool::concurrency(size_t newConcurrency) {
	std::scoped_lock lock(*m_pmtxTask);
	m_nConcurrency = newConcurrency;
	m_cvTask.notify_one();
}

size_t xivres::util::thread_pool::pool::concurrency() const {
	return m_nConcurrency;
}

bool xivres::util::thread_pool::base_task::operator<(const base_task& r) const {
	return m_invokePath > r.m_invokePath;
}

xivres::util::on_dtor xivres::util::thread_pool::pool::release_working_status() {
	std::scoped_lock lock(*m_pmtxTask);
	if (!m_mapThreads.contains(std::this_thread::get_id()))
		return {};

	m_nWaitingThreads += 1;
	dispatch_task_to_worker();
	return { [this] { m_nWaitingThreads -= 1; } };
}

void xivres::util::thread_pool::pool::worker_body() {
	const std::shared_ptr pmtxTask(m_pmtxTask);
	const std::shared_ptr pmtxThread(m_pmtxThread);
	std::unique_lock taskLock(*pmtxTask);
	std::unique_lock threadLock(*pmtxThread);

	const auto it = m_mapThreads.find(std::this_thread::get_id());
	threadLock.unlock();

	auto& [thread, pTask] = it->second;
	while (true) {
		if (!take_task(pTask)) {
			const auto waitFrom = std::chrono::steady_clock::now();
			m_nFreeThreads++;
			++m_nWaitingThreads;
			while (!take_task(pTask)) {
				if (m_bQuitting)
					break;
				if (m_cvTask.wait_until(taskLock, waitFrom + m_durMaxThreadInactivity) == std::cv_status::timeout)
					break;
			}
			--m_nWaitingThreads;
			m_nFreeThreads--;
			if (!pTask)
				break;
		}

		taskLock.unlock();

		dispatch_task_to_worker();
		(*pTask)();
		pTask.reset();

		taskLock.lock();
	}

	threadLock.lock();
	thread.detach();
	m_mapThreads.erase(it);
	m_cvThread.notify_one();
}

void xivres::util::thread_pool::pool::dispatch_task_to_worker() {
	if (m_pqTasks.empty())
		return;

	if (m_nFreeThreads == 0 && m_mapThreads.size() - m_nWaitingThreads < m_nConcurrency) {
		std::unique_lock lock(*m_pmtxThread);
		auto thread = std::thread(&pool::worker_body, this);
		const auto threadId = thread.get_id();
		m_mapThreads[threadId].Thread = std::move(thread);
	}

	m_cvTask.notify_one();
}

xivres::util::thread_pool::object_pool<std::vector<uint8_t>>::scoped_pooled_object xivres::util::thread_pool::pooled_byte_buffer() {
	static object_pool<std::vector<uint8_t>> s_pool([](size_t c, const std::vector<uint8_t>& buf) { return c / 2 < std::thread::hardware_concurrency() && buf.size() < 1048576; });
	return *s_pool;
}
