#include "corona/kernel/memory/cache_aligned_allocator.h"

#include <atomic>
#include <thread>
#include <vector>

#include "../test_framework.h"

using namespace Corona::Kernal::Memory;

// 测试辅助函数：检查指针是否按指定对齐
template <typename T>
bool check_alignment(T* ptr, std::size_t alignment) {
    return is_aligned(ptr, alignment);
}

// ========================================
// 对齐辅助函数测试
// ========================================

TEST(CacheAlignedAllocatorTests, AlignUpFunction) {
    ASSERT_EQ(align_up(0, 64), 0);
    ASSERT_EQ(align_up(1, 64), 64);
    ASSERT_EQ(align_up(63, 64), 64);
    ASSERT_EQ(align_up(64, 64), 64);
    ASSERT_EQ(align_up(65, 64), 128);
    ASSERT_EQ(align_up(127, 64), 128);
    ASSERT_EQ(align_up(128, 64), 128);

    // 测试不同的对齐大小
    ASSERT_EQ(align_up(10, 16), 16);
    ASSERT_EQ(align_up(32, 32), 32);
    ASSERT_EQ(align_up(33, 32), 64);
}

TEST(CacheAlignedAllocatorTests, AlignDownFunction) {
    ASSERT_EQ(align_down(0, 64), 0);
    ASSERT_EQ(align_down(1, 64), 0);
    ASSERT_EQ(align_down(63, 64), 0);
    ASSERT_EQ(align_down(64, 64), 64);
    ASSERT_EQ(align_down(65, 64), 64);
    ASSERT_EQ(align_down(127, 64), 64);
    ASSERT_EQ(align_down(128, 64), 128);

    // 测试不同的对齐大小
    ASSERT_EQ(align_down(10, 16), 0);
    ASSERT_EQ(align_down(32, 32), 32);
    ASSERT_EQ(align_down(63, 32), 32);
}

TEST(CacheAlignedAllocatorTests, IsPowerOfTwoFunction) {
    ASSERT_FALSE(is_power_of_two(0));
    ASSERT_TRUE(is_power_of_two(1));
    ASSERT_TRUE(is_power_of_two(2));
    ASSERT_FALSE(is_power_of_two(3));
    ASSERT_TRUE(is_power_of_two(4));
    ASSERT_FALSE(is_power_of_two(5));
    ASSERT_TRUE(is_power_of_two(8));
    ASSERT_TRUE(is_power_of_two(16));
    ASSERT_TRUE(is_power_of_two(32));
    ASSERT_TRUE(is_power_of_two(64));
    ASSERT_TRUE(is_power_of_two(128));
    ASSERT_TRUE(is_power_of_two(256));
    ASSERT_FALSE(is_power_of_two(255));
    ASSERT_TRUE(is_power_of_two(1024));
    ASSERT_FALSE(is_power_of_two(1000));
}

// ========================================
// 基础分配器测试
// ========================================

TEST(CacheAlignedAllocatorTests, BasicAllocation) {
    CacheAlignedAllocator<int, 64> allocator;

    // 分配单个元素
    int* ptr = allocator.allocate(1);
    ASSERT_TRUE(ptr != nullptr);
    ASSERT_TRUE(check_alignment(ptr, 64));

    // 使用分配的内存
    *ptr = 42;
    ASSERT_EQ(*ptr, 42);

    // 释放内存
    allocator.deallocate(ptr, 1);
}

TEST(CacheAlignedAllocatorTests, MultipleAllocation) {
    CacheAlignedAllocator<int, 64> allocator;

    // 分配多个元素
    constexpr std::size_t count = 10;
    int* ptr = allocator.allocate(count);
    ASSERT_TRUE(ptr != nullptr);
    ASSERT_TRUE(check_alignment(ptr, 64));

    // 初始化和验证所有元素
    for (std::size_t i = 0; i < count; ++i) {
        ptr[i] = static_cast<int>(i * 10);
    }

    for (std::size_t i = 0; i < count; ++i) {
        ASSERT_EQ(ptr[i], static_cast<int>(i * 10));
    }

    // 释放内存
    allocator.deallocate(ptr, count);
}

TEST(CacheAlignedAllocatorTests, LargeAllocation) {
    CacheAlignedAllocator<int, 64> allocator;

    // 分配大量元素
    constexpr std::size_t large_count = 10000;
    int* ptr = allocator.allocate(large_count);
    ASSERT_TRUE(ptr != nullptr);
    ASSERT_TRUE(check_alignment(ptr, 64));

    // 简单验证
    ptr[0] = 1;
    ptr[large_count - 1] = 2;
    ASSERT_EQ(ptr[0], 1);
    ASSERT_EQ(ptr[large_count - 1], 2);

    allocator.deallocate(ptr, large_count);
}

