/*
This thread pool is a modified version of https://github.com/progschj/ThreadPool.
It is adjusted to fit our project structure better.

Original licesne:
Copyright (c) 2012 Jakob Progsch, Václav Zeman

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

   3. This notice may not be removed or altered from any source
   distribution.
*/

#pragma once

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

#include "delegate.hpp"

namespace util
{

	class ThreadPool
	{
	public:
		ThreadPool(size_t);
		template<class F, class... Args>
		decltype(auto) Enqueue(F&& f, Args&&... args);
		~ThreadPool();

		inline void DivideWork(const std::size_t num_tasks, util::Delegate<void(std::uint64_t)> func)
		{
			auto num_threads = m_workers.size();
			const std::size_t num_tasks_per_thread = num_tasks / num_threads;
			const std::size_t num_tougher_threads = num_tasks % num_threads;

			const auto ThreadDivider = [&](unsigned int const thread_id)
			{
				int index0 = num_tasks_per_thread - (num_threads - thread_id) * num_tasks_per_thread;
				for (std::size_t index0 = (thread_id < num_tougher_threads ? thread_id * (num_tasks_per_thread + 1) : num_tasks - (num_threads - thread_id) * num_tasks_per_thread), index = index0; index < index0 + num_tasks_per_thread + (thread_id < num_tougher_threads); ++index)
				{
					func(index);
				}
			};

			for (decltype(num_threads) i = 0; i < num_threads; i++)
			{
				ThreadDivider(i);
			}
		}

	private:
		// need to keep track of threads so we can join them
		std::vector<std::thread> m_workers;
		// the task queue
		std::queue<Delegate<void()>> m_tasks;

		// synchronization
		std::mutex m_queue_mutex;
		std::condition_variable m_condition;
		bool m_stop;
	};

	// the constructor just launches some amount of workers
	inline ThreadPool::ThreadPool(std::size_t threads)
		: m_stop(false)
	{
		for (decltype(threads) i = 0; i < threads; ++i)
			m_workers.emplace_back(
				[this]
		{
			for (;;)
			{
				Delegate<void()> task;

				{
					std::unique_lock<std::mutex> lock(m_queue_mutex);

					m_condition.wait(lock,
						[this] { return m_stop || !m_tasks.empty(); });
					if (m_stop && m_tasks.empty())
						return;
					task = std::move(m_tasks.front());
					m_tasks.pop();
				}

				task();
			}
		}
		);
	}

	// add new work item to the pool
	template<class F, class... Args>
	decltype(auto) ThreadPool::Enqueue(F&& f, Args&&... args)
	{
		using return_type = typename std::result_of<F(Args...)>::type;

		auto task = std::make_shared< std::packaged_task<return_type()> >(
			std::bind(std::forward<F>(f), std::forward<Args>(args)...)
			);

		std::future<return_type> res = task->get_future();
		{
			std::unique_lock<std::mutex> lock(m_queue_mutex);

			// don't allow enqueueing after stopping the pool
			if (m_stop)
				throw std::runtime_error("enqueue on stopped ThreadPool");

			m_tasks.emplace([task]() { (*task)(); });
		}
		m_condition.notify_one();
		return res;
	}

	// the destructor joins all threads
	inline ThreadPool::~ThreadPool()
	{
		{
			std::unique_lock<std::mutex> lock(m_queue_mutex);
			m_stop = true;
		}

		m_condition.notify_all();
		for (std::thread& worker : m_workers)
		{
			worker.join();
		}
	}

}