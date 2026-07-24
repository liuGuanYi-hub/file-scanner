#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

class ThreadPool {
public:
    static constexpr std::size_t kDefaultQueueCapacity = 1024;

    explicit ThreadPool(
        std::size_t thread_count,
        std::size_t queue_capacity = kDefaultQueueCapacity);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    template <typename Function, typename... Arguments>
    auto enqueue(Function&& function, Arguments&&... arguments)
        -> std::future<std::invoke_result_t<Function, Arguments...>> {
        return enqueue_impl(nullptr,
                            std::forward<Function>(function),
                            std::forward<Arguments>(arguments)...);
    }

    template <typename Function, typename... Arguments>
    auto enqueue_with_cancel(std::atomic_bool* cancel,
                             Function&& function,
                             Arguments&&... arguments)
        -> std::future<std::invoke_result_t<Function, Arguments...>> {
        return enqueue_impl(cancel,
                            std::forward<Function>(function),
                            std::forward<Arguments>(arguments)...);
    }

    void shutdown() noexcept;
    std::size_t size() const noexcept;
    std::size_t capacity() const noexcept;
    std::size_t pending_tasks() const noexcept;

private:
    template <typename Function, typename... Arguments>
    auto enqueue_impl(std::atomic_bool* cancel,
                      Function&& function,
                      Arguments&&... arguments)
        -> std::future<std::invoke_result_t<Function, Arguments...>> {
        using ReturnType = std::invoke_result_t<Function, Arguments...>;

        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<Function>(function),
                      std::forward<Arguments>(arguments)...));
        std::future<ReturnType> result = task->get_future();

        {
            std::unique_lock<std::mutex> lock(mutex_);
            while (!stopping_ && tasks_.size() >= queue_capacity_) {
                if (cancel && cancel->load(std::memory_order_relaxed)) {
                    throw std::runtime_error("task submission cancelled");
                }
                space_condition_.wait_for(lock, std::chrono::milliseconds(50));
            }
            if (stopping_) {
                throw std::runtime_error("cannot enqueue task after shutdown");
            }
            if (cancel && cancel->load(std::memory_order_relaxed)) {
                throw std::runtime_error("task submission cancelled");
            }
            tasks_.emplace([task] {
                (*task)();
            });
        }

        condition_.notify_one();
        return result;
    }

    void worker_loop();

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::condition_variable space_condition_;
    std::queue<std::function<void()>> tasks_;
    std::vector<std::thread> workers_;
    std::size_t queue_capacity_ = kDefaultQueueCapacity;
    bool stopping_ = false;
};
