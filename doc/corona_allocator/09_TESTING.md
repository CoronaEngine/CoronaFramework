# 09 - 测试策略和基准测试

## 🧪 测试金字塔

```
                    /\
                   /  \
                  /端到\
                 / 端测试 \
                /  (5%)   \
               /___________\
              /            \
             /   集成测试   \
            /    (15%)     \
           /_______________\
          /                \
         /    单元测试       \
        /      (80%)        \
       /____________________\
```

---

## 📋 测试计划概览

### Phase 1: 单元测试 (80% coverage)

**目标**: 验证每个模块的基本功能正确性

**测试范围**:
- Block 操作
- Bin 管理
- TLS 初始化
- 大小计算
- 对齐逻辑
- 原子操作

**工具**: Google Test / Catch2

### Phase 2: 集成测试 (15%)

**目标**: 验证模块间交互

**测试范围**:
- malloc/free 配对
- 跨线程释放
- 缓存策略
- Backend 交互
- 内存泄漏检测

**工具**: Valgrind, AddressSanitizer, ThreadSanitizer

### Phase 3: 端到端测试 (5%)

**目标**: 验证真实场景

**测试范围**:
- 实际应用集成
- 长时间运行
- 压力测试
- 性能回归

---

## 🎯 单元测试示例

### Block 测试

```cpp
#include <gtest/gtest.h>
#include "block.h"

class BlockTest : public ::testing::Test {
protected:
    Block* block;
    TLSData* mock_tls;
    
    void SetUp() override {
        mock_tls = create_mock_tls();
        block = new Block();
        block->init_empty(32, mock_tls);
    }
    
    void TearDown() override {
        delete block;
        destroy_mock_tls(mock_tls);
    }
};

TEST_F(BlockTest, BumpAllocation) {
    // 测试 bump pointer 分配
    void* obj1 = block->try_allocate_fast();
    ASSERT_NE(obj1, nullptr);
    
    void* obj2 = block->try_allocate_fast();
    ASSERT_NE(obj2, nullptr);
    
    // 地址应该递增
    EXPECT_EQ(reinterpret_cast<char*>(obj2) - reinterpret_cast<char*>(obj1), 32);
}

TEST_F(BlockTest, FreeListAllocation) {
    // 先分配
    void* obj = block->allocate();
    ASSERT_NE(obj, nullptr);
    
    // 释放
    block->free_local(obj);
    
    // 再分配应该得到同一对象
    void* obj2 = block->allocate();
    EXPECT_EQ(obj, obj2);
}

TEST_F(BlockTest, PublicFreeList) {
    void* obj1 = block->allocate();
    void* obj2 = block->allocate();
    
    // 模拟跨线程释放
    block->free_public(obj1);
    block->free_public(obj2);
    
    // 私有化
    block->privatize_public_free_list();
    
    // 应该能再分配
    void* obj3 = block->allocate();
    EXPECT_TRUE(obj3 == obj1 || obj3 == obj2);
}

TEST_F(BlockTest, EmptyDetection) {
    size_t capacity = block->get_capacity();
    
    // 分配所有对象
    std::vector<void*> objects;
    for (size_t i = 0; i < capacity; i++) {
        void* obj = block->allocate();
        ASSERT_NE(obj, nullptr);
        objects.push_back(obj);
    }
    
    EXPECT_TRUE(block->is_full());
    
    // 释放所有对象
    for (void* obj : objects) {
        block->free_local(obj);
    }
    
    EXPECT_TRUE(block->is_empty());
}
```

### Bin 测试

```cpp
TEST(BinTest, IndexCalculation) {
    // 小对象
    EXPECT_EQ(get_bin_index(8), 0);
    EXPECT_EQ(get_bin_index(16), 1);
    EXPECT_EQ(get_bin_index(24), 3);  // 64位16字节对齐
    
    // 分段对象
    EXPECT_EQ(get_bin_index(80), 8);
    EXPECT_EQ(get_bin_index(256), 15);
    
    // 拟合对象
    EXPECT_EQ(get_bin_index(1792), 24);
    EXPECT_EQ(get_bin_index(8128), 28);
}

TEST(BinTest, SizeRoundUp) {
    EXPECT_EQ(get_object_size(1), 8);
    EXPECT_EQ(get_object_size(15), 16);
    EXPECT_EQ(get_object_size(17), 24);  // 64位
    EXPECT_EQ(get_object_size(100), 112);
}
```

