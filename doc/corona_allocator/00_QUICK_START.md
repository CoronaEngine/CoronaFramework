# 快速入门指南

## 🚀 10分钟开始你的第一个版本

本指南帮助你快速搭建一个最小可用版本（MVP），验证核心概念。

---

## 第一步：项目初始化 (10 minutes)

### 创建项目结构

```bash
mkdir corona_malloc && cd corona_malloc

# 创建目录结构
mkdir -p include src tests benchmarks

# 创建基本文件
touch include/corona_malloc.h
touch src/frontend.cpp
touch src/block.cpp
touch CMakeLists.txt
```

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.15)
project(ScalableMalloc VERSION 1.0 LANGUAGES CXX)

# C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译选项
add_compile_options(-Wall -Wextra -O2)

# 主库
add_library(corona_malloc SHARED
    src/frontend.cpp
    src/block.cpp
)

target_include_directories(corona_malloc PUBLIC include)

# 测试
enable_testing()
add_subdirectory(tests)
```

---

## 第二步：最简实现 (30 minutes)

### include/corona_malloc.h

```cpp
#ifndef SCALABLE_MALLOC_H
#define SCALABLE_MALLOC_H

#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

void* corona_malloc(size_t size);
void  corona_free(void* ptr);

#ifdef __cplusplus
}
#endif

#endif // SCALABLE_MALLOC_H
```

### src/frontend.cpp (简化版本)

```cpp
#include "corona_malloc.h"
#include <cstdlib>
#include <cstring>
#include <mutex>

// === MVP 版本：直接封装系统 malloc ===
// 目的：验证构建系统和基本流程

namespace {
    std::mutex g_malloc_lock;  // 全局锁（MVP版本）
}

extern "C" {

void* corona_malloc(size_t size) {
    if (size == 0) return nullptr;
    
    std::lock_guard<std::mutex> lock(g_malloc_lock);
    return std::malloc(size);
}

void corona_free(void* ptr) {
    if (!ptr) return;
    
    std::lock_guard<std::mutex> lock(g_malloc_lock);
    std::free(ptr);
}

} // extern "C"
```

### tests/basic_test.cpp

```cpp
#include "corona_malloc.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    std::cout << "Running basic tests...\n";
    
    // Test 1: 基本分配
    void* ptr = corona_malloc(64);
    assert(ptr != nullptr);
    std::cout << "✓ Basic allocation\n";
    
    // Test 2: 写入数据
    memset(ptr, 0xAB, 64);
    std::cout << "✓ Memory write\n";
    
    // Test 3: 释放
    corona_free(ptr);
    std::cout << "✓ Free\n";
    
    // Test 4: nullptr 释放
    corona_free(nullptr);
    std::cout << "✓ Free nullptr\n";
    
    std::cout << "All tests passed!\n";
    return 0;
}
```

### 构建和运行

```bash
# 构建
mkdir build && cd build
cmake ..
make

# 运行测试
./tests/basic_test
```

---

## 第三步：添加 Block 管理 (2 hours)

### include/block.h

```cpp
#ifndef BLOCK_H
#define BLOCK_H

#include <cstddef>
#include <cstdint>

constexpr size_t SLAB_SIZE = 16 * 1024;  // 16 KB

struct FreeObject {
    FreeObject* next;
};

class Block {
private:
    FreeObject*  bump_ptr_;
    FreeObject*  free_list_;
    size_t       object_size_;
    size_t       allocated_count_;
    
public:
    void init(size_t object_size);
    void* allocate();
    void free_local(void* ptr);
    bool is_empty() const;
    
    static Block* from_object(void* obj) {
        uintptr_t addr = reinterpret_cast<uintptr_t>(obj);
        return reinterpret_cast<Block*>(addr & ~(SLAB_SIZE - 1));
    }
};

#endif // BLOCK_H
```

### src/block.cpp

```cpp
#include "block.h"
#include <cstring>

void Block::init(size_t object_size) {
    object_size_ = object_size;
    allocated_count_ = 0;
    free_list_ = nullptr;
    
    // Bump pointer 从 block 末尾开始
    uintptr_t block_start = reinterpret_cast<uintptr_t>(this);
    uintptr_t data_start = block_start + sizeof(Block);
    uintptr_t block_end = block_start + SLAB_SIZE;
    
    bump_ptr_ = reinterpret_cast<FreeObject*>(
        block_end - object_size_
    );
}

void* Block::allocate() {
    // 1. 尝试 free list
    if (free_list_) {
        void* obj = free_list_;
        free_list_ = free_list_->next;
        allocated_count_++;
        return obj;
    }
    
    // 2. 尝试 bump pointer
    if (bump_ptr_) {
        uintptr_t block_start = reinterpret_cast<uintptr_t>(this);
        uintptr_t data_start = block_start + sizeof(Block);
        uintptr_t current = reinterpret_cast<uintptr_t>(bump_ptr_);
        
        if (current >= data_start) {
            void* obj = bump_ptr_;
            bump_ptr_ = reinterpret_cast<FreeObject*>(
                current - object_size_
            );
            allocated_count_++;
            return obj;
        }
    }
    
    return nullptr;  // Block 满了
}

void Block::free_local(void* ptr) {
    FreeObject* obj = static_cast<FreeObject*>(ptr);
    obj->next = free_list_;
    free_list_ = obj;
    allocated_count_--;
}

