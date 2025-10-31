# 03 - 前端实现详解

## 🎯 前端职责

前端层是用户与分配器交互的入口，负责：
- 提供标准 C/C++ API
- 快速路径优化（inline + 分支预测）
- 参数验证和错误处理
- 大小分类和路由

---

## 📚 公共 API 设计

### 核心分配函数

```cpp
// include/corona_malloc.h

#ifdef __cplusplus
extern "C" {
#endif

// 基本分配
void* corona_malloc(size_t size);
void* corona_calloc(size_t num, size_t size);
void* corona_realloc(void* ptr, size_t size);
void  corona_free(void* ptr);

// 对齐分配
void* corona_aligned_malloc(size_t size, size_t alignment);
void* corona_aligned_realloc(void* ptr, size_t size, size_t alignment);
void  corona_aligned_free(void* ptr);

// POSIX 接口
int   corona_posix_memalign(void** memptr, size_t alignment, size_t size);

// 查询接口
size_t corona_msize(void* ptr);           // 获取对象大小
int    corona_allocation_mode(int param, intptr_t value);  // 控制参数

#ifdef __cplusplus
}
#endif
```

---

## ⚡ malloc 实现 - 快速路径优化

### 完整实现

```cpp
void* corona_malloc(size_t size) {
    // === 第一步：参数验证（分支预测优化）===
    if __unlikely(size == 0) {
        return nullptr;  // 或返回最小分配
    }
    
    if __unlikely(size > MAX_ALLOCATION_SIZE) {
        errno = ENOMEM;
        return nullptr;
    }
    
    // === 第二步：大小分类 ===
    if __likely(size <= config::MAX_SMALL_OBJECT_SIZE) {
        // 快速路径：小对象（99% 的情况）
        return allocate_small_object(size);
    } else {
        // 慢速路径：大对象
        return allocate_large_object(size);
    }
}

// 内联的快速路径
__attribute__((always_inline))
inline void* allocate_small_object(size_t size) {
    // 1. 获取 TLS（通常命中 CPU 寄存器/L1 缓存）
    TLSData* tls = get_thread_tls_fast();
    
    if __unlikely(!tls) {
        // 首次调用，需要初始化
        tls = initialize_thread_tls();
        if (!tls) return nullptr;
    }
    
    // 2. 计算 bin 索引（编译期优化）
    size_t bin_idx = get_bin_index(size);
    Bin* bin = &tls->bins[bin_idx];
    
    // 3. 从 active block 分配
    Block* block = bin->active_block;
    
    if __likely(block) {
        void* obj = block->try_allocate_fast();
        if __likely(obj) {
            // 最快路径成功！
            return obj;
        }
    }
    
    // 4. 慢速路径：需要获取新 block
    return allocate_from_bin_slow(tls, bin, size);
}
```

### 性能优化技巧

#### 1. 分支预测提示

```cpp
// 定义宏
#define __likely(x)   __builtin_expect(!!(x), 1)
#define __unlikely(x) __builtin_expect(!!(x), 0)

// 使用示例
if __likely(fast_path_condition) {
    // 编译器会优化为顺序执行，无跳转
    return fast_result;
}
```

#### 2. TLS 访问优化

```cpp
// 使用 __thread 关键字（GCC/Clang）
__thread TLSData* g_thread_tls = nullptr;

inline TLSData* get_thread_tls_fast() {
    // 直接访问 TLS 变量，非常快
    return g_thread_tls;
}

// Windows 版本
__declspec(thread) TLSData* g_thread_tls = nullptr;
```

#### 3. 内联和强制优化

```cpp
// 强制内联热路径函数
__attribute__((always_inline)) 
inline void* Block::try_allocate_fast() {
    // 实现...
}

// 禁止内联冷路径函数
__attribute__((noinline))
void* allocate_from_bin_slow(TLSData* tls, Bin* bin, size_t size) {
    // 实现...
}
```

---

## 🔄 free 实现 - 零竞争释放

### 完整实现

```cpp
void corona_free(void* ptr) {
    // === 快速退出 ===
    if __unlikely(!ptr) {
        return;  // nullptr 释放合法
    }
    
    // === 识别对象类型 ===
    // 方法1：通过地址范围判断
    if (is_in_slab_range(ptr)) {
        free_small_object(ptr);
    } else {
        free_large_object(ptr);
    }
}

__attribute__((always_inline))
inline void free_small_object(void* ptr) {
    // 1. 获取 Block（通过地址对齐）
    Block* block = Block::from_object(ptr);
    
    // 2. 获取当前线程 TLS
    TLSData* current_tls = get_thread_tls_fast();
    
    // 3. 检查是否同线程
    if __likely(block->owner_tls == current_tls) {
        // 快速路径：本地释放
        block->free_local(ptr);
    } else {
        // 慢速路径：跨线程释放
        block->free_public(ptr);
    }
}
```

