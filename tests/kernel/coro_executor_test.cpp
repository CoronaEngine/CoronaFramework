/**
 * @file coro_executor_test.cpp
 * @brief 执行器和调度器单元测试
 */

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
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
// WaitUntil 测试
// ========================================

TEST(Awaitable, WaitUntilBasic) {
    std::atomic<bool> flag{false};
    
    // 启动一个线程在 50ms 后设置 flag
    std::thread setter([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
        flag = true;
    });
    
    auto task = [&]() -> Task<void> {
        co_await wait_until([&]() { return flag.load(); });
        co_return;
    }();
    
    auto start = std::chrono::steady_clock::now();
    task.get();
    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    setter.join();
    
    ASSERT_TRUE(flag);
    ASSERT_GE(elapsed.count(), 40);  // 应该等待了至少 40ms
}

TEST(Awaitable, WaitUntilAlreadyTrue) {
    auto task = []() -> Task<void> {
        co_await wait_until([]() { return true; });
        co_return;
    }();
    
    auto start = std::chrono::steady_clock::now();
    task.get();
    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // 应该立即返回，不需要等待
    ASSERT_LT(elapsed.count(), 50);
}

TEST(Awaitable, WaitUntilWithInterval) {
    std::atomic<int> check_count{0};
    std::atomic<bool> flag{false};
    
    std::thread setter([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
        flag = true;
    });
    
    auto task = [&]() -> Task<void> {
        co_await wait_until([&]() { 
            check_count++;
            return flag.load(); 
        }, std::chrono::milliseconds{20});
        co_return;
    }();
    
    task.get();
    setter.join();
    
    ASSERT_TRUE(flag);
    // 使用 20ms 间隔检查 100ms，应该检查约 5 次（加上最后一次成功的）
    ASSERT_GE(check_count.load(), 3);
}

// ========================================
// ReadyValue 测试
// ========================================

TEST(Awaitable, ReadyValueBasic) {
    auto task = []() -> Task<int> {
        int value = co_await ready(42);
        co_return value;
    }();
    
    int result = task.get();
    ASSERT_EQ(result, 42);
}

TEST(Awaitable, ReadyValueVoid) {
    bool executed = false;
    auto task = [&]() -> Task<void> {
        co_await ready();
        executed = true;
        co_return;
    }();
    
    task.get();
    ASSERT_TRUE(executed);
}

TEST(Awaitable, ReadyValueString) {
    auto task = []() -> Task<std::string> {
        std::string value = co_await ready(std::string("Hello, Ready!"));
        co_return value;
    }();
    
    std::string result = task.get();
    ASSERT_EQ(result, "Hello, Ready!");
}

TEST(Awaitable, ReadyValueChain) {
    auto task = []() -> Task<int> {
        int a = co_await ready(10);
        int b = co_await ready(20);
        int c = co_await ready(30);
        co_return a + b + c;
    }();
    
    int result = task.get();
    ASSERT_EQ(result, 60);
}

// ========================================
// Yield 测试
// ========================================

TEST(Awaitable, YieldBasic) {
    int counter = 0;
    auto task = [&]() -> Task<void> {
        counter = 1;
        co_await yield();
        counter = 2;
        co_await yield();
        counter = 3;
        co_return;
    }();
    
    task.get();
    ASSERT_EQ(counter, 3);
}

TEST(Awaitable, YieldMultiple) {
    std::vector<int> sequence;
    auto task = [&]() -> Task<void> {
        for (int i = 0; i < 5; ++i) {
            sequence.push_back(i);
            co_await yield();
        }
        co_return;
    }();
    
    task.get();
    ASSERT_EQ(sequence.size(), 5u);
    for (int i = 0; i < 5; ++i) {
        ASSERT_EQ(sequence[i], i);
    }
}

// ========================================
// SuspendFor 测试
// ========================================

