/**
 * @file coro_generator_test.cpp
 * @brief Generator<T> 单元测试
 */

#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include "../test_framework.h"
#include <corona/kernel/coro/coro.h>

using namespace Corona::Kernel::Coro;
using namespace CoronaTest;

// ========================================
// 基础功能测试
// ========================================

Generator<int> simple_generator() {
    co_yield 1;
    co_yield 2;
    co_yield 3;
}

TEST(CoroGenerator, BasicIteration) {
    auto gen = simple_generator();
    std::vector<int> values;

    for (int n : gen) {
        values.push_back(n);
    }

    ASSERT_EQ(values.size(), 3u);
    ASSERT_EQ(values[0], 1);
    ASSERT_EQ(values[1], 2);
    ASSERT_EQ(values[2], 3);
}

TEST(CoroGenerator, NextMethod) {
    auto gen = simple_generator();

    auto v1 = gen.next();
    ASSERT_TRUE(v1.has_value());
    ASSERT_EQ(*v1, 1);

    auto v2 = gen.next();
    ASSERT_TRUE(v2.has_value());
    ASSERT_EQ(*v2, 2);

    auto v3 = gen.next();
    ASSERT_TRUE(v3.has_value());
    ASSERT_EQ(*v3, 3);

    auto v4 = gen.next();
    ASSERT_FALSE(v4.has_value());  // 生成器已完成
}

// ========================================
// 范围生成器测试
// ========================================

Generator<int> range(int start, int end) {
    for (int i = start; i < end; ++i) {
        co_yield i;
    }
}

TEST(CoroGenerator, Range) {
    auto gen = range(5, 10);
    std::vector<int> values;

    for (int n : gen) {
        values.push_back(n);
    }

    ASSERT_EQ(values.size(), 5u);
    ASSERT_EQ(values[0], 5);
    ASSERT_EQ(values[4], 9);
}

TEST(CoroGenerator, EmptyRange) {
    auto gen = range(10, 10);  // 空范围
    std::vector<int> values;

    for (int n : gen) {
        values.push_back(n);
    }

    ASSERT_EQ(values.size(), 0u);
}

// ========================================
// 斐波那契数列测试
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

TEST(CoroGenerator, Fibonacci) {
    auto gen = fibonacci(10);
    std::vector<int> values;

    for (int n : gen) {
        values.push_back(n);
    }

    // 0, 1, 1, 2, 3, 5, 8, 13, 21, 34
    ASSERT_EQ(values.size(), 10u);
    ASSERT_EQ(values[0], 0);
    ASSERT_EQ(values[1], 1);
    ASSERT_EQ(values[2], 1);
    ASSERT_EQ(values[3], 2);
    ASSERT_EQ(values[4], 3);
    ASSERT_EQ(values[5], 5);
    ASSERT_EQ(values[6], 8);
    ASSERT_EQ(values[7], 13);
    ASSERT_EQ(values[8], 21);
    ASSERT_EQ(values[9], 34);
}

// ========================================
// 字符串生成器测试
// ========================================

Generator<std::string> string_generator() {
    co_yield "Hello";
    co_yield "World";
    co_yield "!";
}

TEST(CoroGenerator, StringYield) {
    auto gen = string_generator();
    std::string result;

    for (const auto& s : gen) {
        result += s;
    }

    ASSERT_EQ(result, "HelloWorld!");
}

// ========================================
// 无限生成器测试
// ========================================

Generator<int> natural_numbers() {
    int n = 0;
    while (true) {
        co_yield n++;
    }
}

TEST(CoroGenerator, InfiniteGenerator) {
    auto gen = natural_numbers();
    std::vector<int> values;

    // 只取前 5 个值
    int count = 0;
    for (int n : gen) {
        values.push_back(n);
        if (++count >= 5) break;
    }

    ASSERT_EQ(values.size(), 5u);
    ASSERT_EQ(values[0], 0);
    ASSERT_EQ(values[4], 4);
}

// ========================================
// 异常传播测试
// ========================================

Generator<int> throwing_generator() {
    co_yield 1;
    co_yield 2;
    throw std::runtime_error("Generator error");
    co_yield 3;  // 不会执行
}

