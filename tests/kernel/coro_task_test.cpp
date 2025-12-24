/**
 * @file coro_task_test.cpp
 * @brief Task<T> 单元测试
 */

#include <atomic>
#include <chrono>
#include <iostream>
#include <limits>
#include <memory>
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
// 引用类型测试
// ========================================

TEST(CoroTask, LvalueReference) {
    std::string shared_data = "original";
    auto task = [&]() -> Task<void> {
        shared_data = "modified";
        co_return;
    }();
    task.get();
    ASSERT_EQ(shared_data, "modified");
}

TEST(CoroTask, CaptureByValue) {
    int value = 100;
    auto task = [value]() -> Task<int> {
        co_return value * 2;
    }();
    int result = task.get();
    ASSERT_EQ(result, 200);
}

// ========================================
// 复杂类型测试
// ========================================

struct ComplexType {
    int id;
    std::string name;
    std::vector<double> data;
    
    bool operator==(const ComplexType& other) const {
        return id == other.id && name == other.name && data == other.data;
    }
};

TEST(CoroTask, ComplexTypeReturn) {
    auto task = []() -> Task<ComplexType> {
        ComplexType result;
        result.id = 42;
        result.name = "Test";
        result.data = {1.0, 2.0, 3.0};
        co_return result;
    }();
    
    auto result = task.get();
    ASSERT_EQ(result.id, 42);
    ASSERT_EQ(result.name, "Test");
    ASSERT_EQ(result.data.size(), 3u);
    ASSERT_EQ(result.data[0], 1.0);
}

TEST(CoroTask, SharedPtrReturn) {
    auto task = []() -> Task<std::shared_ptr<int>> {
        co_return std::make_shared<int>(999);
    }();
    
    auto result = task.get();
    ASSERT_TRUE(result != nullptr);
    ASSERT_EQ(*result, 999);
    ASSERT_EQ(result.use_count(), 1);
}

// ========================================
// 边界条件测试
// ========================================

TEST(CoroTask, EmptyStringReturn) {
    auto task = []() -> Task<std::string> {
        co_return "";
    }();
    std::string result = task.get();
    ASSERT_TRUE(result.empty());
}

TEST(CoroTask, ZeroReturn) {
    auto task = []() -> Task<int> {
        co_return 0;
    }();
    int result = task.get();
    ASSERT_EQ(result, 0);
}

TEST(CoroTask, NegativeReturn) {
    auto task = []() -> Task<int> {
        co_return -42;
    }();
    int result = task.get();
    ASSERT_EQ(result, -42);
}

TEST(CoroTask, LargeValueReturn) {
    auto task = []() -> Task<uint64_t> {
        co_return std::numeric_limits<uint64_t>::max();
    }();
    uint64_t result = task.get();
    ASSERT_EQ(result, std::numeric_limits<uint64_t>::max());
}

// ========================================
// 多异常类型测试
// ========================================

TEST(CoroTask, StdExceptionPropagation) {
    auto task = []() -> Task<int> {
        throw std::invalid_argument("Invalid argument test");
        co_return 0;
    }();
    
    bool caught = false;
    try {
        task.get();
    } catch (const std::invalid_argument& e) {
        caught = true;
        std::string msg = e.what();
        ASSERT_TRUE(msg.find("Invalid argument") != std::string::npos);
    }
    ASSERT_TRUE(caught);
}

TEST(CoroTask, OutOfRangeException) {
    auto task = []() -> Task<int> {
        throw std::out_of_range("Index out of range");
        co_return 0;
    }();
    
    bool caught = false;
    try {
        task.get();
    } catch (const std::out_of_range&) {
        caught = true;
    }
    ASSERT_TRUE(caught);
}

TEST(CoroTask, ExceptionInNestedCall) {
    auto inner = []() -> Task<int> {
        throw std::runtime_error("Deep nested error");
        co_return 0;
    };
    
    auto middle = [&]() -> Task<int> {
        co_return co_await inner();
    };
    
    auto outer = [&]() -> Task<int> {
        co_return co_await middle();
    }();
    
    bool caught = false;
    try {
        outer.get();
    } catch (const std::runtime_error& e) {
        caught = true;
        std::string msg = e.what();
        ASSERT_TRUE(msg.find("Deep nested error") != std::string::npos);
    }
    ASSERT_TRUE(caught);
}

// ========================================
// 协程链测试
// ========================================

Task<int> chain_step(int value, int multiplier) {
    co_return value * multiplier;
}

TEST(CoroTask, CoroutineChain) {
    auto task = []() -> Task<int> {
        int result = 1;
        result = co_await chain_step(result, 2);   // 2
        result = co_await chain_step(result, 3);   // 6
        result = co_await chain_step(result, 4);   // 24
        result = co_await chain_step(result, 5);   // 120
        co_return result;
    }();
    
    int result = task.get();
    ASSERT_EQ(result, 120);  // 5! = 120
}