### 地址范围检查优化

```cpp
class AddressRange {
private:
    std::atomic<uintptr_t> min_addr;
    std::atomic<uintptr_t> max_addr;
    
public:
    void register_range(void* start, size_t size) {
        uintptr_t addr_start = reinterpret_cast<uintptr_t>(start);
        uintptr_t addr_end = addr_start + size;
        
        // 原子更新范围
        atomic_min(min_addr, addr_start);
        atomic_max(max_addr, addr_end);
    }
    
    bool contains(void* ptr) const {
        uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        return addr >= min_addr.load(std::memory_order_relaxed) &&
               addr < max_addr.load(std::memory_order_relaxed);
    }
};

// 全局地址范围（用于小对象）
extern AddressRange g_slab_address_range;

inline bool is_in_slab_range(void* ptr) {
    return g_slab_address_range.contains(ptr);
}
```

---

## 🔄 realloc 实现 - 智能调整

### 策略决策树

```
realloc(ptr, new_size):
├─ ptr == nullptr?
│  └─ [Yes] → malloc(new_size)
│
├─ new_size == 0?
│  └─ [Yes] → free(ptr); return nullptr
│
├─ old_size == new_size?
│  └─ [Yes] → return ptr (无操作)
│
├─ 是小对象 && new_size 在同一 bin?
│  └─ [Yes] → return ptr (原地复用)
│
├─ 是大对象 && new_size 在缓存容差内?
│  └─ [Yes] → return ptr (原地复用)
│
├─ new_size < old_size?
│  ├─ 缩小比例 > 50%?
│  │  ├─ [Yes] → 分配新对象，复制，释放旧对象
│  │  └─ [No] → return ptr (容忍浪费)
│  └─
│
└─ [Default] → 分配新对象，复制，释放旧对象
```

### 实现代码

```cpp
void* corona_realloc(void* ptr, size_t new_size) {
    // 特殊情况处理
    if (!ptr) return corona_malloc(new_size);
    if (new_size == 0) {
        corona_free(ptr);
        return nullptr;
    }
    
    // 获取旧对象大小
    size_t old_size = get_object_size(ptr);
    
    // 大小相同或在同一 bin 内
    if (new_size == old_size || 
        same_bin(old_size, new_size)) {
        return ptr;  // 原地复用
    }
    
    // 大对象可能支持原地扩展
    if (is_large_object(ptr) && new_size > old_size) {
        void* expanded = try_expand_in_place(ptr, new_size);
        if (expanded) return expanded;
    }
    
    // 缩小且浪费不大
    if (new_size < old_size && new_size > old_size / 2) {
        return ptr;  // 接受一定的浪费
    }
    
    // 分配新对象
    void* new_ptr = corona_malloc(new_size);
    if (!new_ptr) return nullptr;
    
    // 复制数据
    size_t copy_size = (new_size < old_size) ? new_size : old_size;
    memcpy(new_ptr, ptr, copy_size);
    
    // 释放旧对象
    corona_free(ptr);
    
    return new_ptr;
}
```

---

## 📏 对齐分配实现

### aligned_malloc

```cpp
void* corona_aligned_malloc(size_t size, size_t alignment) {
    // 参数验证
    if (!is_power_of_two(alignment)) {
        errno = EINVAL;
        return nullptr;
    }
    
    if (alignment < sizeof(void*)) {
        alignment = sizeof(void*);
    }
    
    // 小对齐值，使用标准分配
    if (alignment <= config::CACHE_LINE_SIZE) {
        // 我们的分配器已经保证缓存行对齐
        return corona_malloc(size);
    }
    
    // 大对齐值，使用特殊处理
    return allocate_with_alignment(size, alignment);
}

void* allocate_with_alignment(size_t size, size_t alignment) {
    // 分配额外空间存储偏移量
    size_t total_size = size + alignment + sizeof(AlignmentHeader);
    void* raw_ptr = corona_malloc(total_size);
    if (!raw_ptr) return nullptr;
    
    // 计算对齐地址
    uintptr_t raw_addr = reinterpret_cast<uintptr_t>(raw_ptr);
    uintptr_t aligned_addr = align_up(
        raw_addr + sizeof(AlignmentHeader), 
        alignment
    );
    
    // 存储原始指针
    AlignmentHeader* header = 
        reinterpret_cast<AlignmentHeader*>(aligned_addr - sizeof(AlignmentHeader));
    header->original_ptr = raw_ptr;
    header->magic = ALIGNMENT_MAGIC;
    
    return reinterpret_cast<void*>(aligned_addr);
}

void corona_aligned_free(void* ptr) {
    if (!ptr) return;
    
    // 检查是否是对齐分配
    AlignmentHeader* header = 
        reinterpret_cast<AlignmentHeader*>(
            reinterpret_cast<uintptr_t>(ptr) - sizeof(AlignmentHeader)
        );
    
    if (header->magic == ALIGNMENT_MAGIC) {
        // 对齐分配，释放原始指针
        corona_free(header->original_ptr);
    } else {
        // 标准分配
        corona_free(ptr);
    }
}
```

