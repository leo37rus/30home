#pragma once
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>

class ThreadPool;

typedef std::packaged_task<void()> task_type;
typedef std::future<void> res_type;
typedef void (*FuncType) (std::vector<int>&, int, int, bool, ThreadPool&, int);

template<typename T>
class BlockedQueue
{
public:
	void push(T& item)
	{
		std::lock_guard<std::mutex> l(m_locker);
		m_task_queue.push(std::move(item));
		m_notifier.notify_one();
	}

	void pop(T& item)
	{
		std::unique_lock<std::mutex> l(m_locker);
		if (m_task_queue.empty())
		{
			m_notifier.wait(l, [this]() {return !m_task_queue.empty(); });
		}
		item = std::move(m_task_queue.front());
		m_task_queue.pop();
	}

	bool fast_pop(T& item)
	{
		std::lock_guard<std::mutex> l(m_locker);
		if (m_task_queue.empty())
		{
			return false;
		}
		item = std::move(m_task_queue.front());
		m_task_queue.pop();
		return true;
	}
private:
	std::queue<T> m_task_queue;
	std::mutex m_locker;
	std::condition_variable m_notifier;
};

class ThreadPool
{
public:
	ThreadPool();
	~ThreadPool();
	void start();
	void stop();
	res_type push_task(FuncType f, std::vector<int>& arr, int l, int r, bool enable, ThreadPool& tp, int multi_size);
	void thread_func(int qindex);
	void run_pending_task();
private:
	int m_thread_count;
	std::vector<std::thread> m_threads;
	std::vector<BlockedQueue<task_type> > m_queues;
	int m_index;
};

ThreadPool::ThreadPool() : m_thread_count(std::thread::hardware_concurrency() != 0 ? std::thread::hardware_concurrency() : 2),
m_queues(m_thread_count),
m_index(0)
{
	start();
}

ThreadPool::~ThreadPool()
{
	stop();
}

void ThreadPool::start()
{
	for (int i = 0; i < m_thread_count; ++i)
	{
		m_threads.emplace_back(&ThreadPool::thread_func, this, i);
	}
}

void ThreadPool::thread_func(int qindex)
{
	while (1)
	{
		task_type task_to_do;
		bool res;
		int i = 0;
		for (; i < m_thread_count; ++i)
		{
			res = m_queues[(qindex + i) % m_thread_count].fast_pop(task_to_do);
			if (res) break;
		}

		if (!res)
		{
			m_queues[qindex].pop(task_to_do);
		}
		else if (!task_to_do.valid())
		{
			m_queues[(qindex + i) % m_thread_count].push(task_to_do);
		}

		if (!task_to_do.valid()) return;

		task_to_do();
	}
}

void ThreadPool::stop()
{
	for (int i = 0; i < m_thread_count; ++i)
	{
		task_type empty_task;
		m_queues[i].push(empty_task);
	}

	for (auto& t : m_threads)
	{
		if (t.joinable()) t.join();
	}
}

res_type ThreadPool::push_task(FuncType f, std::vector<int>& arr, int l, int r, bool enable, ThreadPool& tp, int multi_size)
{
	int queue_to_push = m_index++ % m_thread_count;
	task_type task([=, &arr, &tp]() { f(arr, l, r, enable, tp, multi_size); });
	auto res = task.get_future();
	m_queues[queue_to_push].push(task);
	return res;
}

void ThreadPool::run_pending_task()
{
	task_type task_to_do;
	bool res;
	int i = 0;
	for (; i < m_thread_count; ++i)	//ПАНИЧЕСКИ ПЫТАЕМСЯ ПОЛУЧИТЬ ХОТЬ КАКУЮ-ТО ЗАДАЧУ
	{
		res = m_queues[i % m_thread_count].fast_pop(task_to_do);
		if (res) break;
	}

	if (!task_to_do.valid())	//Я вообще не уверен, насколько правильно так делать. Не могу пока до конца продумать все варианты, когда это можнт вызвать проблемы
	{							//Подсмотрел это решение я у Andrew Williams в книге про многопоточность, 2я редакция (где С++ 17го стандарта)
		std::this_thread::yield();
	}
	else
	{
		task_to_do();
	}
}
