/**
 * @file coro_generator_test.cpp
 * @brief Generator<T> 单元测试
 */

#include <iostream>
#include <memory>
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
// 嵌套生成器测试
// ========================================

Generator<int> nested_gen_inner(int start, int count) {
    for (int i = 0; i < count; ++i) {
        co_yield start + i;
    }
}

Generator<int> flatten_generators() {
    // 顺序消费多个生成器
    for (int n : nested_gen_inner(0, 3)) {
        co_yield n;
    }
    for (int n : nested_gen_inner(10, 3)) {
        co_yield n;
    }
    for (int n : nested_gen_inner(100, 3)) {
        co_yield n;
    }
}

TEST(CoroGenerator, FlattenNestedGenerators) {
    auto gen = flatten_generators();
    std::vector<int> values;

    for (int n : gen) {
        values.push_back(n);
    }

    ASSERT_EQ(values.size(), 9u);
    // 第一组: 0, 1, 2
    ASSERT_EQ(values[0], 0);
    ASSERT_EQ(values[1], 1);
    ASSERT_EQ(values[2], 2);
    // 第二组: 10, 11, 12
    ASSERT_EQ(values[3], 10);
    ASSERT_EQ(values[4], 11);
    ASSERT_EQ(values[5], 12);
    // 第三组: 100, 101, 102
    ASSERT_EQ(values[6], 100);
    ASSERT_EQ(values[7], 101);
    ASSERT_EQ(values[8], 102);
}

// ========================================
// 大数据量测试
// ========================================

Generator<int> large_sequence(int count) {
    for (int i = 0; i < count; ++i) {
        co_yield i;
    }
}

TEST(CoroGenerator, LargeSequence) {
    constexpr int count = 10000;
    auto gen = large_sequence(count);
    
    int sum = 0;
    int item_count = 0;
    for (int n : gen) {
        sum += n;
        item_count++;
    }
    
    ASSERT_EQ(item_count, count);
    // 0 + 1 + 2 + ... + (count-1) = count*(count-1)/2
    ASSERT_EQ(sum, count * (count - 1) / 2);
}

TEST(CoroGenerator, PartialConsumption) {
    auto gen = large_sequence(10000);
    
    // 只消费前100个
    int count = 0;
    for (int n : gen) {
        (void)n;
        if (++count >= 100) break;
    }
    
    ASSERT_EQ(count, 100);
}

// ========================================
// 复杂类型生成器测试
// ========================================

struct DataItem {
    int id;
    std::string name;
    double value;
};

Generator<DataItem> data_generator(int count) {
    for (int i = 0; i < count; ++i) {
        DataItem item;
        item.id = i;
        item.name = "Item_" + std::to_string(i);
        item.value = i * 1.5;
        co_yield item;
    }
}

TEST(CoroGenerator, ComplexTypeGenerator) {
    auto gen = data_generator(5);
    std::vector<DataItem> items;

    for (const auto& item : gen) {
        items.push_back(item);
    }

    ASSERT_EQ(items.size(), 5u);
    ASSERT_EQ(items[0].id, 0);
    ASSERT_EQ(items[0].name, "Item_0");
    ASSERT_EQ(items[2].id, 2);
    ASSERT_EQ(items[2].value, 3.0);
    ASSERT_EQ(items[4].name, "Item_4");
}

// ========================================
// Unique Ptr 生成器测试
// ========================================

Generator<std::unique_ptr<int>> unique_ptr_generator(int count) {
    for (int i = 0; i < count; ++i) {
        co_yield std::make_unique<int>(i * 10);
    }
}

TEST(CoroGenerator, UniquePtrGenerator) {
    auto gen = unique_ptr_generator(5);
    std::vector<int> values;

    for (auto& ptr : gen) {
        values.push_back(*ptr);
    }

    ASSERT_EQ(values.size(), 5u);
    ASSERT_EQ(values[0], 0);
    ASSERT_EQ(values[1], 10);
    ASSERT_EQ(values[2], 20);
    ASSERT_EQ(values[3], 30);
    ASSERT_EQ(values[4], 40);
}

