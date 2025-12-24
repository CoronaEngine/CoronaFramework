/**
 * @file main.cpp
 * @brief Corona Framework 协程模块示例
 *
 * 展示 C++20 协程封装的基本用法
 */

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

#include <corona/kernel/coro/coro.h>

using namespace Corona::Kernel::Coro;

// ========================================
// 示例 1: 基本异步任务
// ========================================

Task<int> async_add(int a, int b) {
    // 模拟异步操作
    co_await suspend_for(std::chrono::milliseconds{10});
    co_return a + b;
}

Task<int> compute_sum() {
    std::cout << "[Task] Starting computation..." << std::endl;

    int x = co_await async_add(1, 2);
    std::cout << "[Task] x = " << x << std::endl;

    int y = co_await async_add(3, 4);
    std::cout << "[Task] y = " << y << std::endl;

    int z = co_await async_add(x, y);
    std::cout << "[Task] z = " << z << std::endl;

    co_return z;
}

// ========================================
// 示例 2: 生成器
// ========================================

Generator<int> fibonacci(int count) {
    int a = 0, b = 1;
    for (int i = 0; i < count; ++i) {
        co_yield a;
        int next = a + b;
        a = b;
        b = next;
    }
}

Generator<int> range(int start, int end) {
    for (int i = start; i < end; ++i) {
        co_yield i;
    }
}

Generator<int> filter_even(Generator<int> gen) {
    for (int n : gen) {
        if (n % 2 == 0) {
            co_yield n;
        }
    }
}

// ========================================
// 示例 3: 异常处理
// ========================================

Task<int> may_fail(bool should_fail) {
    co_await suspend_for(std::chrono::milliseconds{5});
    if (should_fail) {
        throw std::runtime_error("Something went wrong!");
    }
    co_return 42;
}

Task<std::string> handle_error() {
    try {
        int result = co_await may_fail(true);
        co_return "Success: " + std::to_string(result);
    } catch (const std::exception& e) {
        co_return std::string("Error: ") + e.what();
    }
}

// ========================================
// 示例 4: 条件等待
// ========================================

Task<void> wait_for_condition() {
    int counter = 0;

    std::cout << "[Wait] Waiting for counter to reach 5..." << std::endl;

    co_await wait_until([&counter]() {
        ++counter;
        return counter >= 5;
    }, std::chrono::milliseconds{10});

    std::cout << "[Wait] Counter reached: " << counter << std::endl;
}

// ========================================
// Main
// ========================================

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Corona Framework Coroutine Examples" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    // 示例 1: 异步任务
    std::cout << "--- Example 1: Async Task ---" << std::endl;
    {
        auto task = compute_sum();
        int result = task.get();
        std::cout << "[Main] Final result: " << result << std::endl;
    }
    std::cout << std::endl;

    // 示例 2: 生成器
    std::cout << "--- Example 2: Generator ---" << std::endl;
    {
        std::cout << "Fibonacci sequence: ";
        for (int n : fibonacci(10)) {
            std::cout << n << " ";
        }
        std::cout << std::endl;

        std::cout << "Even numbers in range [0, 10): ";
        for (int n : filter_even(range(0, 10))) {
            std::cout << n << " ";
        }
        std::cout << std::endl;
    }
    std::cout << std::endl;

    // 示例 3: 异常处理
    std::cout << "--- Example 3: Exception Handling ---" << std::endl;
    {
        auto task = handle_error();
        std::string result = task.get();
        std::cout << "[Main] " << result << std::endl;
    }
    std::cout << std::endl;

    // 示例 4: 条件等待
    std::cout << "--- Example 4: Wait Until ---" << std::endl;
    {
        auto task = wait_for_condition();
        task.get();
    }
    std::cout << std::endl;

    // 示例 5: 使用 Runner
    std::cout << "--- Example 5: Using Runner ---" << std::endl;
    {
        int result = Runner::run(async_add(100, 200));
        std::cout << "[Runner] Result: " << result << std::endl;
    }
    std::cout << std::endl;

    std::cout << "========================================" << std::endl;
    std::cout << "All examples completed!" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
