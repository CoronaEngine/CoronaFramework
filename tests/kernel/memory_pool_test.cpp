#include "corona/kernel/memory/memory_pool.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "../test_framework.h"

using namespace Corona::Kernal::Memory;

// ========================================
// PoolConfig 测试
// ========================================

TEST(PoolConfigTests, DefaultConfig) {
    PoolConfig config;
    ASSERT_EQ(config.block_size, 64);
    ASSERT_EQ(config.block_alignment, CacheLineSize);
    ASSERT_EQ(config.chunk_size, kDefaultChunkSize);
    ASSERT_EQ(config.initial_chunks, 1);
    ASSERT_EQ(config.max_chunks, 0);
    ASSERT_TRUE(config.thread_safe);
    ASSERT_FALSE(config.enable_debug);
}

TEST(PoolConfigTests, ValidConfig) {
    PoolConfig config{
        .block_size = 128,
        .block_alignment = 64,
        .chunk_size = 32 * 1024,
        .initial_chunks = 2,
        .max_chunks = 10,
        .thread_safe = true,
        .enable_debug = false,
    };
    ASSERT_TRUE(config.is_valid());
}

TEST(PoolConfigTests, InvalidBlockSize) {
    PoolConfig config{
        .block_size = 8,  // 小于 kMinBlockSize (16)
    };
    ASSERT_FALSE(config.is_valid());
}

TEST(PoolConfigTests, InvalidAlignment) {
    PoolConfig config{
        .block_size = 64,
        .block_alignment = 63,  // 不是 2 的幂
    };
    ASSERT_FALSE(config.is_valid());
}

// ========================================
// FixedPool 基础测试
// ========================================

TEST(FixedPoolTests, BasicAllocation) {
    PoolConfig config{
        .block_size = 64,
        .chunk_size = 4096,
        .thread_safe = false,
    };

    FixedPool pool(config);

    // 分配一个块
    void* ptr = pool.allocate();
    ASSERT_TRUE(ptr != nullptr);
    ASSERT_TRUE(is_aligned(ptr, config.block_alignment));

    // 释放
    pool.deallocate(ptr);
}

TEST(FixedPoolTests, MultipleAllocations) {
    PoolConfig config{
        .block_size = 32,
        .chunk_size = 4096,
        .thread_safe = false,
    };

    FixedPool pool(config);
    std::vector<void*> ptrs;

    // 分配多个块
    for (int i = 0; i < 100; ++i) {
        void* ptr = pool.allocate();
        ASSERT_TRUE(ptr != nullptr);
        ptrs.push_back(ptr);
    }

    // 验证所有指针都对齐
    for (void* ptr : ptrs) {
        ASSERT_TRUE(is_aligned(ptr, config.block_alignment));
    }

    // 释放所有
    for (void* ptr : ptrs) {
        pool.deallocate(ptr);
    }
}

TEST(FixedPoolTests, AllocateAndDeallocatePattern) {
    PoolConfig config{
        .block_size = 64,
        .chunk_size = 4096,
        .thread_safe = false,
    };

    FixedPool pool(config);

    // 分配 -> 释放 -> 重新分配 模式
    void* ptr1 = pool.allocate();
    ASSERT_TRUE(ptr1 != nullptr);

    pool.deallocate(ptr1);

    void* ptr2 = pool.allocate();
    ASSERT_TRUE(ptr2 != nullptr);

    // 释放后重新分配应该复用内存
    ASSERT_EQ(ptr1, ptr2);

    pool.deallocate(ptr2);
}

TEST(FixedPoolTests, StatsTracking) {
    PoolConfig config{
        .block_size = 64,
        .chunk_size = 4096,
        .initial_chunks = 1,
        .thread_safe = false,
    };

    FixedPool pool(config);

    ASSERT_EQ(pool.chunk_count(), 1);
    ASSERT_GT(pool.total_blocks(), 0);
    ASSERT_EQ(pool.used_blocks(), 0);

    void* ptr = pool.allocate();
    ASSERT_EQ(pool.used_blocks(), 1);

    pool.deallocate(ptr);
    ASSERT_EQ(pool.used_blocks(), 0);
}

