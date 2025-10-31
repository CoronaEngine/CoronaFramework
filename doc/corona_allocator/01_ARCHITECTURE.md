# 01 - 总体架构设计

## 📐 架构概述

本文档详细描述可扩展内存分配器的整体架构设计，包括核心设计原则、模块划分和交互流程。

---

## 🎯 设计原则

### 1. 最小化竞争 (Minimize Contention)

**原则**: 避免线程间共享热数据

**实现策略**:
- 每个线程拥有独立的 TLS 数据结构
- 快速路径完全无锁（thread-local 分配）
- 慢速路径使用粗粒度锁保护
- 跨线程操作使用无锁数据结构

**代码示例**:
```cpp
// 快速路径：完全无锁
void* fast_malloc(size_t size) {
    TLSData* tls = get_thread_tls(); // 线程本地
    Bin* bin = &tls->bins[get_bin_index(size)];
    Block* block = bin->active_block; // 无竞争访问
    
    if (block && block->has_free_space()) {
        return block->bump_allocate(); // 指针递增
    }
    return slow_malloc(tls, size); // 进入慢速路径
}
```

### 2. 局部性优化 (Locality of Reference)

**原则**: 最大化 CPU 缓存利用率

**实现策略**:
- 缓存行对齐所有热数据结构
- 将频繁访问的字段放在一起
- 避免 false sharing
- 使用 bump pointer 分配提高空间局部性

**内存布局**:
```
┌─────────────────────────────────────────┐
│  Block Header (128 bytes, cache-aligned)│
│  ┌─────────────────────────────────┐    │
│  │ Hot fields (first cache line)   │    │
│  │  - bumpPtr                       │    │
│  │  - freeList                      │    │
│  │  - publicFreeList (atomic)       │    │
│  └─────────────────────────────────┘    │
│  ┌─────────────────────────────────┐    │
│  │ Cold fields (second cache line) │    │
│  │  - metadata                      │    │
│  │  - backref                       │    │
│  └─────────────────────────────────┘    │
└─────────────────────────────────────────┘
```

### 3. 渐进式复杂度 (Progressive Complexity)

**原则**: 常见情况快速，罕见情况正确

**实现策略**:
- 快速路径：简单、内联、分支预测友好
- 慢速路径：完整但较慢的处理
- 极少路径：可以使用全局锁

**决策树**:
```
malloc(size)
├─ size <= 8128? (99% case)
│  ├─ TLS initialized? (99.9% case)
│  │  ├─ Active block has space? (90% case)
│  │  │  └─ [FAST] Bump allocate
│  │  └─ [MEDIUM] Get block from pool
│  └─ [SLOW] Initialize TLS
└─ [RARE] Large object allocation
```

### 4. 内存效率 (Memory Efficiency)

**原则**: 平衡性能和内存开销

**实现策略**:
- Block 大小优化（16 KB）
- 智能缓存策略（有上限）
- 延迟释放回操作系统
- 内存合并减少碎片

---

## 🏗️ 三层架构

### 层次结构