TEST(CoroGenerator, ExceptionPropagation) {
    auto gen = throwing_generator();
    std::vector<int> values;
    bool caught = false;

    try {
        for (int n : gen) {
            values.push_back(n);
        }
    } catch (const std::runtime_error& e) {
        caught = true;
        std::string msg = e.what();
        ASSERT_TRUE(msg.find("Generator error") != std::string::npos);
    }

    ASSERT_TRUE(caught);
    ASSERT_EQ(values.size(), 2u);  // 只收集了前两个值
}

// ========================================
// 移动语义测试
// ========================================

TEST(CoroGenerator, MoveConstruction) {
    auto gen1 = simple_generator();
    Generator<int> gen2 = std::move(gen1);

    ASSERT_FALSE(static_cast<bool>(gen1));  // gen1 应该为空
    ASSERT_TRUE(static_cast<bool>(gen2));

    std::vector<int> values;
    for (int n : gen2) {
        values.push_back(n);
    }
    ASSERT_EQ(values.size(), 3u);
}

TEST(CoroGenerator, MoveAssignment) {
    auto gen1 = range(0, 3);
    auto gen2 = range(10, 15);

    gen2 = std::move(gen1);
    ASSERT_FALSE(static_cast<bool>(gen1));

    std::vector<int> values;
    for (int n : gen2) {
        values.push_back(n);
    }
    ASSERT_EQ(values.size(), 3u);
    ASSERT_EQ(values[0], 0);
}

// ========================================
// 状态查询测试
// ========================================

TEST(CoroGenerator, DoneState) {
    auto gen = range(0, 2);

    ASSERT_FALSE(gen.done());

    auto v1 = gen.next();
    ASSERT_TRUE(v1.has_value());
    ASSERT_FALSE(gen.done());

    auto v2 = gen.next();
    ASSERT_TRUE(v2.has_value());
    ASSERT_FALSE(gen.done());

    auto v3 = gen.next();
    ASSERT_FALSE(v3.has_value());
    ASSERT_TRUE(gen.done());
}

// ========================================
// 复杂类型测试
// ========================================

struct Point {
    int x, y;
};

Generator<Point> point_generator() {
    co_yield Point{1, 2};
    co_yield Point{3, 4};
    co_yield Point{5, 6};
}

TEST(CoroGenerator, StructYield) {
    auto gen = point_generator();
    std::vector<Point> points;

    for (const auto& p : gen) {
        points.push_back(p);
    }

    ASSERT_EQ(points.size(), 3u);
    ASSERT_EQ(points[0].x, 1);
    ASSERT_EQ(points[0].y, 2);
    ASSERT_EQ(points[2].x, 5);
    ASSERT_EQ(points[2].y, 6);
}

// ========================================
// 算法组合测试
// ========================================

Generator<int> filter_even(Generator<int> gen) {
    for (int n : gen) {
        if (n % 2 == 0) {
            co_yield n;
        }
    }
}

TEST(CoroGenerator, FilterComposition) {
    auto gen = filter_even(range(0, 10));
    std::vector<int> values;

    for (int n : gen) {
        values.push_back(n);
    }

    ASSERT_EQ(values.size(), 5u);  // 0, 2, 4, 6, 8
    ASSERT_EQ(values[0], 0);
    ASSERT_EQ(values[1], 2);
    ASSERT_EQ(values[4], 8);
}

Generator<int> map_square(Generator<int> gen) {
    for (int n : gen) {
        co_yield n * n;
    }
}

TEST(CoroGenerator, MapComposition) {
    auto gen = map_square(range(1, 5));
    std::vector<int> values;

    for (int n : gen) {
        values.push_back(n);
    }

    ASSERT_EQ(values.size(), 4u);  // 1, 4, 9, 16
    ASSERT_EQ(values[0], 1);
    ASSERT_EQ(values[1], 4);
    ASSERT_EQ(values[2], 9);
    ASSERT_EQ(values[3], 16);
}

// ========================================
// Main
// ========================================

int main() {
    return TestRunner::instance().run_all();
}
