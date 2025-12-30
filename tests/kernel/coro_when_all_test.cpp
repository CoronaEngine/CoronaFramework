/**
 * @file coro_when_all_test.cpp
 * @brief when_all 并行组合测试
 */

#include <corona/kernel/coro/coro.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "../test_framework.h"

using namespace Corona::Kernel::Coro;
using namespace CoronaTest;

// ========================================
// 基础功能测试
// ========================================

TEST(CoroWhenAll, VariadicBasic) {
    auto task1 = []() -> Task<int> { co_return 1; }();
    auto task2 = []() -> Task<int> { co_return 2; }();
    auto task3 = []() -> Task<int> { co_return 3; }();

    auto all_task = when_all(std::move(task1), std::move(task2), std::move(task3));
    auto result = all_task.get();

    ASSERT_EQ(std::get<0>(result), 1);
    ASSERT_EQ(std::get<1>(result), 2);
    ASSERT_EQ(std::get<2>(result), 3);
}

TEST(CoroWhenAll, VariadicMixedTypes) {
    auto task1 = []() -> Task<int> { co_return 42; }();
    auto task2 = []() -> Task<std::string> { co_return "hello"; }();
    auto task3 = []() -> Task<double> { co_return 3.14; }();

    auto all_task = when_all(std::move(task1), std::move(task2), std::move(task3));
    auto result = all_task.get();

    ASSERT_EQ(std::get<0>(result), 42);
    ASSERT_EQ(std::get<1>(result), "hello");
    ASSERT_EQ(std::get<2>(result), 3.14);
}

TEST(CoroWhenAll, VariadicWithVoid) {
    bool void_executed = false;
    auto task1 = []() -> Task<int> { co_return 100; }();
    auto task2 = [&]() -> Task<void> {
        void_executed = true;
        co_return;
    }();

    auto all_task = when_all(std::move(task1), std::move(task2));
    auto result = all_task.get();

    ASSERT_EQ(std::get<0>(result), 100);
    // std::get<1>(result) is std::monostate
    ASSERT_TRUE(void_executed);
}

TEST(CoroWhenAll, VectorBasic) {
    std::vector<Task<int>> tasks;
    auto make_task = [](int val) -> Task<int> {
        co_return val;
    };

    for (int i = 0; i < 5; ++i) {
        tasks.push_back(make_task(i));
    }

    auto all_task = when_all(std::move(tasks));
    auto result = all_task.get();

    ASSERT_EQ(result.size(), 5u);
    for (int i = 0; i < 5; ++i) {
        ASSERT_EQ(result[i], i);
    }
}

TEST(CoroWhenAll, VectorVoid) {
    std::atomic<int> counter{0};
    std::vector<Task<void>> tasks;
    for (int i = 0; i < 5; ++i) {
        tasks.push_back([&]() -> Task<void> {
            counter++;
            co_return;
        }());
    }

    auto all_task = when_all(std::move(tasks));
    auto result = all_task.get();  // result is vector<monostate>

    ASSERT_EQ(result.size(), 5u);
    ASSERT_EQ(counter.load(), 5);
}

// ========================================
// 并发性测试
// ========================================

TEST(CoroWhenAll, ConcurrentExecution) {
    // 使用 TbbExecutor 确保多线程执行
    TbbExecutor executor(4);
    std::atomic<int> active_threads{0};
    std::atomic<int> max_concurrent{0};

    auto make_task = [&](int ms) -> Task<void> {
        co_await switch_to(executor);

        int current = ++active_threads;
        int max = max_concurrent.load();
        while (current > max) {
            max_concurrent.compare_exchange_strong(max, current);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{ms});

        --active_threads;
        co_return;
    };

    std::vector<Task<void>> tasks;
    for (int i = 0; i < 8; ++i) {
        tasks.push_back(make_task(50));
    }

    auto all_task = when_all(std::move(tasks));
    all_task.get();

    // 应该有并发执行
    ASSERT_GT(max_concurrent.load(), 1);
}

// ========================================
// 异常处理测试
// ========================================

TEST(CoroWhenAll, ExceptionPropagation) {
    auto task1 = []() -> Task<int> {
        co_await suspend_for(std::chrono::milliseconds{10});
        co_return 1;
    }();

    auto task2 = []() -> Task<int> {
        throw std::runtime_error("Task failed");
        co_return 2;
    }();

    auto all_task = when_all(std::move(task1), std::move(task2));

    bool caught = false;
    try {
        all_task.get();
    } catch (const std::runtime_error& e) {
        caught = true;
        ASSERT_EQ(std::string(e.what()), "Task failed");
    }
    ASSERT_TRUE(caught);
}

TEST(CoroWhenAll, MultipleExceptions) {
    // 即使多个任务抛出异常，也应该只传播一个，且程序不崩溃
    auto task1 = []() -> Task<void> {
        throw std::runtime_error("Error 1");
        co_return;
    }();

    auto task2 = []() -> Task<void> {
        throw std::logic_error("Error 2");
        co_return;
    }();

    auto all_task = when_all(std::move(task1), std::move(task2));

    bool caught = false;
    try {
        all_task.get();
    } catch (const std::exception&) {
        caught = true;
    }
    ASSERT_TRUE(caught);
}

int main() {
    return TestRunner::instance().run_all();
}
