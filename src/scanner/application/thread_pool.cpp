#include "scanner/application/thread_pool.h"

#include <stdexcept>

namespace scanner {
namespace application {

ThreadPool::ThreadPool(std::size_t thread_count, std::size_t queue_capacity)
    : queue_capacity_(queue_capacity) {
    if (thread_count == 0) {
        throw std::invalid_argument("thread pool requires at least one worker");
    }
    if (queue_capacity == 0) {
        throw std::invalid_argument(
            "thread pool queue capacity must be positive");
    }

    workers_.reserve(thread_count);
    try {
        for (std::size_t i = 0; i < thread_count; ++i) {
            workers_.emplace_back(&ThreadPool::worker_loop, this);
        }
    } catch (...) {
        shutdown();
        throw;
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::shutdown() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            return;
        }
        stopping_ = true;
    }

    condition_.notify_all();
    space_condition_.notify_all();
    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

std::size_t ThreadPool::size() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return workers_.size();
}

std::size_t ThreadPool::capacity() const noexcept {
    return queue_capacity_;
}

std::size_t ThreadPool::pending_tasks() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

void ThreadPool::worker_loop() {
    while (true) {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(
                lock, [this] { return stopping_ || !tasks_.empty(); });

            if (stopping_ && tasks_.empty()) {
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop();
        }

        space_condition_.notify_one();

        try {
            task();
        } catch (...) {
            // packaged_task 将任务异常保存到 future，不能让工作线程退出。
        }
    }
}

}  // namespace application
}  // namespace scanner