---

## 🔥 并发测试

### 多线程分配/释放

```cpp
TEST(ConcurrencyTest, ParallelAllocFree) {
    constexpr int NUM_THREADS = 16;
    constexpr int ITERATIONS = 10000;
    
    std::vector<std::thread> threads;
    std::atomic<int> error_count{0};
    
    for (int t = 0; t < NUM_THREADS; t++) {
        threads.emplace_back([&error_count]() {
            for (int i = 0; i < ITERATIONS; i++) {
                size_t size = (rand() % 1024) + 1;
                void* ptr = corona_malloc(size);
                
                if (!ptr) {
                    error_count++;
                    continue;
                }
                
                // 写入数据
                memset(ptr, 0xAB, size);
                
                // 释放
                corona_free(ptr);
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_EQ(error_count.load(), 0);
}
```

### 跨线程释放测试

```cpp
TEST(ConcurrencyTest, CrossThreadFree) {
    constexpr int NUM_OBJECTS = 1000;
    std::vector<void*> objects;
    
    // 线程A分配
    std::thread allocator([&objects]() {
        for (int i = 0; i < NUM_OBJECTS; i++) {
            void* ptr = corona_malloc(64);
            objects.push_back(ptr);
        }
    });
    allocator.join();
    
    // 线程B释放
    std::thread freer([&objects]() {
        for (void* ptr : objects) {
            corona_free(ptr);
        }
    });
    freer.join();
    
    // 不应该崩溃或泄漏
}
```

### ABA 问题测试

```cpp
TEST(ConcurrencyTest, ABAResistance) {
    std::atomic<FreeObject*> head{nullptr};
    constexpr int ITERATIONS = 100000;
    
    auto pusher = [&head]() {
        for (int i = 0; i < ITERATIONS; i++) {
            FreeObject* obj = new FreeObject();
            
            FreeObject* old_head = head.load(std::memory_order_acquire);
            do {
                obj->next = old_head;
            } while (!head.compare_exchange_weak(
                old_head, obj,
                std::memory_order_release,
                std::memory_order_acquire
            ));
        }
    };
    
    auto popper = [&head]() {
        int pop_count = 0;
        for (int i = 0; i < ITERATIONS; i++) {
            FreeObject* old_head = head.load(std::memory_order_acquire);
            while (old_head) {
                FreeObject* new_head = old_head->next;
                if (head.compare_exchange_weak(
                    old_head, new_head,
                    std::memory_order_release,
                    std::memory_order_acquire
                )) {
                    delete old_head;
                    pop_count++;
                    break;
                }
            }
        }
        return pop_count;
    };
    
    std::thread t1(pusher);
    std::thread t2(pusher);
    std::thread t3(popper);
    std::thread t4(popper);
    
    t1.join();
    t2.join();
    t3.join();
    t4.join();
    
    // 验证无内存泄漏
}
```

---

## 🔍 内存检测

### AddressSanitizer 测试

```bash
# 编译with ASan
cmake -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" ..
make
./test_suite

# 检测：
# - 堆外访问
# - 使用已释放内存
# - 重复释放
# - 内存泄漏
```

### ThreadSanitizer 测试

```bash
# 编译 with TSan
cmake -DCMAKE_CXX_FLAGS="-fsanitize=thread" ..
make
./test_suite

# 检测：
# - 数据竞争
# - 锁顺序反转
# - 信号-不安全调用
```

### Valgrind Memcheck

```bash
valgrind --tool=memcheck \
         --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
         ./test_suite

# 检测：
# - 内存泄漏
# - 未初始化内存使用
# - 非法读写
```

---

## 📊 性能基准测试

### 基准测试框架

使用 Google Benchmark:

