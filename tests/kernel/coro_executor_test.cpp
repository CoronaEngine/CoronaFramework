/**
 * @file coro_executor_test.cpp
 * @brief 执行器和调度器单元测试
 */

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "../test_framework.h"
#include <corona/kernel/coro/coro.h>

using namespace Corona::Kernel::Coro;
using namespace CoronaTest;

// ========================================
// InlineExecutor 测试
// ========================================

TEST(Executor, InlineExecutorBasic) {
    InlineExecutor executor;
    bool executed = false;

    executor.execute([&]() { executed = true; });

    ASSERT_TRUE(executed);
}

TEST(Executor, InlineExecutorThread) {
    InlineExecutor executor;
    ASSERT_TRUE(executor.is_in_executor_thread());
}

TEST(Executor, InlineExecutorDelay) {
    InlineExecutor executor;
    auto start = std::chrono::steady_clock::now();

    executor.execute_after([]() {}, std::chrono::milliseconds{50});

    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    ASSERT_GE(elapsed.count(), 40);
}

// ========================================
// TbbExecutor 测试
// ========================================

TEST(Executor, TbbExecutorBasic) {
    TbbExecutor executor;
    std::atomic<bool> executed{false};

    executor.execute([&]() { executed = true; });
    executor.wait();

    ASSERT_TRUE(executed);
}

TEST(Executor, TbbExecutorMultipleTasks) {
    TbbExecutor executor;
    std::atomic<int> counter{0};
    constexpr int num_tasks = 100;

    for (int i = 0; i < num_tasks; ++i) {
        executor.execute([&]() { counter++; });
    }
    executor.wait();

    ASSERT_EQ(counter.load(), num_tasks);
}

TEST(Executor, TbbExecutorDelay) {
    TbbExecutor executor;
    std::atomic<bool> executed{false};
    auto start = std::chrono::steady_clock::now();

    executor.execute_after([&]() { executed = true; }, std::chrono::milliseconds{50});

    // 等待任务完成
    while (!executed) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    ASSERT_GE(elapsed.count(), 40);
}

TEST(Executor, TbbExecutorConcurrency) {
    TbbExecutor executor(4);  // 4 个线程
    ASSERT_EQ(executor.max_concurrency(), 4);
}

TEST(Executor, TbbExecutorShutdown) {
    TbbExecutor executor;
    std::atomic<int> counter{0};

    // 提交一些任务
    for (int i = 0; i < 10; ++i) {
        executor.execute([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
            counter++;
        });
    }

    // 关闭执行器（会等待所有任务完成）
    executor.shutdown();

    ASSERT_EQ(counter.load(), 10);
    ASSERT_FALSE(executor.is_running());
}

// ========================================
// Scheduler 测试
// ========================================

TEST(Scheduler, DefaultExecutor) {
    // 重置调度器
    Scheduler::instance().reset();

    ASSERT_FALSE(Scheduler::instance().has_default_executor());

    // 获取默认执行器会惰性初始化
    auto& executor = Scheduler::instance().default_executor();
    ASSERT_TRUE(Scheduler::instance().has_default_executor());

    // 清理
    Scheduler::instance().reset();
}

TEST(Scheduler, CustomExecutor) {
    Scheduler::instance().reset();

    auto custom_executor = std::make_shared<TbbExecutor>(2);
    Scheduler::instance().set_default_executor(custom_executor);

    ASSERT_TRUE(Scheduler::instance().has_default_executor());
    ASSERT_EQ(&Scheduler::instance().default_executor(), custom_executor.get());

    Scheduler::instance().reset();
}

// ========================================
// 协程与执行器集成测试
// ========================================

TEST(Scheduler, ScheduleCoroutine) {
    Scheduler::instance().reset();

    std::atomic<bool> executed{false};

    // 使用 spawn 而不是 get()，因为 schedule_on_pool 会在不同线程恢复
    auto task = [&]() -> Task<void> {
        co_await schedule_on_pool();
        executed = true;
        co_return;
    }();

    // 手动启动任务
    task.resume();

    // 等待任务完成
    int timeout_ms = 1000;
    while (!executed && timeout_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        timeout_ms -= 10;
    }

    ASSERT_TRUE(executed);

    Scheduler::instance().reset();
}

TEST(Scheduler, ScheduleAfter) {
    Scheduler::instance().reset();

    std::atomic<bool> completed{false};
    std::atomic<int> elapsed_ms{0};

    auto task = [&]() -> Task<void> {
        auto start = std::chrono::steady_clock::now();
        co_await schedule_on_pool_after(std::chrono::milliseconds{50});
        auto end = std::chrono::steady_clock::now();
        elapsed_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
        completed = true;
        co_return;
    }();

    // 启动任务
    task.resume();

    // 等待完成
    int timeout_ms = 1000;
    while (!completed && timeout_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        timeout_ms -= 10;
    }

    ASSERT_TRUE(completed);
    ASSERT_GE(elapsed_ms.load(), 40);

    Scheduler::instance().reset();
}

// ========================================
// Runner 测试
// ========================================

TEST(Runner, RunTask) {
    auto task = []() -> Task<int> {
        co_return 42;
    }();

    int result = Runner::run(std::move(task));
    ASSERT_EQ(result, 42);
}

TEST(Runner, RunVoidTask) {
    bool executed = false;
    auto task = [&]() -> Task<void> {
        executed = true;
        co_return;
    }();

    Runner::run(std::move(task));
    ASSERT_TRUE(executed);
}

TEST(Runner, SpawnTask) {
    Scheduler::instance().reset();

    std::atomic<bool> completed{false};

    auto task = [&]() -> Task<void> {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        completed = true;
        co_return;
    }();

    Runner::spawn(std::move(task));

    // 等待任务完成
    while (!completed) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    ASSERT_TRUE(completed);
    Scheduler::instance().reset();
}

// ========================================
// SwitchTo 测试
// ========================================

TEST(Awaitable, SwitchToExecutor) {
    TbbExecutor executor;
    std::atomic<bool> switched{false};

    auto task = [&]() -> Task<void> {
        co_await switch_to(executor);
        switched = true;
        co_return;
    }();

    // 启动任务
    task.resume();

    // 等待完成
    int timeout_ms = 1000;
    while (!switched && timeout_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        timeout_ms -= 10;
    }

    executor.wait();
    ASSERT_TRUE(switched);
}

TEST(Awaitable, SwitchToExecutorAfter) {
    TbbExecutor executor;
    std::atomic<bool> completed{false};
    std::atomic<int> elapsed_ms{0};

    auto task = [&]() -> Task<void> {
        auto start = std::chrono::steady_clock::now();
        co_await switch_to_after(executor, std::chrono::milliseconds{50});
        auto end = std::chrono::steady_clock::now();
        elapsed_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
        completed = true;
        co_return;
    }();

    // 启动任务
    task.resume();

    // 等待完成
    int timeout_ms = 1000;
    while (!completed && timeout_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        timeout_ms -= 10;
    }

    executor.wait();
    ASSERT_TRUE(completed);
    ASSERT_GE(elapsed_ms.load(), 40);
}

// ========================================
// Main
// ========================================

int main() {
    return TestRunner::instance().run_all();
}