---

## 🔍 对象大小查询

### corona_msize 实现

```cpp
size_t corona_msize(void* ptr) {
    if (!ptr) return 0;
    
    if (is_in_slab_range(ptr)) {
        // 小对象：从 Block 获取
        Block* block = Block::from_object(ptr);
        return block->get_object_size();
    } else {
        // 大对象：从 header 获取
        LargeObjectHdr* hdr = LargeObjectHdr::from_user_ptr(ptr);
        return hdr->memory_block->object_size;
    }
}
```

---

## 🎛️ 控制接口

### corona_allocation_mode

```cpp
enum AllocationMode {
    TBBMALLOC_USE_HUGE_PAGES = 1,
    TBBMALLOC_SET_SOFT_HEAP_LIMIT = 2,
    // ... 更多模式
};

int corona_allocation_mode(int param, intptr_t value) {
    switch (param) {
    case TBBMALLOC_USE_HUGE_PAGES:
        g_huge_pages_status.set_mode(value);
        return 0;
        
    case TBBMALLOC_SET_SOFT_HEAP_LIMIT:
        g_memory_limit.set(value);
        return 0;
        
    default:
        errno = EINVAL;
        return -1;
    }
}
```

---

## 🔧 初始化和清理

### 自动初始化

```cpp
// 使用构造函数属性自动初始化
__attribute__((constructor))
static void allocator_init() {
    // 初始化后端
    if (!g_backend.initialize()) {
        fprintf(stderr, "Failed to initialize allocator backend\n");
        abort();
    }
    
    // 初始化 TLS 键
    if (!TLSManager::initialize()) {
        fprintf(stderr, "Failed to initialize TLS\n");
        abort();
    }
    
    // 初始化 BackRef 系统
    if (!BackRefMain::initialize()) {
        fprintf(stderr, "Failed to initialize BackRef\n");
        abort();
    }
    
    // 打印版本信息（如果启用）
    if (getenv("SCALABLE_MALLOC_VERBOSE")) {
        printf("Scalable Malloc v1.0 initialized\n");
    }
}

// 清理
__attribute__((destructor))
static void allocator_fini() {
    // 清理所有 TLS
    TLSManager::cleanup_all();
    
    // 释放后端资源
    g_backend.shutdown();
}
```

---

## 🚀 性能测量点

### 内置性能计数器

```cpp
#if COLLECT_STATS

struct AllocatorStats {
    std::atomic<uint64_t> malloc_calls;
    std::atomic<uint64_t> free_calls;
    std::atomic<uint64_t> cache_hits;
    std::atomic<uint64_t> cache_misses;
    
    void record_malloc() {
        malloc_calls.fetch_add(1, std::memory_order_relaxed);
    }
    
    void record_cache_hit() {
        cache_hits.fetch_add(1, std::memory_order_relaxed);
    }
    
    void print() {
        printf("Malloc calls: %lu\n", malloc_calls.load());
        printf("Cache hit rate: %.2f%%\n", 
               100.0 * cache_hits.load() / malloc_calls.load());
    }
};

extern AllocatorStats g_stats;

#define STAT_RECORD_MALLOC() g_stats.record_malloc()
#define STAT_RECORD_HIT() g_stats.record_cache_hit()

#else

#define STAT_RECORD_MALLOC() ((void)0)
#define STAT_RECORD_HIT() ((void)0)

#endif
```

---

## 🎯 关键性能优化总结

| 优化技术 | 影响 | 实现难度 |
|---------|------|---------|
| __likely/__unlikely 分支预测 | +5-10% | 简单 |
| TLS 变量快速访问 | +10-15% | 简单 |
| 内联热路径函数 | +10-20% | 简单 |
| Bump pointer 分配 | +30-50% | 中等 |
| 避免跨线程同步 | +100-300% | 困难 |
| 缓存行对齐 | +10-30% | 中等 |

---

## 🎯 下一步

阅读下一个文档：**[04_THREAD_LOCAL.md](./04_THREAD_LOCAL.md)** - 线程本地存储详细设计

---

**文档版本**: 1.0  
**最后更新**: 2025年10月31日