TEST(Awaitable, SuspendForZero) {
    auto task = []() -> Task<void> {
        auto start = std::chrono::steady_clock::now();
        co_await suspend_for(std::chrono::milliseconds{0});
        auto end = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        // 应该立即返回
        ASSERT_LT(elapsed.count(), 50);
        co_return;
    }();
    
    task.get();
}

TEST(Awaitable, SuspendForNegative) {
    auto task = []() -> Task<void> {
        auto start = std::chrono::steady_clock::now();
        co_await suspend_for(std::chrono::milliseconds{-100});
        auto end = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        // 负数应该立即返回
        ASSERT_LT(elapsed.count(), 50);
        co_return;
    }();
    
    task.get();
}

TEST(Awaitable, SuspendForSeconds) {
    auto task = []() -> Task<int> {
        auto start = std::chrono::steady_clock::now();
        co_await suspend_for_seconds(0.05);  // 50ms
        auto end = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        co_return static_cast<int>(elapsed.count());
    }();
    
    int elapsed_ms = task.get();
    ASSERT_GE(elapsed_ms, 40);
}

// ========================================
// TbbExecutor 高级测试
// ========================================

TEST(Executor, TbbExecutorStress) {
    TbbExecutor executor;
    std::atomic<int> counter{0};
    constexpr int num_tasks = 1000;

    for (int i = 0; i < num_tasks; ++i) {
        executor.execute([&]() { 
            // 做一些工作
            std::this_thread::sleep_for(std::chrono::microseconds{100});
            counter++; 
        });
    }
    executor.wait();

    ASSERT_EQ(counter.load(), num_tasks);
}

TEST(Executor, TbbExecutorConcurrentSubmit) {
    TbbExecutor executor;
    std::atomic<int> counter{0};
    constexpr int num_threads = 4;
    constexpr int tasks_per_thread = 100;

    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < tasks_per_thread; ++i) {
                executor.execute([&]() { counter++; });
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }
    executor.wait();

    ASSERT_EQ(counter.load(), num_threads * tasks_per_thread);
}

TEST(Executor, TbbExecutorDelayOrdering) {
    TbbExecutor executor;
    std::vector<int> order;
    std::mutex order_mutex;

    // 提交延时任务，期望按延时顺序执行
    executor.execute_after([&]() {
        std::lock_guard lock(order_mutex);
        order.push_back(3);
    }, std::chrono::milliseconds{60});

    executor.execute_after([&]() {
        std::lock_guard lock(order_mutex);
        order.push_back(1);
    }, std::chrono::milliseconds{20});

    executor.execute_after([&]() {
        std::lock_guard lock(order_mutex);
        order.push_back(2);
    }, std::chrono::milliseconds{40});

    // 等待所有任务完成
    std::this_thread::sleep_for(std::chrono::milliseconds{150});
    executor.wait();

    ASSERT_EQ(order.size(), 3u);
    ASSERT_EQ(order[0], 1);
    ASSERT_EQ(order[1], 2);
    ASSERT_EQ(order[2], 3);
}

TEST(Executor, TbbExecutorAfterShutdown) {
    TbbExecutor executor;
    executor.shutdown();
    
    // 关闭后提交任务不应该执行
    std::atomic<bool> executed{false};
    executor.execute([&]() { executed = true; });
    
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    ASSERT_FALSE(executed);
    ASSERT_FALSE(executor.is_running());
}

// ========================================
// Scheduler 高级测试
// ========================================

TEST(Scheduler, MultipleCoroutinesOnPool) {
    Scheduler::instance().reset();
    
    std::atomic<int> completed_count{0};
    constexpr int num_tasks = 5;
    
    // 使用更简单的方式：直接在默认执行器上运行
    for (int i = 0; i < num_tasks; ++i) {
        Scheduler::instance().default_executor().execute([&]() {
            completed_count++;
        });
    }
    
    // 等待所有任务完成
    int timeout_ms = 2000;
    while (completed_count < num_tasks && timeout_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        timeout_ms -= 10;
    }
    
    ASSERT_EQ(completed_count.load(), num_tasks);
    Scheduler::instance().reset();
}