```cpp
#include <benchmark/benchmark.h>

// 单线程小对象分配
static void BM_SmallAlloc(benchmark::State& state) {
    for (auto _ : state) {
        void* ptr = corona_malloc(64);
        benchmark::DoNotOptimize(ptr);
        corona_free(ptr);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SmallAlloc);

// 多线程小对象分配
static void BM_SmallAllocMT(benchmark::State& state) {
    for (auto _ : state) {
        void* ptr = corona_malloc(64);
        benchmark::DoNotOptimize(ptr);
        corona_free(ptr);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SmallAllocMT)->Threads(1)->Threads(2)->Threads(4)->Threads(8)->Threads(16);

// 大对象分配
static void BM_LargeAlloc(benchmark::State& state) {
    size_t size = state.range(0);
    for (auto _ : state) {
        void* ptr = corona_malloc(size);
        benchmark::DoNotOptimize(ptr);
        corona_free(ptr);
    }
    state.SetBytesProcessed(state.iterations() * size);
}
BENCHMARK(BM_LargeAlloc)->Range(16*1024, 4*1024*1024);

// 跨线程释放
static void BM_CrossThreadFree(benchmark::State& state) {
    std::vector<void*> ptrs;
    for (int i = 0; i < 1000; i++) {
        ptrs.push_back(corona_malloc(64));
    }
    
    for (auto _ : state) {
        for (void* ptr : ptrs) {
            corona_free(ptr);
        }
        for (int i = 0; i < 1000; i++) {
            ptrs[i] = corona_malloc(64);
        }
    }
}
BENCHMARK(BM_CrossThreadFree);

BENCHMARK_MAIN();
```

### 对比测试

```cpp
// 与系统分配器对比
static void BM_SystemMalloc(benchmark::State& state) {
    for (auto _ : state) {
        void* ptr = malloc(64);
        benchmark::DoNotOptimize(ptr);
        free(ptr);
    }
}
BENCHMARK(BM_SystemMalloc);

static void BM_ScalableMalloc(benchmark::State& state) {
    for (auto _ : state) {
        void* ptr = corona_malloc(64);
        benchmark::DoNotOptimize(ptr);
        corona_free(ptr);
    }
}
BENCHMARK(BM_ScalableMalloc);
```

### 实际负载模拟

```cpp
// 模拟 Web 服务器负载
static void BM_WebServerWorkload(benchmark::State& state) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<> size_dist(16, 4096);
    std::uniform_int_distribution<> lifetime_dist(1, 100);
    
    std::deque<std::pair<void*, int>> allocations;
    
    for (auto _ : state) {
        // 分配新对象
        size_t size = size_dist(rng);
        int lifetime = lifetime_dist(rng);
        void* ptr = corona_malloc(size);
        allocations.push_back({ptr, lifetime});
        
        // 释放过期对象
        while (!allocations.empty() && --allocations.front().second <= 0) {
            corona_free(allocations.front().first);
            allocations.pop_front();
        }
    }
    
    // 清理
    for (auto& alloc : allocations) {
        corona_free(alloc.first);
    }
}
BENCHMARK(BM_WebServerWorkload);
```

---

## 📈 性能目标

### 单线程性能

| 操作 | 目标延迟 | 对比 glibc | 对比 jemalloc |
|------|---------|-----------|--------------|
| malloc(64) | < 20 ns | 1.5x 更快 | 持平 |
| free(64) | < 15 ns | 2x 更快 | 1.2x 更快 |
| malloc(1MB) | < 50 μs | 持平 | 持平 |

### 多线程性能（可扩展性）

| 线程数 | malloc 吞吐量 | 可扩展性 |
|-------|-------------|---------|
| 1 | 基准(100%) | 1.0x |
| 2 | 190% | 0.95x |
| 4 | 370% | 0.93x |
| 8 | 720% | 0.90x |
| 16 | 1350% | 0.84x |
| 32 | 2400% | 0.75x |

### 内存开销

- **Per-thread overhead**: < 16 KB
- **全局 overhead**: < 2 MB
- **碎片率**: < 15%

---

## 🐛 常见问题测试

### 1. 双重释放检测

```cpp
TEST(ErrorTest, DoubleFree) {
    void* ptr = corona_malloc(64);
    corona_free(ptr);
    
    // 第二次释放应该被检测到（调试模式）
    #ifdef DEBUG_MODE
    EXPECT_DEATH(corona_free(ptr), "double free");
    #endif
}
```

### 2. 内存泄漏检测