Task<std::string> string_chain(std::string prefix) {
    co_return prefix + "!";
}

TEST(CoroTask, StringCoroutineChain) {
    auto task = []() -> Task<std::string> {
        std::string result = "Hello";
        result = co_await string_chain(result);
        result = co_await string_chain(result);
        result = co_await string_chain(result);
        co_return result;
    }();
    
    std::string result = task.get();
    ASSERT_EQ(result, "Hello!!!");
}

// ========================================
// 条件分支测试
// ========================================

Task<int> conditional_task(bool flag) {
    if (flag) {
        co_return 100;
    } else {
        co_return 200;
    }
}

TEST(CoroTask, ConditionalBranch) {
    auto task1 = conditional_task(true);
    auto task2 = conditional_task(false);
    
    ASSERT_EQ(task1.get(), 100);
    ASSERT_EQ(task2.get(), 200);
}

Task<int> early_return_task(int value) {
    if (value < 0) {
        co_return 0;  // 早期返回
    }
    int result = value * 2;
    if (result > 100) {
        co_return 100;  // 另一个早期返回
    }
    co_return result;
}

TEST(CoroTask, EarlyReturn) {
    auto task1 = early_return_task(-5);
    auto task2 = early_return_task(30);
    auto task3 = early_return_task(100);
    
    ASSERT_EQ(task1.get(), 0);
    ASSERT_EQ(task2.get(), 60);
    ASSERT_EQ(task3.get(), 100);
}

// ========================================
// 循环内await测试
// ========================================

Task<int> accumulate_async(int count) {
    int sum = 0;
    for (int i = 1; i <= count; ++i) {
        sum += co_await nested_add(sum, i);  // 使用已有的 nested_add
    }
    co_return sum;
}

TEST(CoroTask, LoopWithAwait) {
    auto task = accumulate_async(5);
    int result = task.get();
    // sum = (0+1) + ((0+1)+2) + ... 
    // 这是累加逻辑：1, 1+2=3, 3+3=6, 6+4=10, 10+5=15 -> 总和 1+3+6+10+15=35
    // 实际逻辑: sum从0开始，每次 sum = sum + (sum + i) = 2*sum + i
    // i=1: sum = 0 + (0+1) = 1
    // i=2: sum = 1 + (1+2) = 4
    // i=3: sum = 4 + (4+3) = 11
    // i=4: sum = 11 + (11+4) = 26
    // i=5: sum = 26 + (26+5) = 57
    ASSERT_EQ(result, 57);
}

// ========================================
// 多重移动测试
// ========================================

TEST(CoroTask, MultipleMove) {
    auto task1 = []() -> Task<int> {
        co_return 42;
    }();
    
    Task<int> task2 = std::move(task1);
    Task<int> task3 = std::move(task2);
    Task<int> task4 = std::move(task3);
    
    ASSERT_FALSE(static_cast<bool>(task1));
    ASSERT_FALSE(static_cast<bool>(task2));
    ASSERT_FALSE(static_cast<bool>(task3));
    ASSERT_TRUE(static_cast<bool>(task4));
    
    ASSERT_EQ(task4.get(), 42);
}

// ========================================
// Void Task 异常测试
// ========================================

TEST(CoroTask, VoidTaskException) {
    auto task = []() -> Task<void> {
        throw std::runtime_error("Void task error");
        co_return;
    }();
    
    bool caught = false;
    try {
        task.get();
    } catch (const std::runtime_error& e) {
        caught = true;
        std::string msg = e.what();
        ASSERT_TRUE(msg.find("Void task error") != std::string::npos);
    }
    ASSERT_TRUE(caught);
}

// ========================================
// 嵌套 Void Task 测试
// ========================================

TEST(CoroTask, NestedVoidTasks) {
    int counter = 0;
    
    auto increment = [&]() -> Task<void> {
        counter++;
        co_return;
    };
    
    auto outer = [&]() -> Task<void> {
        co_await increment();
        co_await increment();
        co_await increment();
        co_return;
    }();
    
    outer.get();
    ASSERT_EQ(counter, 3);
}

// ========================================
// 协程重用测试（创建多个同类型任务）
// ========================================

Task<int> reusable_task(int n) {
    co_return n * n;
}

TEST(CoroTask, MultipleTaskInstances) {
    std::vector<Task<int>> tasks;
    for (int i = 0; i < 10; ++i) {
        tasks.push_back(reusable_task(i));
    }
    
    for (int i = 0; i < 10; ++i) {
        int result = tasks[i].get();
        ASSERT_EQ(result, i * i);
    }
}

// ========================================
// Main
// ========================================

int main() {
    return TestRunner::instance().run_all();
}