// ========================================
// 条件生成测试
// ========================================

Generator<int> conditional_yield(int max) {
    for (int i = 0; i <= max; ++i) {
        if (i % 3 == 0) {  // 只 yield 3 的倍数
            co_yield i;
        }
    }
}

TEST(CoroGenerator, ConditionalYield) {
    auto gen = conditional_yield(15);
    std::vector<int> values;

    for (int n : gen) {
        values.push_back(n);
    }

    // 0, 3, 6, 9, 12, 15
    ASSERT_EQ(values.size(), 6u);
    ASSERT_EQ(values[0], 0);
    ASSERT_EQ(values[1], 3);
    ASSERT_EQ(values[2], 6);
    ASSERT_EQ(values[5], 15);
}

// ========================================
// take 组合器测试
// ========================================

Generator<int> take(Generator<int> gen, int n) {
    int count = 0;
    for (int value : gen) {
        if (count++ >= n) break;
        co_yield value;
    }
}

TEST(CoroGenerator, TakeComposition) {
    auto gen = take(natural_numbers(), 7);
    std::vector<int> values;

    for (int n : gen) {
        values.push_back(n);
    }

    ASSERT_EQ(values.size(), 7u);
    for (int i = 0; i < 7; ++i) {
        ASSERT_EQ(values[i], i);
    }
}

// ========================================
// skip 组合器测试
// ========================================

Generator<int> skip(Generator<int> gen, int n) {
    int count = 0;
    for (int value : gen) {
        if (count++ < n) continue;
        co_yield value;
    }
}

TEST(CoroGenerator, SkipComposition) {
    auto gen = skip(range(0, 10), 5);
    std::vector<int> values;

    for (int n : gen) {
        values.push_back(n);
    }

    ASSERT_EQ(values.size(), 5u);  // 5, 6, 7, 8, 9
    ASSERT_EQ(values[0], 5);
    ASSERT_EQ(values[4], 9);
}

// ========================================
// 组合器链测试
// ========================================

TEST(CoroGenerator, CompositionChain) {
    // take(skip(filter_even(range(0, 100)), 2), 5)
    // range: 0, 1, 2, ..., 99
    // filter_even: 0, 2, 4, 6, 8, 10, 12, 14, ...
    // skip 2: 4, 6, 8, 10, 12, 14, ...
    // take 5: 4, 6, 8, 10, 12
    auto gen = take(skip(filter_even(range(0, 100)), 2), 5);
    std::vector<int> values;

    for (int n : gen) {
        values.push_back(n);
    }

    ASSERT_EQ(values.size(), 5u);
    ASSERT_EQ(values[0], 4);
    ASSERT_EQ(values[1], 6);
    ASSERT_EQ(values[2], 8);
    ASSERT_EQ(values[3], 10);
    ASSERT_EQ(values[4], 12);
}

// ========================================
// 空值边界测试
// ========================================

Generator<std::string> empty_string_generator() {
    co_yield "";
    co_yield "non-empty";
    co_yield "";
}

TEST(CoroGenerator, EmptyStringYield) {
    auto gen = empty_string_generator();
    std::vector<std::string> values;

    for (const auto& s : gen) {
        values.push_back(s);
    }

    ASSERT_EQ(values.size(), 3u);
    ASSERT_TRUE(values[0].empty());
    ASSERT_EQ(values[1], "non-empty");
    ASSERT_TRUE(values[2].empty());
}

// ========================================
// 单元素生成器测试
// ========================================

Generator<int> single_element_gen() {
    co_yield 42;
}

TEST(CoroGenerator, SingleElement) {
    auto gen = single_element_gen();
    
    auto val = gen.next();
    ASSERT_TRUE(val.has_value());
    ASSERT_EQ(*val, 42);
    
    val = gen.next();
    ASSERT_FALSE(val.has_value());
    ASSERT_TRUE(gen.done());
}