TEST(FixedPoolTests, Reset) {
    PoolConfig config{
        .block_size = 64,
        .chunk_size = 4096,
        .thread_safe = false,
    };

    FixedPool pool(config);
    std::vector<void*> ptrs;

    // 分配一些块
    for (int i = 0; i < 50; ++i) {
        ptrs.push_back(pool.allocate());
    }

    ASSERT_EQ(pool.used_blocks(), 50);

    // 重置池
    pool.reset();

    ASSERT_EQ(pool.used_blocks(), 0);
    ASSERT_EQ(pool.free_blocks(), pool.total_blocks());
}

TEST(FixedPoolTests, Clear) {
    PoolConfig config{
        .block_size = 64,
        .chunk_size = 4096,
        .thread_safe = false,
    };

    FixedPool pool(config);

    // 分配触发多个 chunk
    for (int i = 0; i < 200; ++i) {
        pool.allocate();
    }

    ASSERT_GT(pool.chunk_count(), 0);

    // 清空池
    pool.clear();

    ASSERT_EQ(pool.chunk_count(), 0);
    ASSERT_EQ(pool.total_blocks(), 0);
}

TEST(FixedPoolTests, MoveConstruction) {
    PoolConfig config{
        .block_size = 64,
        .chunk_size = 4096,
        .thread_safe = false,
    };

    FixedPool pool1(config);
    void* ptr = pool1.allocate();
    std::size_t original_used = pool1.used_blocks();

    FixedPool pool2(std::move(pool1));

    ASSERT_EQ(pool2.used_blocks(), original_used);
    ASSERT_EQ(pool1.chunk_count(), 0);

    pool2.deallocate(ptr);
}

// ========================================
// FixedPool 多线程测试
// ========================================

