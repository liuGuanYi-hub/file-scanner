#pragma once

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
    explicit ThreadPool(std::size_t thread_count);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    template <typename Function, typename... Arguments>
    auto enqueue(Function&& function, Arguments&&... arguments)
        -> std::future<std::invoke_result_t<Function, Arguments...>> {
        using ReturnType = std::invoke_result_t<Function, Arguments...>;

        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<Function>(function),
                      std::forward<Arguments>(arguments)...));
        std::future<ReturnType> result = task->get_future();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) {
                throw std::runtime_error("cannot enqueue task after shutdown");
            }
            tasks_.emplace([task] {
                (*task)();
            });
        }

        condition_.notify_one();
        return result;
    }

    void shutdown() noexcept;
    std::size_t size() const noexcept;

private:
    void worker_loop();

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::queue<std::function<void()>> tasks_;
    std::vector<std::thread> workers_;
    bool stopping_ = false;
};