```
┌────────────────────────────────────────────────────────┐
│                   LAYER 1: Frontend                     │
│                  (User-Facing API)                      │
│  ┌──────────┐  ┌──────────┐  ┌────────────┐           │
│  │ malloc() │  │  free()  │  │ realloc()  │           │
│  └────┬─────┘  └────┬─────┘  └─────┬──────┘           │
│       │             │               │                   │
│       └─────────────┼───────────────┘                   │
└─────────────────────┼─────────────────────────────────┘
                      │
┌─────────────────────┼─────────────────────────────────┐
│                     ▼        LAYER 2: Middle           │
│          Thread Local Storage (TLS)                    │
│  ┌─────────────────────────────────────────────────┐  │
│  │  Per-Thread Data Structures                     │  │
│  │  ┌────────┐  ┌──────────┐  ┌────────────────┐  │  │
│  │  │ Bins   │  │ Block    │  │ Local Caches   │  │  │
│  │  │ Array  │  │ Pool     │  │ (LOC)          │  │  │
│  │  └───┬────┘  └────┬─────┘  └───────┬────────┘  │  │
│  └──────┼────────────┼─────────────────┼───────────┘  │
│         │            │                 │              │
│  ┌──────┼────────────┼─────────────────┼───────────┐  │
│  │      ▼            ▼                 ▼           │  │
│  │  Shared Structures (Careful Synchronization)   │  │
│  │  ┌──────────┐  ┌───────────┐  ┌─────────────┐ │  │
│  │  │ Orphaned │  │  Global   │  │  Mailbox    │ │  │
│  │  │  Blocks  │  │  Caches   │  │  System     │ │  │
│  │  └──────────┘  └───────────┘  └─────────────┘ │  │
│  └─────────────────────┬──────────────────────────┘  │
└────────────────────────┼────────────────────────────┘
                         │
┌────────────────────────┼────────────────────────────┐
│                        ▼       LAYER 3: Backend     │
│              Operating System Interface             │
│  ┌────────────┐  ┌──────────────┐  ┌────────────┐ │
│  │   mmap     │  │  VirtualAlloc │  │ Huge Pages │ │
│  │  (Linux)   │  │  (Windows)    │  │  Support   │ │
│  └────────────┘  └──────────────┘  └────────────┘ │
│  ┌────────────┐  ┌──────────────┐  ┌────────────┐ │
│  │ Coalescing │  │   Address    │  │  Memory    │ │
│  │  Engine    │  │   Tracking   │  │  Regions   │ │
│  └────────────┘  └──────────────┘  └────────────┘ │
└──────────────────────────────────────────────────────┘
```

---

## 🔄 核心数据流

### 分配流程

```
┌─────────────────────────────────────────────────────────┐
│ 1. malloc(size) 调用                                     │
└────────────────┬────────────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────────────┐
│ 2. 大小分类                                              │
│    if (size <= 8128) → 小对象                           │
│    else              → 大对象                           │
└────────────────┬────────────────────────────────────────┘
                 │
         ┌───────┴────────┐
         ▼                ▼
┌──────────────┐  ┌──────────────────────────────────────┐
│ 小对象路径    │  │ 大对象路径                            │
│              │  │                                       │
│ 3a. 计算bin  │  │ 3b. 检查 Local LOC                    │
│     索引      │  │     如果命中 → 返回                   │
│              │  │     未命中 ↓                          │
│ 4a. 获取TLS  │  │                                       │
│              │  │ 4b. 检查 Global LOC                   │
│ 5a. 从bin    │  │     如果命中 → 返回                   │
│     的active │  │     未命中 ↓                          │
│     block    │  │                                       │
│     分配     │  │ 5b. 从 Backend 分配                   │
│              │  │     - mmap/VirtualAlloc               │
│ 6a. 成功?    │  │     - 对齐到页边界                    │
│     是 → 返回│  │     - 注册 backref                    │
│     否 ↓     │  │                                       │
│              │  │ 6b. 添加到 LOC                        │
│ 7a. 获取新   │  │                                       │
│     block    │  │ 7b. 返回给用户                        │
│     从池/后端│  │                                       │
│              │  │                                       │
│ 8a. 设置为   │  └───────────────────────────────────────┘
│     active   │
│              │
│ 9a. 重试分配 │
└──────────────┘
```

### 释放流程