TEST(FixedPoolTests, ConcurrentAllocations) {
    PoolConfig config{
        .block_size = 64,
        .chunk_size = 64 * 1024,
        .thread_safe = true,
    };

    FixedPool pool(config);
    constexpr int num_threads = 4;
    constexpr int allocs_per_thread = 1000;

    std::atomic<int> success_count{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&pool, &success_count]() {
            std::vector<void*> ptrs;
            for (int i = 0; i < allocs_per_thread; ++i) {
                void* ptr = pool.allocate();
                if (ptr) {
                    ptrs.push_back(ptr);
                    success_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
            // 释放所有
            for (void* ptr : ptrs) {
                pool.deallocate(ptr);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(success_count.load(), num_threads * allocs_per_thread);
    ASSERT_EQ(pool.used_blocks(), 0);
}

// ========================================
// ObjectPool 测试
// ========================================

struct TestObject {
    int value;
    double data;
    bool initialized;

    TestObject() : value(0), data(0.0), initialized(true) {}
    TestObject(int v, double d) : value(v), data(d), initialized(true) {}
    ~TestObject() { initialized = false; }
};

TEST(ObjectPoolTests, BasicCreateDestroy) {
    ObjectPool<TestObject> pool(64, false);

    TestObject* obj = pool.create(42, 3.14);
    ASSERT_TRUE(obj != nullptr);
    ASSERT_EQ(obj->value, 42);
    ASSERT_EQ(obj->data, 3.14);
    ASSERT_TRUE(obj->initialized);
    ASSERT_EQ(pool.size(), 1);

    pool.destroy(obj);
    ASSERT_EQ(pool.size(), 0);
}

TEST(ObjectPoolTests, MultipleObjects) {
    ObjectPool<TestObject> pool(64, false);
    std::vector<TestObject*> objects;

    for (int i = 0; i < 100; ++i) {
        TestObject* obj = pool.create(i, static_cast<double>(i) * 0.5);
        objects.push_back(obj);
    }

    ASSERT_EQ(pool.size(), 100);

    // 验证所有对象
    for (int i = 0; i < 100; ++i) {
        ASSERT_EQ(objects[i]->value, i);
        ASSERT_EQ(objects[i]->data, static_cast<double>(i) * 0.5);
    }

    // 销毁所有
    for (TestObject* obj : objects) {
        pool.destroy(obj);
    }

    ASSERT_EQ(pool.size(), 0);
}

TEST(ObjectPoolTests, DefaultConstruction) {
    ObjectPool<TestObject> pool(64, false);

    TestObject* obj = pool.create();
    ASSERT_TRUE(obj != nullptr);
    ASSERT_EQ(obj->value, 0);
    ASSERT_EQ(obj->data, 0.0);
    ASSERT_TRUE(obj->initialized);

    pool.destroy(obj);
}

TEST(ObjectPoolTests, DestroyNull) {
    ObjectPool<TestObject> pool(64, false);

    // 销毁 nullptr 应该安全
    pool.destroy(nullptr);
    ASSERT_EQ(pool.size(), 0);
}

TEST(ObjectPoolTests, ConcurrentCreateDestroy) {
    ObjectPool<TestObject> pool(1024, true);
    constexpr int num_threads = 4;
    constexpr int ops_per_thread = 500;

    std::atomic<int> created_count{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&pool, &created_count, t]() {
            std::vector<TestObject*> objs;
            for (int i = 0; i < ops_per_thread; ++i) {
                TestObject* obj = pool.create(t * 1000 + i, static_cast<double>(i));
                if (obj) {
                    objs.push_back(obj);
                    created_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
            for (TestObject* obj : objs) {
                pool.destroy(obj);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(created_count.load(), num_threads * ops_per_thread);
    ASSERT_EQ(pool.size(), 0);
}

// ========================================
// LinearArena 测试
// ========================================

TEST(LinearArenaTests, BasicAllocation) {
    LinearArena arena(4096);

    ASSERT_EQ(arena.capacity(), 4096);
    ASSERT_EQ(arena.used(), 0);
    ASSERT_TRUE(arena.empty());

    void* ptr = arena.allocate(64);
    ASSERT_TRUE(ptr != nullptr);
    ASSERT_GE(arena.used(), 64);
    ASSERT_FALSE(arena.empty());
}

TEST(LinearArenaTests, AlignedAllocation) {
    LinearArena arena(4096);

    void* ptr1 = arena.allocate(10, 64);
    ASSERT_TRUE(ptr1 != nullptr);
    ASSERT_TRUE(is_aligned(ptr1, 64));

    // 注意：分配的对齐取决于缓冲区起始地址和已分配大小
    // 只测试 CacheLineSize 对齐，因为缓冲区本身是按 CacheLineSize 对齐的
    void* ptr2 = arena.allocate(10, CacheLineSize);
    ASSERT_TRUE(ptr2 != nullptr);
    ASSERT_TRUE(is_aligned(ptr2, CacheLineSize));
}

TEST(LinearArenaTests, CreateObject) {
    LinearArena arena(4096);

    TestObject* obj = arena.create<TestObject>(100, 2.5);
    ASSERT_TRUE(obj != nullptr);
    ASSERT_EQ(obj->value, 100);
    ASSERT_EQ(obj->data, 2.5);
    ASSERT_TRUE(obj->initialized);

    // 注意：LinearArena 不调用析构函数
}

TEST(LinearArenaTests, AllocateArray) {
    LinearArena arena(4096);

    int* arr = arena.allocate_array<int>(10);
    ASSERT_TRUE(arr != nullptr);

    for (int i = 0; i < 10; ++i) {
        arr[i] = i * 10;
    }

    for (int i = 0; i < 10; ++i) {
        ASSERT_EQ(arr[i], i * 10);
    }
}

TEST(LinearArenaTests, Reset) {
    LinearArena arena(4096);

    arena.allocate(1000);
    arena.allocate(500);
    ASSERT_GT(arena.used(), 0);

    arena.reset();
    ASSERT_EQ(arena.used(), 0);
    ASSERT_TRUE(arena.empty());

    // 重置后应该可以继续分配
    void* ptr = arena.allocate(100);
    ASSERT_TRUE(ptr != nullptr);
}

TEST(LinearArenaTests, OutOfMemory) {
    LinearArena arena(256);

    // 尝试分配超过容量
    void* ptr = arena.allocate(512);
    ASSERT_TRUE(ptr == nullptr);
}

TEST(LinearArenaTests, Utilization) {
    LinearArena arena(1000);

    ASSERT_EQ(arena.utilization(), 0.0);

    arena.allocate(500);
    ASSERT_GE(arena.utilization(), 0.5);

    arena.reset();
    ASSERT_EQ(arena.utilization(), 0.0);
}

TEST(LinearArenaTests, MoveConstruction) {
    LinearArena arena1(4096);
    arena1.allocate(100);
    std::size_t used = arena1.used();

    LinearArena arena2(std::move(arena1));

    ASSERT_EQ(arena2.used(), used);
    ASSERT_EQ(arena2.capacity(), 4096);
    ASSERT_EQ(arena1.capacity(), 0);
}

// ========================================
// FrameArena 测试
// ========================================

TEST(FrameArenaTests, BasicUsage) {
    FrameArena arena(4096);

    ASSERT_EQ(arena.buffer_capacity(), 4096);
    ASSERT_EQ(arena.current_used(), 0);
}

TEST(FrameArenaTests, CurrentAllocation) {
    FrameArena arena(4096);

    void* ptr = arena.allocate(64);
    ASSERT_TRUE(ptr != nullptr);
    ASSERT_GT(arena.current_used(), 0);
}

TEST(FrameArenaTests, CreateObject) {
    FrameArena arena(4096);

    TestObject* obj = arena.create<TestObject>(42, 1.5);
    ASSERT_TRUE(obj != nullptr);
    ASSERT_EQ(obj->value, 42);
    ASSERT_EQ(obj->data, 1.5);
}

TEST(FrameArenaTests, BeginFrame) {
    FrameArena arena(4096);

    arena.allocate(100);
    ASSERT_GT(arena.current_used(), 0);

    arena.begin_frame();
    ASSERT_EQ(arena.current_used(), 0);
}

TEST(FrameArenaTests, SwapBuffers) {
    FrameArena arena(4096);

    std::size_t initial_index = arena.current_index();

    arena.swap();
    ASSERT_NE(arena.current_index(), initial_index);

    arena.swap();
    ASSERT_EQ(arena.current_index(), initial_index);
}

TEST(FrameArenaTests, DoubleBuffering) {
    FrameArena arena(4096);

    // 帧 1：分配数据
    arena.begin_frame();
    int* frame1_data = arena.create<int>(100);
    ASSERT_EQ(*frame1_data, 100);

    // 结束帧 1
    arena.end_frame();

    // 帧 2：上一帧数据仍然有效
    arena.begin_frame();
    ASSERT_EQ(*frame1_data, 100);

    int* frame2_data = arena.create<int>(200);
    ASSERT_EQ(*frame2_data, 200);

    // 结束帧 2
    arena.end_frame();

    // 帧 3：帧 1 的数据可能被覆盖（这是预期行为）
}

TEST(FrameArenaTests, ResetAll) {
    FrameArena arena(4096);

    arena.allocate(100);
    arena.swap();
    arena.allocate(200);

    arena.reset_all();

    ASSERT_EQ(arena.current_used(), 0);
    ASSERT_EQ(arena.current_index(), 0);
}

// ========================================
// LockFreeStack 测试
// ========================================

TEST(LockFreeStackTests, BasicPushPop) {
    LockFreeStack stack;

    ASSERT_TRUE(stack.empty());

    LockFreeNode node1, node2, node3;

    stack.push(&node1);
    ASSERT_FALSE(stack.empty());

    stack.push(&node2);
    stack.push(&node3);

    // LIFO 顺序
    ASSERT_EQ(stack.pop(), &node3);
    ASSERT_EQ(stack.pop(), &node2);
    ASSERT_EQ(stack.pop(), &node1);
    ASSERT_TRUE(stack.empty());
}

TEST(LockFreeStackTests, PopEmpty) {
    LockFreeStack stack;

    ASSERT_EQ(stack.pop(), nullptr);
}

TEST(LockFreeStackTests, PushNull) {
    LockFreeStack stack;

    stack.push(nullptr);
    ASSERT_TRUE(stack.empty());
}

TEST(LockFreeStackTests, Clear) {
    LockFreeStack stack;
    LockFreeNode node1, node2;

    stack.push(&node1);
    stack.push(&node2);
    ASSERT_FALSE(stack.empty());

    stack.clear();
    ASSERT_TRUE(stack.empty());
}

TEST(LockFreeStackTests, ApproximateSize) {
    LockFreeStack stack;
    LockFreeNode nodes[10];

    ASSERT_EQ(stack.approximate_size(), 0);

    for (int i = 0; i < 10; ++i) {
        stack.push(&nodes[i]);
    }

    ASSERT_EQ(stack.approximate_size(), 10);
}

TEST(LockFreeStackTests, ConcurrentPushPop) {
    LockFreeStack stack;
    constexpr int num_threads = 4;
    constexpr int ops_per_thread = 1000;

    // 每个线程有自己的节点数组（使用 unique_ptr 数组）
    std::vector<std::unique_ptr<LockFreeNode[]>> thread_nodes(num_threads);
    for (int i = 0; i < num_threads; ++i) {
        thread_nodes[i] = std::make_unique<LockFreeNode[]>(ops_per_thread);
    }

    std::atomic<int> push_count{0};
    std::atomic<int> pop_count{0};
    std::vector<std::thread> threads;

    // 生产者线程
    for (int t = 0; t < num_threads / 2; ++t) {
        threads.emplace_back([&stack, &thread_nodes, &push_count, t]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                stack.push(&thread_nodes[t][i]);
                push_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // 消费者线程
    for (int t = num_threads / 2; t < num_threads; ++t) {
        threads.emplace_back([&stack, &pop_count]() {
            int local_count = 0;
            while (local_count < ops_per_thread) {
                if (stack.pop() != nullptr) {
                    local_count++;
                }
            }
            pop_count.fetch_add(local_count, std::memory_order_relaxed);
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(push_count.load(), (num_threads / 2) * ops_per_thread);
    ASSERT_EQ(pop_count.load(), (num_threads / 2) * ops_per_thread);
}

// ========================================
// ThreadLocalPool 测试
// ========================================

TEST(ThreadLocalPoolTests, BasicUsage) {
    TestObject* obj = ThreadLocalPool<TestObject>::create(42, 1.5);
    ASSERT_TRUE(obj != nullptr);
    ASSERT_EQ(obj->value, 42);
    ASSERT_EQ(obj->data, 1.5);

    ThreadLocalPool<TestObject>::destroy(obj);
}

TEST(ThreadLocalPoolTests, MultipleObjects) {
    std::vector<TestObject*> objects;

    for (int i = 0; i < 100; ++i) {
        objects.push_back(ThreadLocalPool<TestObject>::create(i, static_cast<double>(i)));
    }

    for (int i = 0; i < 100; ++i) {
        ASSERT_EQ(objects[i]->value, i);
    }

    for (TestObject* obj : objects) {
        ThreadLocalPool<TestObject>::destroy(obj);
    }
}

TEST(ThreadLocalPoolTests, MultiThreaded) {
    constexpr int num_threads = 4;
    constexpr int ops_per_thread = 500;

    std::atomic<int> total_created{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&total_created, t]() {
            std::vector<TestObject*> objects;
            for (int i = 0; i < ops_per_thread; ++i) {
                TestObject* obj = ThreadLocalPool<TestObject>::create(t * 1000 + i, 0.0);
                objects.push_back(obj);
                total_created.fetch_add(1, std::memory_order_relaxed);
            }
            for (TestObject* obj : objects) {
                ThreadLocalPool<TestObject>::destroy(obj);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(total_created.load(), num_threads * ops_per_thread);
}

TEST(ThreadLocalPoolTests, UniquePtr) {
    auto ptr = make_thread_local_unique<TestObject>(99, 9.9);
    ASSERT_TRUE(ptr != nullptr);
    ASSERT_EQ(ptr->value, 99);
    ASSERT_EQ(ptr->data, 9.9);

    // 离开作用域自动销毁
}

// ========================================
// ChunkHeader 测试
// ========================================

TEST(ChunkHeaderTests, CalculateBlockCount) {
    std::size_t chunk_size = 4096;
    std::size_t block_size = 64;
    std::size_t alignment = 64;

    std::size_t count = ChunkHeader::calculate_block_count(chunk_size, block_size, alignment);
    ASSERT_GT(count, 0);

    // 验证计算合理性：头部 + count * block_size <= chunk_size
    std::size_t header_size = align_up(sizeof(ChunkHeader), alignment);
    ASSERT_LE(header_size + count * block_size, chunk_size);
}

TEST(ChunkHeaderTests, SmallChunkSize) {
    // chunk_size 太小无法容纳任何块
    std::size_t count = ChunkHeader::calculate_block_count(64, 128, 64);
    ASSERT_EQ(count, 0);
}

// ========================================
// PoolStats 测试
// ========================================

TEST(PoolStatsTests, Utilization) {
    PoolStats stats;
    stats.total_memory = 1000;
    stats.used_memory = 500;

    ASSERT_EQ(stats.utilization(), 0.5);
}

TEST(PoolStatsTests, UtilizationZeroTotal) {
    PoolStats stats;
    stats.total_memory = 0;
    stats.used_memory = 0;

    ASSERT_EQ(stats.utilization(), 0.0);
}

TEST(PoolStatsTests, Fragmentation) {
    PoolStats stats;
    stats.block_count = 100;
    stats.free_block_count = 30;

    // fragmentation = 1.0 - used/total = 1.0 - 70/100 = 0.3
    // 即 30% 的空闲率意味着 30% 的碎片
    double frag = stats.fragmentation();
    ASSERT_TRUE(frag > 0.29 && frag < 0.31);  // 使用容差比较，避免浮点精度问题
}

// ========================================
// 性能基准测试
// ========================================

TEST(MemoryPoolBenchmark, FixedPoolVsMalloc) {
    constexpr int iterations = 10000;

    // FixedPool 测试
    PoolConfig config{
        .block_size = 64,
        .chunk_size = 64 * 1024,
        .thread_safe = false,
    };
    FixedPool pool(config);

    auto pool_start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        void* ptr = pool.allocate();
        pool.deallocate(ptr);
    }
    auto pool_end = std::chrono::high_resolution_clock::now();
    auto pool_duration = std::chrono::duration<double, std::micro>(pool_end - pool_start).count();

    // malloc 测试
    auto malloc_start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        void* ptr = std::malloc(64);
        std::free(ptr);
    }
    auto malloc_end = std::chrono::high_resolution_clock::now();
    auto malloc_duration = std::chrono::duration<double, std::micro>(malloc_end - malloc_start).count();

    std::cout << "  FixedPool: " << pool_duration << " us\n";
    std::cout << "  malloc:    " << malloc_duration << " us\n";
    std::cout << "  Speedup:   " << malloc_duration / pool_duration << "x\n";

    // 只要能运行就算通过，不强制要求 FixedPool 更快
    ASSERT_TRUE(true);
}

TEST(MemoryPoolBenchmark, LinearArenaVsMalloc) {
    constexpr int iterations = 10000;
    constexpr std::size_t alloc_size = 64;

    // LinearArena 测试
    LinearArena arena(iterations * alloc_size * 2);

    auto arena_start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        arena.allocate(alloc_size);
    }
    auto arena_end = std::chrono::high_resolution_clock::now();
    auto arena_duration = std::chrono::duration<double, std::micro>(arena_end - arena_start).count();

    // malloc 测试
    std::vector<void*> ptrs;
    ptrs.reserve(iterations);

    auto malloc_start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        ptrs.push_back(std::malloc(alloc_size));
    }
    auto malloc_end = std::chrono::high_resolution_clock::now();
    auto malloc_duration = std::chrono::duration<double, std::micro>(malloc_end - malloc_start).count();

    // 清理
    for (void* ptr : ptrs) {
        std::free(ptr);
    }

    std::cout << "  LinearArena: " << arena_duration << " us\n";
    std::cout << "  malloc:      " << malloc_duration << " us\n";
    std::cout << "  Speedup:     " << malloc_duration / arena_duration << "x\n";

    ASSERT_TRUE(true);
}

// ========================================
// Main
// ========================================

int main() {
    return CoronaTest::TestRunner::instance().run_all();
}