TEST(CacheAlignedAllocatorTests, ZeroSizeAllocation) {
    CacheAlignedAllocator<int, 64> allocator;

    // 零大小分配应该返回有效指针（内部转换为1）
    int* ptr = allocator.allocate(0);
    ASSERT_TRUE(ptr != nullptr);
    ASSERT_TRUE(check_alignment(ptr, 64));

    allocator.deallocate(ptr, 0);
}

// ========================================
// 不同类型测试
// ========================================

TEST(CacheAlignedAllocatorTests, DoubleType) {
    CacheAlignedAllocator<double, 64> allocator;

    constexpr std::size_t count = 5;
    double* ptr = allocator.allocate(count);
    ASSERT_TRUE(ptr != nullptr);
    ASSERT_TRUE(check_alignment(ptr, 64));

    for (std::size_t i = 0; i < count; ++i) {
        ptr[i] = static_cast<double>(i) * 3.14;
    }

    for (std::size_t i = 0; i < count; ++i) {
        ASSERT_EQ(ptr[i], static_cast<double>(i) * 3.14);
    }

    allocator.deallocate(ptr, count);
}

TEST(CacheAlignedAllocatorTests, StructType) {
    struct TestStruct {
        int a;
        double b;
        char c[16];
    };

    CacheAlignedAllocator<TestStruct, 64> allocator;

    TestStruct* ptr = allocator.allocate(3);
    ASSERT_TRUE(ptr != nullptr);
    ASSERT_TRUE(check_alignment(ptr, 64));

    ptr[0].a = 10;
    ptr[0].b = 3.14;
    ptr[1].a = 20;
    ptr[1].b = 2.71;

    ASSERT_EQ(ptr[0].a, 10);
    ASSERT_EQ(ptr[0].b, 3.14);
    ASSERT_EQ(ptr[1].a, 20);
    ASSERT_EQ(ptr[1].b, 2.71);

    allocator.deallocate(ptr, 3);
}

// ========================================
// 不同对齐大小测试
// ========================================

TEST(CacheAlignedAllocatorTests, Alignment16) {
    CacheAlignedAllocator<int, 16> allocator;

    int* ptr = allocator.allocate(4);
    ASSERT_TRUE(ptr != nullptr);
    ASSERT_TRUE(check_alignment(ptr, 16));

    allocator.deallocate(ptr, 4);
}

TEST(CacheAlignedAllocatorTests, Alignment128) {
    CacheAlignedAllocator<int, 128> allocator;

    int* ptr = allocator.allocate(4);
    ASSERT_TRUE(ptr != nullptr);
    ASSERT_TRUE(check_alignment(ptr, 128));

    allocator.deallocate(ptr, 4);
}

TEST(CacheAlignedAllocatorTests, Alignment256) {
    CacheAlignedAllocator<int, 256> allocator;

    int* ptr = allocator.allocate(4);
    ASSERT_TRUE(ptr != nullptr);
    ASSERT_TRUE(check_alignment(ptr, 256));

    allocator.deallocate(ptr, 4);
}

// ========================================
// 与标准容器集成测试
// ========================================

TEST(CacheAlignedAllocatorTests, WithStdVector) {
    std::vector<int, CacheAlignedAllocator<int, 64>> vec;

    // 添加元素
    for (int i = 0; i < 100; ++i) {
        vec.push_back(i);
    }

    // 验证元素
    ASSERT_EQ(vec.size(), 100);
    for (int i = 0; i < 100; ++i) {
        ASSERT_EQ(vec[i], i);
    }

    // 验证首元素对齐
    ASSERT_TRUE(check_alignment(vec.data(), 64));
}

TEST(CacheAlignedAllocatorTests, VectorReallocation) {
    std::vector<double, CacheAlignedAllocator<double, 64>> vec;
    vec.reserve(10);

    // 记录初始容量
    auto initial_capacity = vec.capacity();
    ASSERT_GE(initial_capacity, 10);

    // 触发重新分配
    for (int i = 0; i < 100; ++i) {
        vec.push_back(static_cast<double>(i));
    }

    // 验证重新分配后仍然对齐
    ASSERT_TRUE(check_alignment(vec.data(), 64));
    ASSERT_EQ(vec.size(), 100);

    // 验证数据完整性
    for (int i = 0; i < 100; ++i) {
        ASSERT_EQ(vec[i], static_cast<double>(i));
    }
}

// ========================================
// 分配器比较测试
// ========================================

TEST(CacheAlignedAllocatorTests, AllocatorEquality) {
    CacheAlignedAllocator<int, 64> alloc1;
    CacheAlignedAllocator<int, 64> alloc2;
    CacheAlignedAllocator<int, 128> alloc3;

    // 相同对齐的分配器应该相等
    ASSERT_TRUE(alloc1 == alloc2);
    ASSERT_FALSE(alloc1 != alloc2);

    // 不同对齐的分配器应该不相等
    ASSERT_FALSE(alloc1 == alloc3);
    ASSERT_TRUE(alloc1 != alloc3);
}

