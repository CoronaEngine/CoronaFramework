/**
 * @file coro_when_all_test.cpp
 * @brief when_all 并行组合测试
 */

#include <corona/kernel/coro/coro.h>

#include <atomic>
#include <chrono>
#include <memory>
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

// ========================================
// 边界条件测试
// ========================================

TEST(CoroWhenAll, EmptyVector) {
    // 空 vector 应该立即返回空结果
    std::vector<Task<int>> tasks;
    auto all_task = when_all(std::move(tasks));
    auto result = all_task.get();

    ASSERT_EQ(result.size(), 0u);
}

TEST(CoroWhenAll, EmptyVoidVector) {
    std::vector<Task<void>> tasks;
    auto all_task = when_all(std::move(tasks));
    auto result = all_task.get();

    ASSERT_EQ(result.size(), 0u);
}

TEST(CoroWhenAll, SingleTask) {
    // 单个任务
    auto task = []() -> Task<int> { co_return 42; }();
    auto all_task = when_all(std::move(task));
    auto result = all_task.get();

    ASSERT_EQ(std::get<0>(result), 42);
}

TEST(CoroWhenAll, SingleVoidTask) {
    bool executed = false;
    auto task = [&]() -> Task<void> {
        executed = true;
        co_return;
    }();

    auto all_task = when_all(std::move(task));
    all_task.get();

    ASSERT_TRUE(executed);
}

TEST(CoroWhenAll, SingleTaskVector) {
    std::vector<Task<int>> tasks;
    tasks.push_back([]() -> Task<int> { co_return 100; }());

    auto all_task = when_all(std::move(tasks));
    auto result = all_task.get();

    ASSERT_EQ(result.size(), 1u);
    ASSERT_EQ(result[0], 100);
}

// ========================================
// 结果顺序测试
// ========================================

TEST(CoroWhenAll, ResultOrderPreserved) {
    // 即使任务完成顺序不同，结果也应该按原始顺序排列
    TbbExecutor executor(4);

    // 第一个任务最慢完成，但结果应该在第一位
    auto task1 = [&executor]() -> Task<int> {
        co_await switch_to(executor);
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
        co_return 1;
    }();

    auto task2 = [&executor]() -> Task<int> {
        co_await switch_to(executor);
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        co_return 2;
    }();

    auto task3 = [&executor]() -> Task<int> {
        co_await switch_to(executor);
        std::this_thread::sleep_for(std::chrono::milliseconds{30});
        co_return 3;
    }();

    auto all_task = when_all(std::move(task1), std::move(task2), std::move(task3));
    auto result = all_task.get();

    ASSERT_EQ(std::get<0>(result), 1);
    ASSERT_EQ(std::get<1>(result), 2);
    ASSERT_EQ(std::get<2>(result), 3);
}

TEST(CoroWhenAll, VectorResultOrderPreserved) {
    auto executor = std::make_shared<TbbExecutor>(4);

    // 使用结构体来保证值的正确传递
    struct TaskParams {
        int id;
        int delay;
    };
    std::vector<TaskParams> params = {{0, 50}, {1, 10}, {2, 30}, {3, 20}, {4, 40}};

    std::vector<Task<int>> tasks;
    for (const auto& p : params) {
        tasks.push_back([](std::shared_ptr<TbbExecutor> exec, int id, int delay) -> Task<int> {
            co_await switch_to(*exec);
            std::this_thread::sleep_for(std::chrono::milliseconds{delay});
            co_return id;
        }(executor, p.id, p.delay));
    }

    auto all_task = when_all(std::move(tasks));
    auto result = all_task.get();

    ASSERT_EQ(result.size(), 5u);
    for (int i = 0; i < 5; ++i) {
        ASSERT_EQ(result[i], i);
    }
}

// ========================================
// 复杂类型测试
// ========================================

TEST(CoroWhenAll, MoveOnlyTypes) {
    // 测试只支持移动的类型
    auto task1 = []() -> Task<std::unique_ptr<int>> {
        co_return std::make_unique<int>(42);
    }();

    auto task2 = []() -> Task<std::unique_ptr<std::string>> {
        co_return std::make_unique<std::string>("hello");
    }();

    auto all_task = when_all(std::move(task1), std::move(task2));
    auto result = all_task.get();

    ASSERT_EQ(*std::get<0>(result), 42);
    ASSERT_EQ(*std::get<1>(result), "hello");
}

