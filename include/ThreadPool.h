#pragma once

#include <thread>
#include <atomic>
#include <deque>
#include <vector>
#include <cstddef>
#include <iostream>
#include <condition_variable>

namespace ns
{
    template <typename T>
    class ThreadPool
    {
    public:
        struct Job
        {
            typedef void (*Func)(const T&);
            Func fun;
            T data;
        };
    public:
        ThreadPool() = delete;
        ThreadPool(size_t n_threads);
        ~ThreadPool();
    public:
        bool is_idle() const;
        void wait_idle() const;
        void push_job(const Job& job);
        void push_priority_job(const Job& job);
    private:
        std::optional<Job> try_pop_job();
        void worker_func();
    private:
        std::atomic_bool m_stop_threads;
        std::deque<Job> m_jobs;
        std::vector<std::thread> m_threads;
        mutable std::mutex m_jobs_mtx;
        mutable std::condition_variable m_jobs_con_var;
        std::atomic_uint64_t m_n_jobs_running;
    };

    template <typename T>
    ThreadPool<T>::ThreadPool(size_t n_threads)
    {
        m_stop_threads = false;

        while (n_threads-- > 0)
            m_threads.push_back(std::thread(&ThreadPool::worker_func, this));
    }

    template <typename T>
    ThreadPool<T>::~ThreadPool()
    {
        m_stop_threads = true;

        m_jobs_con_var.notify_all();

        for (auto& t : m_threads)
            t.join();
    }

    template <typename T>
    bool ThreadPool<T>::is_idle() const
    {
        std::unique_lock lock(m_jobs_mtx);

        return m_jobs.empty() && m_n_jobs_running == 0;
    }


    template <typename T>
    void ThreadPool<T>::push_job(const Job& job)
    {
        std::unique_lock lock(m_jobs_mtx);

        m_jobs.push_back(job);

        m_jobs_con_var.notify_one();
    }

    template <typename T>
    void ThreadPool<T>::push_priority_job(const Job& job)
    {
        std::unique_lock lock(m_jobs_mtx);

        m_jobs.push_front(job);

        m_jobs_con_var.notify_one();
    }

    template <typename T>
    std::optional<typename ThreadPool<T>::Job> ThreadPool<T>::try_pop_job()
    {
        std::unique_lock lock(m_jobs_mtx);

        m_jobs_con_var.wait(lock, [&]{ return !m_jobs.empty() || m_stop_threads; });

        if (m_stop_threads)
            return {};

        Job job = m_jobs.front();

        ++m_n_jobs_running;

        m_jobs.pop_front();

        return job;
    }

    template <typename T>
    void ThreadPool<T>::worker_func()
    {
        while (!m_stop_threads)
        {
            auto optional_job = try_pop_job();
        
            if (!optional_job.has_value())
                continue;

            auto& job = optional_job.value();
            
            try
            {
                job.fun(job.data);
            }
            catch(const std::exception& e)
            {
                printf("[ THREAD ERR ]:\n%s\n", e.what());
            }

            {
                std::unique_lock lock(m_jobs_mtx);
                --m_n_jobs_running;
            }
        }
    }
}