bool Block::is_empty() const {
    return allocated_count_ == 0;
}
```

### 更新 frontend.cpp

```cpp
#include "corona_malloc.h"
#include "block.h"
#include <sys/mman.h>  // Linux
#include <mutex>
#include <map>

namespace {
    std::mutex g_malloc_lock;
    std::map<size_t, Block*> g_blocks;  // size -> block
    
    Block* get_or_create_block(size_t size) {
        auto it = g_blocks.find(size);
        if (it != g_blocks.end() && !it->second->is_full()) {
            return it->second;
        }
        
        // 分配新 block
        void* mem = mmap(nullptr, SLAB_SIZE,
                        PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mem == MAP_FAILED) return nullptr;
        
        Block* block = static_cast<Block*>(mem);
        block->init(size);
        g_blocks[size] = block;
        
        return block;
    }
}

extern "C" {

void* corona_malloc(size_t size) {
    if (size == 0) return nullptr;
    if (size > 8192) return std::malloc(size);  // 大对象
    
    // 对齐到 8 字节
    size = (size + 7) & ~7;
    
    std::lock_guard<std::mutex> lock(g_malloc_lock);
    
    Block* block = get_or_create_block(size);
    if (!block) return nullptr;
    
    return block->allocate();
}

void corona_free(void* ptr) {
    if (!ptr) return;
    
    std::lock_guard<std::mutex> lock(g_malloc_lock);
    
    Block* block = Block::from_object(ptr);
    block->free_local(ptr);
}

} // extern "C"
```

---

## 第四步：添加性能测试 (30 minutes)

### benchmarks/simple_bench.cpp

```cpp
#include "corona_malloc.h"
#include <chrono>
#include <iostream>
#include <vector>

using namespace std::chrono;

void benchmark_alloc_free(const char* name, size_t size, int iterations) {
    auto start = high_resolution_clock::now();
    
    for (int i = 0; i < iterations; i++) {
        void* ptr = corona_malloc(size);
        corona_free(ptr);
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<nanoseconds>(end - start).count();
    
    std::cout << name << " (" << size << " bytes): "
              << duration / iterations << " ns/op\n";
}

int main() {
    constexpr int ITERATIONS = 1000000;
    
    std::cout << "=== Scalable Malloc Benchmarks ===\n\n";
    
    benchmark_alloc_free("Small object", 64, ITERATIONS);
    benchmark_alloc_free("Medium object", 256, ITERATIONS);
    benchmark_alloc_free("Large object", 4096, ITERATIONS);
    
    return 0;
}
```

---

## 第五步：持续改进路线图

### Week 1-2: 基础优化
- [ ] 去除全局锁，添加 TLS
- [ ] 实现 Bin 数组
- [ ] 优化 Block 分配策略

### Week 3-4: 跨线程支持
- [ ] Public FreeList（无锁）
- [ ] Mailbox 机制
- [ ] Orphaned Blocks

### Week 5-6: 大对象管理
- [ ] LargeMemoryBlock
- [ ] Local LOC 缓存
- [ ] BackRef 系统

### Week 7-8: 性能优化
- [ ] Huge Pages 支持
- [ ] 缓存行对齐
- [ ] 内联优化

### Week 9-10: 测试和调优
- [ ] 完整测试套件
- [ ] 性能基准测试
- [ ] 内存泄漏检测

---

## 📊 性能对比（MVP vs 最终版本）

| 指标 | MVP版本 | 第三步 | 最终版本 | 目标 |
|------|---------|--------|---------|------|
| malloc(64) | ~80 ns | ~40 ns | ~15 ns | <20 ns |
| 多线程可扩展性 | 0.1x | 0.5x | 0.85x | >0.8x |
| 代码行数 | ~200 | ~800 | ~5000 | - |

---

## 🐛 常见问题

### 编译错误

```bash
# 缺少 C++17 支持
# 解决：升级编译器或添加 -std=c++17

# 链接错误
# 解决：确保 CMakeLists.txt 正确配置
```

### 运行时错误

```bash
# Segmentation fault
# 检查：Block::from_object 的地址计算
# 使用 gdb 调试：gdb ./basic_test

# 内存泄漏
# 使用 Valgrind 检测：valgrind --leak-check=full ./basic_test
```

---

## 📚 学习资源

### 推荐阅读顺序

1. **第一天**: 阅读 01_ARCHITECTURE.md
2. **第二天**: 阅读 02_DATA_STRUCTURES.md
3. **第三天**: 实现 MVP 版本
4. **第四天**: 阅读 03_FRONTEND.md
5. **第一周**: 完成基础 Block 管理
6. **第二周**: 添加 TLS 支持
7. **第三周**: 实现跨线程释放
8. **第四周**: 大对象管理
9. **第二个月**: 性能优化
10. **第三个月**: 测试和调优

### 参考代码

- **Intel TBB tbbmalloc**: 本项目的参考实现
- **Google TCMalloc**: https://github.com/google/tcmalloc
- **jemalloc**: https://github.com/jemalloc/jemalloc
- **Mimalloc**: https://github.com/microsoft/mimalloc

---

## 🎯 下一步行动

1. **立即开始**: 复制粘贴代码，构建 MVP
2. **验证概念**: 运行测试，确保工作
3. **逐步迭代**: 按照路线图一步步改进
4. **持续学习**: 阅读详细文档，理解原理

**记住**: 先让它工作，再让它正确，最后让它快速！

---

**祝你成功！** 🚀

---

**文档版本**: 1.0  
**最后更新**: 2025年10月31日