```
┌─────────────────────────────────────────────────────────┐
│ 1. free(ptr) 调用                                        │
└────────────────┬────────────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────────────┐
│ 2. 查找对象元数据                                        │
│    - 通过 BackRef 索引                                   │
│    - 或通过地址计算                                      │
└────────────────┬────────────────────────────────────────┘
                 │
         ┌───────┴────────┐
         ▼                ▼
┌──────────────┐  ┌──────────────────────────────────────┐
│ 小对象释放    │  │ 大对象释放                            │
│              │  │                                       │
│ 3a. 获取所属 │  │ 3b. 减少引用计数                      │
│     Block    │  │                                       │
│              │  │ 4b. 尝试添加到 LOC                    │
│ 4a. 同线程?  │  │     - 检查缓存大小                    │
│              │  │     - 检查缓存数量                    │
│ 5a. 是:      │  │                                       │
│     添加到   │  │ 5b. LOC 满?                           │
│     freeList │  │     否 → 缓存                         │
│              │  │     是 ↓                              │
│ 5b. 否:      │  │                                       │
│     CAS 添加 │  │ 6b. 合并相邻块                        │
│     到 public│  │                                       │
│     FreeList │  │ 7b. 返回给 Backend                    │
│              │  │     - munmap/VirtualFree              │
│ 6a. Block空? │  │                                       │
│     是 ↓     │  │                                       │
│              │  └───────────────────────────────────────┘
│ 7a. 通过     │
│     Mailbox  │
│     归还     │
└──────────────┘
```

---

## 🧩 模块划分

### Module 1: Frontend (前端接口)

**职责**:
- 提供标准 malloc/free/realloc API
- 参数验证
- 大小分类和路由
- 快速路径内联

**文件**:
- `frontend.h` - 公共接口声明
- `frontend.cpp` - API 实现
- `malloc_export.h` - 符号导出

**关键函数**:
```cpp
void* corona_malloc(size_t size);
void  corona_free(void* ptr);
void* corona_realloc(void* ptr, size_t size);
void* corona_aligned_malloc(size_t size, size_t alignment);
```

### Module 2: Thread Local Storage (线程本地)

**职责**:
- 管理每线程数据结构
- Bin 数组管理
- Block 池管理
- 本地缓存

**文件**:
- `thread_local.h` - TLS 数据结构
- `thread_local.cpp` - TLS 管理
- `bin.h` / `bin.cpp` - Bin 实现

**核心结构**:
```cpp
struct TLSData {
    Bin bins[31];           // 小对象 bins
    BlockPool block_pool;   // Block 缓存
    LocalLOC loc;           // 大对象本地缓存
    MemoryPool* pool;       // 所属内存池
};
```

### Module 3: Block Management (块管理)

**职责**:
- Block 生命周期管理
- Bump pointer 分配
- FreeList 管理
- Public FreeList 处理

**文件**:
- `block.h` - Block 结构定义
- `block.cpp` - Block 操作实现
- `free_list.h` - 空闲链表

**核心结构**:
```cpp
class Block {
    // 热路径字段（第一缓存行）
    FreeObject* bump_ptr;
    FreeObject* free_list;
    std::atomic<FreeObject*> public_free_list;
    
    // 元数据（第二缓存行）
    uint16_t object_size;
    uint16_t allocated_count;
    TLSData* owner_tls;
};
```

### Module 4: Backend (后端)

**职责**:
- 操作系统内存分配
- 内存区域管理
- 地址空间跟踪
- Huge Pages 支持

**文件**:
- `backend.h` - 后端接口
- `backend.cpp` - 后端实现
- `os_mapping.h` - OS 特定内存映射

**平台抽象**:
```cpp
void* map_memory(size_t size, PageType type);
bool unmap_memory(void* addr, size_t size);
size_t get_page_size();
bool supports_huge_pages();
```

### Module 5: Large Objects (大对象)

**职责**:
- 大对象分配
- 多级缓存（Local + Global）
- BackRef 系统
- 对象合并

**文件**:
- `large_objects.h` - 大对象结构
- `large_objects.cpp` - 大对象管理
- `large_cache.h` - 缓存实现
- `backref.h` / `backref.cpp` - 反向引用

### Module 6: Synchronization (同步)

**职责**:
- 无锁数据结构
- 原子操作封装
- 内存屏障管理
- 锁实现

**文件**:
- `atomic_ops.h` - 原子操作
- `spinlock.h` - 自旋锁
- `mutex.h` - 互斥锁
- `lockfree_stack.h` - 无锁栈