TEST(CoroWhenAll, LargeVector) {
    // 大量任务
    constexpr int num_tasks = 500;
    std::vector<Task<int>> tasks;

    for (int i = 0; i < num_tasks; ++i) {
        tasks.push_back([](int val) -> Task<int> { co_return val * 2; }(i));
    }

    auto all_task = when_all(std::move(tasks));
    auto result = all_task.get();

    ASSERT_EQ(result.size(), static_cast<size_t>(num_tasks));
    for (int i = 0; i < num_tasks; ++i) {
        ASSERT_EQ(result[i], i * 2);
    }
}

// ========================================
// 嵌套 when_all 测试
// ========================================

TEST(CoroWhenAll, NestedWhenAll) {
    auto inner1 = when_all(
        []() -> Task<int> { co_return 1; }(),
        []() -> Task<int> { co_return 2; }());

    auto inner2 = when_all(
        []() -> Task<int> { co_return 3; }(),
        []() -> Task<int> { co_return 4; }());

    auto outer = when_all(std::move(inner1), std::move(inner2));
    auto result = outer.get();

    auto [r1, r2] = result;
    ASSERT_EQ(std::get<0>(r1), 1);
    ASSERT_EQ(std::get<1>(r1), 2);
    ASSERT_EQ(std::get<0>(r2), 3);
    ASSERT_EQ(std::get<1>(r2), 4);
}

// ========================================
// 异常在不同任务位置测试
// ========================================

TEST(CoroWhenAll, ExceptionInFirstTask) {
    auto task1 = []() -> Task<int> {
        throw std::runtime_error("First failed");
        co_return 1;
    }();
    auto task2 = []() -> Task<int> { co_return 2; }();
    auto task3 = []() -> Task<int> { co_return 3; }();

    auto all_task = when_all(std::move(task1), std::move(task2), std::move(task3));

    bool caught = false;
    try {
        all_task.get();
    } catch (const std::runtime_error& e) {
        caught = true;
        ASSERT_EQ(std::string(e.what()), "First failed");
    }
    ASSERT_TRUE(caught);
}

TEST(CoroWhenAll, ExceptionInMiddleTask) {
    auto task1 = []() -> Task<int> { co_return 1; }();
    auto task2 = []() -> Task<int> {
        throw std::runtime_error("Middle failed");
        co_return 2;
    }();
    auto task3 = []() -> Task<int> { co_return 3; }();

    auto all_task = when_all(std::move(task1), std::move(task2), std::move(task3));

    bool caught = false;
    try {
        all_task.get();
    } catch (const std::runtime_error& e) {
        caught = true;
        ASSERT_EQ(std::string(e.what()), "Middle failed");
    }
    ASSERT_TRUE(caught);
}

TEST(CoroWhenAll, ExceptionInLastTask) {
    auto task1 = []() -> Task<int> { co_return 1; }();
    auto task2 = []() -> Task<int> { co_return 2; }();
    auto task3 = []() -> Task<int> {
        throw std::runtime_error("Last failed");
        co_return 3;
    }();

    auto all_task = when_all(std::move(task1), std::move(task2), std::move(task3));

    bool caught = false;
    try {
        all_task.get();
    } catch (const std::runtime_error& e) {
        caught = true;
        ASSERT_EQ(std::string(e.what()), "Last failed");
    }
    ASSERT_TRUE(caught);
}

// ========================================
// 性能相关测试
// ========================================

// 注意：ParallelSpeedup 测试已删除，因为它与 ConcurrentExecution 重复
// ConcurrentExecution 已经验证了并行执行和并发计数

TEST(CoroWhenAll, AllVoidTasks) {
    // 全部是 void 任务
    std::atomic<int> counter{0};

    auto task1 = [&]() -> Task<void> {
        counter++;
        co_return;
    }();
    auto task2 = [&]() -> Task<void> {
        counter++;
        co_return;
    }();
    auto task3 = [&]() -> Task<void> {
        counter++;
        co_return;
    }();

    auto all_task = when_all(std::move(task1), std::move(task2), std::move(task3));
    auto result = all_task.get();

    // 结果是 tuple<monostate, monostate, monostate>
    ASSERT_EQ(counter.load(), 3);
    static_assert(std::is_same_v<decltype(std::get<0>(result)), std::monostate&>);
}

int main() {
    return TestRunner::instance().run_all();
}
