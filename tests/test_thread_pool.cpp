// ThreadPool 单元测试
#include "minitest.h"

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>

#include "scanner/application/thread_pool.h"

namespace {

using scanner::application::ThreadPool;

// ============================================================
// 构造与销毁
// ============================================================
TEST(ThreadPoolTest, CreateWithValidCount) {
    EXPECT_NO_THROW({ ThreadPool pool(2); });
    EXPECT_NO_THROW({ ThreadPool pool(8, 256); });
}

TEST(ThreadPoolTest, ZeroThreadsThrows) {
    EXPECT_THROW({ ThreadPool pool(0); }, std::invalid_argument);
}

TEST(ThreadPoolTest, ZeroCapacityThrows) {
    EXPECT_THROW({ ThreadPool pool(2, 0); }, std::invalid_argument);
}

TEST(ThreadPoolTest, SizeReturnsCorrectCount) {
    ThreadPool pool(4);
    EXPECT_EQ(pool.size(), 4u);
}

TEST(ThreadPoolTest, CapacityReturnsCorrectValue) {
    ThreadPool pool(2, 128);
    EXPECT_EQ(pool.capacity(), 128u);
}

// ============================================================
// 任务提交与执行
// ============================================================
TEST(ThreadPoolTest, EnqueueAndGetResult) {
    ThreadPool pool(2);
    auto future = pool.enqueue([] { return 42; });
    EXPECT_EQ(future.get(), 42);
    pool.shutdown();
}

TEST(ThreadPoolTest, MultipleTasks) {
    ThreadPool pool(4);
    std::vector<std::future<int>> futures;
    for (int i = 0; i < 10; ++i) {
        futures.push_back(pool.enqueue([i] { return i * i; }));
    }
    pool.shutdown();

    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(futures[i].get(), i * i);
    }
}

TEST(ThreadPoolTest, TasksRunInParallel) {
    ThreadPool pool(4);
    std::atomic<int> counter{0};

    for (int i = 0; i < 20; ++i) {
        pool.enqueue([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            ++counter;
        });
    }
    pool.shutdown();

    EXPECT_EQ(counter.load(), 20);
}

// ============================================================
// Shutdown
// ============================================================
TEST(ThreadPoolTest, ShutdownProcessesQueuedTasks) {
    ThreadPool pool(2);
    std::atomic<int> counter{0};

    for (int i = 0; i < 10; ++i) {
        pool.enqueue([&] { ++counter; });
    }
    pool.shutdown();

    EXPECT_EQ(counter.load(), 10);
}

TEST(ThreadPoolTest, EnqueueAfterShutdownThrows) {
    ThreadPool pool(2);
    pool.shutdown();
    EXPECT_THROW({ pool.enqueue([] { return 1; }); }, std::runtime_error);
}

TEST(ThreadPoolTest, MultipleShutdownCallsSafe) {
    ThreadPool pool(2);
    pool.shutdown();
    EXPECT_NO_THROW(pool.shutdown());
    EXPECT_NO_THROW(pool.shutdown());
}

// ============================================================
// 队列容量 / 背压
// ============================================================
TEST(ThreadPoolTest, PendingTasksCount) {
    ThreadPool pool(1, 64);
    EXPECT_EQ(pool.pending_tasks(), 0u);

    for (int i = 0; i < 30; ++i) {
        pool.enqueue([] { std::this_thread::sleep_for(std::chrono::milliseconds(5)); });
    }
    // 生产者可能阻塞，但一定有任务在排队或执行
    auto pending = pool.pending_tasks();
    // 有些任务已被消费，pending <= 30
    EXPECT_LE(pending, 30u);

    pool.shutdown();
}

// ============================================================
// 取消令牌
// ============================================================
TEST(ThreadPoolTest, CancelBeforeEnqueue) {
    ThreadPool pool(1, 2);
    std::atomic_bool cancel{true};

    EXPECT_THROW(
        {
            pool.enqueue_with_cancel(&cancel, [] { return 1; });
        },
        std::runtime_error);

    pool.shutdown();
}

TEST(ThreadPoolTest, CancelMidQueue) {
    ThreadPool pool(2, 3);
    std::atomic_bool cancel{false};
    std::atomic<int> processed{0};

    // 填满队列
    for (int i = 0; i < 3; ++i) {
        pool.enqueue([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            ++processed;
        });
    }

    cancel.store(true);
    try {
        pool.enqueue_with_cancel(&cancel, [&] { ++processed; });
        FAIL();
    } catch (const std::runtime_error&) {
        // 预期行为
    }

    pool.shutdown();
    EXPECT_EQ(processed.load(), 3);
}

// ============================================================
// 异常处理
// ============================================================
TEST(ThreadPoolTest, ExceptionInTaskPropagated) {
    ThreadPool pool(1);
    auto future = pool.enqueue([] { throw std::runtime_error("task error"); });
    EXPECT_THROW({ future.get(); }, std::runtime_error);
    pool.shutdown();
}

}  // namespace
