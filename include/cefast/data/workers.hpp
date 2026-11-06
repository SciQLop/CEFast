#pragma once
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace cefast
{
namespace detail
{
    inline std::size_t default_threads() noexcept
    {
        const auto hardware = static_cast<std::size_t>(std::thread::hardware_concurrency());
        return std::clamp<std::size_t>(hardware, 1, 8);
    }

    // A process-wide set of persistent workers. Starting seven std::threads costs ~100 us, which
    // is most of a small file's load, so the threads outlive every parse instead. Never destroyed:
    // a detached worker must not see its mutex and condition variable go away during static
    // destruction or Python interpreter shutdown.
    class worker_pool
    {
    public:
        explicit worker_pool(std::size_t requested)
        {
            for (std::size_t index = 0; index < requested; ++index)
            {
                try
                {
                    std::thread { [this] { work(); } }.detach();
                    ++size_;
                }
                catch (const std::system_error&)
                {
                    // Fewer workers than asked is fine: the caller always runs the job too.
                    break;
                }
            }
        }

        worker_pool(const worker_pool&) = delete;
        worker_pool& operator=(const worker_pool&) = delete;

        [[nodiscard]] std::size_t size() const noexcept { return size_; }

        [[nodiscard]] bool submit(std::function<void()> job)
        {
            if (size_ == 0)
                return false;
            {
                const std::lock_guard lock { mutex_ };
                jobs_.push_back(std::move(job));
            }
            has_work_.notify_one();
            return true;
        }

    private:
        void work()
        {
            for (;;)
            {
                std::function<void()> job;
                {
                    std::unique_lock lock { mutex_ };
                    has_work_.wait(lock, [this] { return !jobs_.empty(); });
                    job = std::move(jobs_.front());
                    jobs_.pop_front();
                }
                job();
            }
        }

        std::mutex mutex_;
        std::condition_variable has_work_;
        std::deque<std::function<void()>> jobs_;
        std::size_t size_ = 0;
    };

    [[nodiscard]] inline unsigned long current_process_id() noexcept
    {
#if defined(_WIN32)
        return static_cast<unsigned long>(_getpid());
#else
        return static_cast<unsigned long>(::getpid());
#endif
    }

    [[nodiscard]] inline worker_pool* workers()
    {
        static std::mutex creation;
        static worker_pool* pool = nullptr;
        static unsigned long pool_pid = 0;

        const auto pid = current_process_id();
        const std::lock_guard lock { creation };
        if (pool != nullptr && pool_pid == pid)
            return pool;

        // First use, or first use in a forked child. The parent's pool stays allocated on purpose:
        // its threads do not exist here, and destroying a mutex a now-dead thread may have held is
        // undefined. simplify: one leaked pool per fork; pthread_atfork would remove the leak at
        // the cost of a process-global hook.
        pool = new worker_pool(default_threads() - 1);
        pool_pid = pid;
        return pool;
    }

    // Runs `job` on up to `jobs` workers and on the calling thread, returning once the caller's
    // own pass and every worker that started are done. A worker that has not picked up its copy
    // yet is left behind on purpose: by then the shared chunk counter is exhausted, so it only
    // finds nothing to do, and whatever it captures is kept alive by the shared_ptr it holds. A
    // busy or missing pool therefore degrades to the caller doing all the work, never a deadlock.
    template <typename Job>
    void run_on_workers(std::size_t jobs, Job job)
    {
        worker_pool* pool = nullptr;
        try
        {
            pool = workers();
        }
        catch (...)
        {
            pool = nullptr;
        }

        struct progress
        {
            std::atomic<std::size_t> running { 0 };
        };
        auto state = std::make_shared<progress>();
        const auto copies = pool == nullptr ? 0 : std::min(jobs, pool->size());
        for (std::size_t index = 0; index < copies; ++index)
        {
            if (!pool->submit(
                    [state, job]
                    {
                        state->running.fetch_add(1);
                        try
                        {
                            job();
                        }
                        catch (...)
                        {
                        }
                        state->running.fetch_sub(1);
                    }))
                break;
        }

        try
        {
            job();
        }
        catch (...)
        {
            while (state->running.load() != 0)
                std::this_thread::yield();
            throw;
        }
        while (state->running.load() != 0)
            std::this_thread::yield();
    }
}
}