TEST(CacheAlignedAllocatorTests, AllocatorRebind) {
    CacheAlignedAllocator<int, 64> int_alloc;

    // Rebind 到 double
    using DoubleAlloc = CacheAlignedAllocator<int, 64>::rebind<double>::other;
    DoubleAlloc double_alloc(int_alloc);

    // 分配并测试 double
    double* ptr = double_alloc.allocate(5);
    ASSERT_TRUE(ptr != nullptr);
    ASSERT_TRUE(check_alignment(ptr, 64));

    ptr[0] = 1.5;
    ASSERT_EQ(ptr[0], 1.5);

    double_alloc.deallocate(ptr, 5);
}

// ========================================
// 多线程测试
// ========================================

TEST(CacheAlignedAllocatorTests, ConcurrentAllocation) {
    constexpr int num_threads = 8;
    constexpr int allocations_per_thread = 100;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    auto worker = [&success_count]() {
        CacheAlignedAllocator<int, 64> allocator;

        for (int i = 0; i < allocations_per_thread; ++i) {
            int* ptr = allocator.allocate(10);
            if (ptr && check_alignment(ptr, 64)) {
                // 使用内存
                for (int j = 0; j < 10; ++j) {
                    ptr[j] = i * 10 + j;
                }

                // 验证
                bool all_correct = true;
                for (int j = 0; j < 10; ++j) {
                    if (ptr[j] != i * 10 + j) {
                        all_correct = false;
                        break;
                    }
                }

                if (all_correct) {
                    success_count.fetch_add(1, std::memory_order_relaxed);
                }

                allocator.deallocate(ptr, 10);
            }
        }
    };

    // 启动线程
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker);
    }

    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }

    // 验证所有分配都成功
    ASSERT_EQ(success_count.load(), num_threads * allocations_per_thread);
}

TEST(CacheAlignedAllocatorTests, ConcurrentVectorOperations) {
    constexpr int num_threads = 4;
    std::vector<std::thread> threads;
    std::atomic<bool> start{false};

    auto worker = [&start]() {
        // 等待开始信号
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        std::vector<int, CacheAlignedAllocator<int, 64>> local_vec;

        // 执行一些操作
        for (int i = 0; i < 1000; ++i) {
            local_vec.push_back(i);
        }

        // 验证数据
        for (int i = 0; i < 1000; ++i) {
            if (local_vec[i] != i) {
                throw std::runtime_error("Data corruption detected");
            }
        }
    };

    // 创建线程
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker);
    }

    // 启动所有线程
    start.store(true, std::memory_order_release);

    // 等待完成
    for (auto& t : threads) {
        t.join();
    }
}

// ========================================
// 边界情况测试
// ========================================

TEST(CacheAlignedAllocatorTests, IsAlignedFunction) {
    CacheAlignedAllocator<int, 64> allocator;

    int* ptr = allocator.allocate(1);
    ASSERT_TRUE(is_aligned(ptr, 64));
    ASSERT_TRUE(is_aligned(ptr, 32));
    ASSERT_TRUE(is_aligned(ptr, 16));
    ASSERT_TRUE(is_aligned(ptr, 8));

    allocator.deallocate(ptr, 1);
}

TEST(CacheAlignedAllocatorTests, MultipleAllocationsAlignment) {
    CacheAlignedAllocator<int, 64> allocator;
    std::vector<int*> pointers;

    // 分配多个块
    for (int i = 0; i < 10; ++i) {
        int* ptr = allocator.allocate(5);
        ASSERT_TRUE(ptr != nullptr);
        ASSERT_TRUE(check_alignment(ptr, 64));
        pointers.push_back(ptr);
    }

    // 释放所有块
    for (auto ptr : pointers) {
        allocator.deallocate(ptr, 5);
    }
}

TEST(CacheAlignedAllocatorTests, AllocateDeallocateLoop) {
    CacheAlignedAllocator<int, 64> allocator;

    // 多次分配和释放
    for (int iteration = 0; iteration < 100; ++iteration) {
        int* ptr = allocator.allocate(10);
        ASSERT_TRUE(ptr != nullptr);
        ASSERT_TRUE(check_alignment(ptr, 64));

        // 使用内存
        for (int i = 0; i < 10; ++i) {
            ptr[i] = iteration * 10 + i;
        }

        // 验证
        for (int i = 0; i < 10; ++i) {
            ASSERT_EQ(ptr[i], iteration * 10 + i);
        }

        allocator.deallocate(ptr, 10);
    }
}

// ========================================
// 性能相关测试
// ========================================

TEST(CacheAlignedAllocatorTests, CacheLineSize) {
    // 验证缓存行大小是合理的
    ASSERT_TRUE(CacheLineSize == 64 || CacheLineSize == 128);
    ASSERT_TRUE(is_power_of_two(CacheLineSize));
}

// ========================================
// 主函数
// ========================================

int main() {
    return CoronaTest::TestRunner::instance().run_all();
}