---

## 📦 关键接口定义

### 1. TLS 接口

```cpp
class TLSManager {
public:
    // 初始化线程本地存储
    static bool initialize();
    
    // 获取当前线程的 TLS
    static TLSData* get_tls();
    
    // 创建新的 TLS（首次访问）
    static TLSData* create_tls(MemoryPool* pool);
    
    // 销毁 TLS（线程退出）
    static void destroy_tls(TLSData* tls);
    
    // 清理未使用的 TLS
    static void cleanup_unused();
};
```

### 2. Block 接口

```cpp
class Block {
public:
    // 从 block 分配对象
    void* allocate();
    
    // 本地释放（同线程）
    void free_local(void* ptr);
    
    // 跨线程释放
    void free_public(void* ptr);
    
    // 私有化 public free list
    void privatize_public_list();
    
    // 检查是否为空
    bool is_empty() const;
    
    // 检查是否可以释放
    bool can_be_released() const;
};
```

### 3. Backend 接口

```cpp
class Backend {
public:
    // 初始化后端
    bool initialize(size_t granularity);
    
    // 分配原始内存
    void* allocate_raw(size_t& size);
    
    // 释放原始内存
    bool free_raw(void* ptr, size_t size);
    
    // 获取 Block（16KB）
    Block* get_slab_block();
    
    // 归还 Block
    void return_slab_block(Block* block);
    
    // 内存合并
    void coalesce_free_blocks();
};
```

---

## 🔧 配置系统

### 编译时配置

```cpp
// config.h
namespace config {
    // Block 大小（必须是 2 的幂）
    constexpr size_t SLAB_SIZE = 16 * 1024;
    
    // 缓存行大小
    constexpr size_t CACHE_LINE_SIZE = 128;
    
    // Bin 数量
    constexpr size_t NUM_BINS = 31;
    
    // Block 池大小限制
    constexpr size_t BLOCK_POOL_HIGH_MARK = 32;
    constexpr size_t BLOCK_POOL_LOW_MARK = 8;
    
    // 大对象缓存限制
    constexpr size_t LOCAL_LOC_MAX_SIZE = 4 * 1024 * 1024;
    constexpr size_t LOCAL_LOC_MAX_COUNT = 32;
    
    // 启用调试模式
    constexpr bool DEBUG_MODE = false;
    
    // 启用统计信息
    constexpr bool COLLECT_STATS = false;
}
```

### 运行时配置

```cpp
// 通过环境变量控制
// MALLOC_USE_HUGE_PAGES=1
// MALLOC_VERBOSE=1
// MALLOC_STATS=1
// MALLOC_DEBUG=1

class RuntimeConfig {
public:
    static bool use_huge_pages();
    static bool verbose_mode();
    static bool collect_statistics();
    static size_t huge_page_threshold();
};
```

---

## 📈 性能监控

### 统计信息收集

```cpp
struct AllocatorStats {
    // 分配统计
    std::atomic<uint64_t> total_allocations;
    std::atomic<uint64_t> total_frees;
    std::atomic<uint64_t> active_allocations;
    
    // 大小分布
    std::atomic<uint64_t> small_obj_count;
    std::atomic<uint64_t> large_obj_count;
    
    // 缓存命中率
    std::atomic<uint64_t> cache_hits;
    std::atomic<uint64_t> cache_misses;
    
    // 跨线程操作
    std::atomic<uint64_t> cross_thread_frees;
    
    // 内存使用
    std::atomic<uint64_t> total_memory_mapped;
    std::atomic<uint64_t> total_memory_used;
    
    void print_report() const;
};
```

---

## 🎯 下一步

阅读下一个文档：**[02_DATA_STRUCTURES.md](./02_DATA_STRUCTURES.md)** - 详细的数据结构设计

---

**文档版本**: 1.0  
**最后更新**: 2025年10月31日