```cpp
TEST(LeakTest, NoLeaksAfterManyAllocs) {
    size_t initial_mem = get_memory_usage();
    
    for (int i = 0; i < 10000; i++) {
        void* ptr = corona_malloc(rand() % 1024);
        corona_free(ptr);
    }
    
    size_t final_mem = get_memory_usage();
    EXPECT_LT(final_mem - initial_mem, 100 * 1024);  // < 100KB 增长
}
```

### 3. 对齐验证

```cpp
TEST(AlignmentTest, CacheLineAligned) {
    for (int i = 0; i < 100; i++) {
        void* ptr = corona_malloc(64);
        EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % 128, 0);
        corona_free(ptr);
    }
}
```

---

## 🚀 压力测试

### 长时间运行测试

```cpp
TEST(StressTest, LongRunning) {
    auto start = std::chrono::steady_clock::now();
    auto duration = std::chrono::hours(1);
    
    while (std::chrono::steady_clock::now() - start < duration) {
        // 随机操作
        int op = rand() % 3;
        if (op == 0) {
            // 分配
            void* ptr = corona_malloc(rand() % 10000);
            // 存储以便后续释放
        } else if (op == 1) {
            // 释放
            // 从存储中取出并释放
        } else {
            // realloc
        }
    }
}
```

### 内存压力测试

```cpp
TEST(StressTest, MemoryPressure) {
    std::vector<void*> ptrs;
    
    // 分配直到接近系统限制
    while (true) {
        void* ptr = corona_malloc(1024 * 1024);  // 1MB
        if (!ptr) break;
        ptrs.push_back(ptr);
    }
    
    // 应该能正常释放
    for (void* ptr : ptrs) {
        corona_free(ptr);
    }
}
```

---

## 📊 CI/CD 集成

### GitHub Actions 配置

```yaml
name: Tests

on: [push, pull_request]

jobs:
  unit-tests:
    runs-on: ${{ matrix.os }}
    strategy:
      matrix:
        os: [ubuntu-latest, macos-latest, windows-latest]
        compiler: [gcc, clang, msvc]
    
    steps:
    - uses: actions/checkout@v2
    
    - name: Build
      run: |
        mkdir build && cd build
        cmake ..
        cmake --build .
    
    - name: Run Tests
      run: |
        cd build
        ctest --output-on-failure
  
  sanitizer-tests:
    runs-on: ubuntu-latest
    strategy:
      matrix:
        sanitizer: [address, thread, undefined]
    
    steps:
    - uses: actions/checkout@v2
    
    - name: Build with Sanitizer
      run: |
        mkdir build && cd build
        cmake -DCMAKE_CXX_FLAGS="-fsanitize=${{ matrix.sanitizer }}" ..
        cmake --build .
    
    - name: Run Tests
      run: |
        cd build
        ctest --output-on-failure
  
  benchmarks:
    runs-on: ubuntu-latest
    
    steps:
    - uses: actions/checkout@v2
    
    - name: Run Benchmarks
      run: |
        mkdir build && cd build
        cmake -DBUILD_BENCHMARKS=ON ..
        cmake --build .
        ./benchmarks --benchmark_format=json > results.json
    
    - name: Compare with Baseline
      run: |
        python scripts/compare_benchmarks.py results.json baseline.json
```

---

## 🎯 测试检查清单

### Phase 1: 基本功能 ✅
- [ ] malloc 返回非空指针
- [ ] free 不崩溃
- [ ] realloc 保留数据
- [ ] aligned_malloc 返回对齐地址
- [ ] msize 返回正确大小

### Phase 2: 并发安全 ✅
- [ ] 多线程分配无竞争
- [ ] 跨线程释放正确
- [ ] 无数据竞争（TSan）
- [ ] 无死锁

### Phase 3: 内存安全 ✅
- [ ] 无内存泄漏（Valgrind）
- [ ] 无堆溢出（ASan）
- [ ] 无使用已释放内存
- [ ] 无双重释放

### Phase 4: 性能达标 ✅
- [ ] 单线程性能 > glibc
- [ ] 多线程可扩展性 > 0.8
- [ ] 内存开销 < 20%
- [ ] 碎片率 < 15%

---

**文档版本**: 1.0  
**最后更新**: 2025年10月31日