// ========================================
// 异常在不同位置测试
// ========================================

Generator<int> exception_at_start() {
    throw std::runtime_error("Error at start");
    co_yield 1;  // 不会执行
}

TEST(CoroGenerator, ExceptionAtStart) {
    auto gen = exception_at_start();
    bool caught = false;

    try {
        for (int n : gen) {
            (void)n;
        }
    } catch (const std::runtime_error& e) {
        caught = true;
        std::string msg = e.what();
        ASSERT_TRUE(msg.find("start") != std::string::npos);
    }

    ASSERT_TRUE(caught);
}

Generator<int> exception_at_end(int count) {
    for (int i = 0; i < count; ++i) {
        co_yield i;
    }
    throw std::runtime_error("Error at end");
}

TEST(CoroGenerator, ExceptionAtEnd) {
    auto gen = exception_at_end(3);
    std::vector<int> values;
    bool caught = false;

    try {
        for (int n : gen) {
            values.push_back(n);
        }
    } catch (const std::runtime_error& e) {
        caught = true;
        std::string msg = e.what();
        ASSERT_TRUE(msg.find("end") != std::string::npos);
    }

    ASSERT_TRUE(caught);
    ASSERT_EQ(values.size(), 3u);  // 成功收集了前3个值
}

// ========================================
// 迭代器相等性测试
// ========================================

TEST(CoroGenerator, IteratorEquality) {
    auto gen = range(0, 3);
    auto it = gen.begin();
    auto end = gen.end();
    
    ASSERT_NE(it, end);
    ++it;
    ASSERT_NE(it, end);
    ++it;
    ASSERT_NE(it, end);
    ++it;
    ASSERT_EQ(it, end);
}

// ========================================
// 多次调用 begin 测试
// ========================================

TEST(CoroGenerator, BeginCalledOnce) {
    auto gen = range(0, 5);
    
    // 第一次调用 begin
    auto it1 = gen.begin();
    ASSERT_EQ(*it1, 0);
    ++it1;
    ASSERT_EQ(*it1, 1);
}

// ========================================
// 生成器有效性测试
// ========================================

TEST(CoroGenerator, ValidityAfterConsumption) {
    auto gen = range(0, 3);
    
    ASSERT_TRUE(static_cast<bool>(gen));
    ASSERT_FALSE(gen.done());
    
    // 完全消费
    for (int n : gen) {
        (void)n;
    }
    
    ASSERT_TRUE(static_cast<bool>(gen));  // 仍然有效
    ASSERT_TRUE(gen.done());               // 但已完成
}

// ========================================
// 重复值生成器测试
// ========================================

Generator<int> repeat(int value, int times) {
    for (int i = 0; i < times; ++i) {
        co_yield value;
    }
}

TEST(CoroGenerator, RepeatGenerator) {
    auto gen = repeat(42, 5);
    std::vector<int> values;

    for (int n : gen) {
        values.push_back(n);
    }

    ASSERT_EQ(values.size(), 5u);
    for (int v : values) {
        ASSERT_EQ(v, 42);
    }
}

// ========================================
// 交替值生成器测试
// ========================================

Generator<int> alternate(int a, int b, int count) {
    for (int i = 0; i < count; ++i) {
        co_yield (i % 2 == 0) ? a : b;
    }
}

TEST(CoroGenerator, AlternateGenerator) {
    auto gen = alternate(1, 2, 6);
    std::vector<int> values;

    for (int n : gen) {
        values.push_back(n);
    }

    ASSERT_EQ(values.size(), 6u);
    ASSERT_EQ(values[0], 1);
    ASSERT_EQ(values[1], 2);
    ASSERT_EQ(values[2], 1);
    ASSERT_EQ(values[3], 2);
    ASSERT_EQ(values[4], 1);
    ASSERT_EQ(values[5], 2);
}

// ========================================
// Main
// ========================================

int main() {
    return TestRunner::instance().run_all();
}
