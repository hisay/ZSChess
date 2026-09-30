#pragma once
#include <vector>
#include <thread>
#include <queue>
#include <future>
#include <functional>
#include <mutex>
#include <condition_variable>

class ThreadPool {
public:
	ThreadPool(size_t threadCount = std::thread::hardware_concurrency());
	~ThreadPool();

	// submit a task, return future
	template<class F, class... Args>
	auto enqueue(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
		using return_type = decltype(f(args...));
		auto task = std::make_shared<std::packaged_task<return_type()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
		std::future<return_type> res = task->get_future();
		{
			std::unique_lock<std::mutex> lock(m_queueMutex);
			m_tasks.emplace([task]() { (*task)(); });
		}
		m_condition.notify_one();
		return res;
	}

private:
	std::vector<std::thread> m_workers;
	std::queue<std::function<void()>> m_tasks;
	std::mutex m_queueMutex;
	std::condition_variable m_condition;
	bool m_stop = false;
};
