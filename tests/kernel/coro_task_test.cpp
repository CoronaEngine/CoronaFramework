/**
 * @file coro_task_test.cpp
 * @brief Task<T> 单元测试
 */

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "../test_framework.h"
#include <corona/kernel/coro/coro.h>

using namespace Corona::Kernel::Coro;
using namespace CoronaTest;

// ========================================
// 基础功能测试
// ========================================

TEST(CoroTask, BasicReturnValue) {
    auto task = []() -> Task<int> {
        co_return 42;
    }();

    int result = task.get();
    ASSERT_EQ(result, 42);
}

TEST(CoroTask, VoidTask) {
    bool executed = false;
    auto task = [&]() -> Task<void> {
        executed = true;
        co_return;
    }();

    task.get();
    ASSERT_TRUE(executed);
}

TEST(CoroTask, StringReturn) {
    auto task = []() -> Task<std::string> {
        co_return "Hello, Coroutine!";
    }();

    std::string result = task.get();
    ASSERT_EQ(result, "Hello, Coroutine!");
}

TEST(CoroTask, MoveOnlyType) {
    auto task = []() -> Task<std::unique_ptr<int>> {
        co_return std::make_unique<int>(123);
    }();

    auto result = task.get();
    ASSERT_TRUE(result != nullptr);
    ASSERT_EQ(*result, 123);
}

// ========================================
// 协程嵌套测试
// ========================================

Task<int> nested_add(int a, int b) {
    co_return a + b;
}

Task<int> nested_compute() {
    int x = co_await nested_add(1, 2);
    int y = co_await nested_add(3, 4);
    co_return x + y;
}

TEST(CoroTask, NestedCoroutines) {
    auto task = nested_compute();
    int result = task.get();
    ASSERT_EQ(result, 10);  // (1+2) + (3+4) = 10
}

Task<int> deep_nesting(int depth) {
    if (depth <= 0) {
        co_return 1;
    }
    int value = co_await deep_nesting(depth - 1);
    co_return value + 1;
}

TEST(CoroTask, DeepNesting) {
    // 测试深层嵌套不会导致栈溢出（对称转移）
    auto task = deep_nesting(100);
    int result = task.get();
    ASSERT_EQ(result, 101);
}

// ========================================
// 异常传播测试
// ========================================

TEST(CoroTask, ExceptionPropagation) {
    auto task = []() -> Task<int> {
        throw std::runtime_error("Test exception");
        co_return 0;  // 不会执行
    }();

    bool caught = false;
    try {
        task.get();
    } catch (const std::runtime_error& e) {
        caught = true;
        std::string msg = e.what();
        ASSERT_TRUE(msg.find("Test exception") != std::string::npos);
    }
    ASSERT_TRUE(caught);
}

TEST(CoroTask, NestedExceptionPropagation) {
    auto inner = []() -> Task<int> {
        throw std::logic_error("Inner exception");
        co_return 0;
    };

    auto outer = [&]() -> Task<int> {
        co_return co_await inner();
    }();

    bool caught = false;
    try {
        outer.get();
    } catch (const std::logic_error&) {
        caught = true;
    }
    ASSERT_TRUE(caught);
}

// ========================================
// 状态查询测试
// ========================================

TEST(CoroTask, DoneState) {
    auto task = []() -> Task<int> {
        co_return 42;
    }();

    ASSERT_FALSE(task.done());  // 惰性执行，尚未开始
    task.resume();
    ASSERT_TRUE(task.done());   // 执行完成
}

TEST(CoroTask, ValidityCheck) {
    Task<int> empty_task;
    ASSERT_FALSE(static_cast<bool>(empty_task));

    auto valid_task = []() -> Task<int> {
        co_return 1;
    }();
    ASSERT_TRUE(static_cast<bool>(valid_task));
}

// ========================================
// 移动语义测试
// ========================================

TEST(CoroTask, MoveConstruction) {
    auto task1 = []() -> Task<int> {
        co_return 99;
    }();

    Task<int> task2 = std::move(task1);
    ASSERT_FALSE(static_cast<bool>(task1));  // task1 应该为空
    ASSERT_TRUE(static_cast<bool>(task2));

    int result = task2.get();
    ASSERT_EQ(result, 99);
}

TEST(CoroTask, MoveAssignment) {
    auto task1 = []() -> Task<int> {
        co_return 100;
    }();

    auto task2 = []() -> Task<int> {
        co_return 200;
    }();

    task2 = std::move(task1);
    ASSERT_FALSE(static_cast<bool>(task1));

    int result = task2.get();
    ASSERT_EQ(result, 100);
}

// ========================================
// 延时等待测试
// ========================================

TEST(CoroTask, SuspendFor) {
    auto task = []() -> Task<int> {
        auto start = std::chrono::steady_clock::now();
        co_await suspend_for(std::chrono::milliseconds{50});
        auto end = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        co_return static_cast<int>(elapsed.count());
    }();

    int elapsed_ms = task.get();
    ASSERT_GE(elapsed_ms, 40);  // 允许一些误差
}

// ========================================
// 多值序列测试
// ========================================

Task<std::vector<int>> collect_values() {
    std::vector<int> result;
    for (int i = 0; i < 5; ++i) {
        result.push_back(i * i);
    }
    co_return result;
}

TEST(CoroTask, ReturnVector) {
    auto task = collect_values();
    auto values = task.get();
    ASSERT_EQ(values.size(), 5u);
    ASSERT_EQ(values[0], 0);
    ASSERT_EQ(values[1], 1);
    ASSERT_EQ(values[2], 4);
    ASSERT_EQ(values[3], 9);
    ASSERT_EQ(values[4], 16);
}

// ========================================
// Main
// ========================================

int main() {
    return TestRunner::instance().run_all();
}