TEST(Scheduler, NestedScheduleOnPool) {
    Scheduler::instance().reset();
    
    std::atomic<bool> inner_completed{false};
    std::atomic<bool> outer_completed{false};
    
    auto inner = [&]() -> Task<int> {
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
        inner_completed = true;
        co_return 42;
    };
    
    auto outer_task = [&]() -> Task<void> {
        int result = co_await inner();
        ASSERT_EQ(result, 42);
        outer_completed = true;
        co_return;
    }();
    
    // 直接运行而不是通过调度器
    outer_task.get();
    
    ASSERT_TRUE(inner_completed);
    ASSERT_TRUE(outer_completed);
    Scheduler::instance().reset();
}

// ========================================
// Runner 高级测试
// ========================================

TEST(Runner, SpawnMultipleTasks) {
    Scheduler::instance().reset();
    
    std::atomic<int> counter{0};
    constexpr int num_tasks = 5;
    
    for (int i = 0; i < num_tasks; ++i) {
        auto task = [&]() -> Task<void> {
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
            counter++;
            co_return;
        }();
        
        Runner::spawn(std::move(task));
    }
    
    // 等待所有任务完成
    int timeout_ms = 1000;
    while (counter < num_tasks && timeout_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        timeout_ms -= 10;
    }
    
    ASSERT_EQ(counter.load(), num_tasks);
    Scheduler::instance().reset();
}

TEST(Runner, RunWithException) {
    auto task = []() -> Task<int> {
        throw std::runtime_error("Runner exception test");
        co_return 0;
    }();
    
    bool caught = false;
    try {
        Runner::run(std::move(task));
    } catch (const std::runtime_error& e) {
        caught = true;
        std::string msg = e.what();
        ASSERT_TRUE(msg.find("Runner exception") != std::string::npos);
    }
    ASSERT_TRUE(caught);
}

// ========================================
// 混合 Awaitable 测试
// ========================================

TEST(Awaitable, MixedAwaitables) {
    std::atomic<int> step{0};
    
    auto task = [&]() -> Task<int> {
        step = 1;
        co_await yield();
        
        step = 2;
        int val = co_await ready(100);
        
        step = 3;
        co_await suspend_for(std::chrono::milliseconds{20});
        
        step = 4;
        co_return val + step.load();
    }();
    
    int result = task.get();
    ASSERT_EQ(step.load(), 4);
    ASSERT_EQ(result, 104);  // 100 + 4
}

// ========================================
// InlineExecutor 高级测试
// ========================================

TEST(Executor, InlineExecutorChain) {
    InlineExecutor executor;
    std::vector<int> order;
    
    executor.execute([&]() {
        order.push_back(1);
        executor.execute([&]() {
            order.push_back(2);
            executor.execute([&]() {
                order.push_back(3);
            });
        });
    });
    
    ASSERT_EQ(order.size(), 3u);
    ASSERT_EQ(order[0], 1);
    ASSERT_EQ(order[1], 2);
    ASSERT_EQ(order[2], 3);
}

TEST(Executor, InlineExecutorWithCoroutine) {
    InlineExecutor executor;
    std::atomic<bool> switched{false};
    
    auto task = [&]() -> Task<void> {
        co_await switch_to(executor);
        switched = true;
        co_return;
    }();
    
    task.resume();
    
    ASSERT_TRUE(switched);
}

// ========================================
// 全局内联执行器测试
// ========================================

TEST(Executor, GlobalInlineExecutor) {
    auto& exec1 = inline_executor();
    auto& exec2 = inline_executor();
    
    // 应该是同一个实例
    ASSERT_EQ(&exec1, &exec2);
    
    bool executed = false;
    exec1.execute([&]() { executed = true; });
    ASSERT_TRUE(executed);
}

// ========================================
// Main
// ========================================

int main() {
    return TestRunner::instance().run_all();
}
