# 02 - 数据结构设计

## 📊 核心数据结构详解

本文档详细描述分配器中所有关键数据结构的设计、内存布局和操作接口。

---

## 🧱 Block - 小对象容器

### 设计目标

- **固定大小**: 16 KB（4个页面）
- **缓存友好**: Header 对齐到缓存行
- **高效分配**: Bump pointer + FreeList
- **并发安全**: 支持跨线程释放

### 数据结构定义

```cpp
class alignas(128) Block {
private:
    // === 第一缓存行：热路径字段（最频繁访问）===
    FreeObject*     bump_ptr;           // Bump pointer 分配器
    FreeObject*     free_list;          // 本地空闲链表
    std::atomic<FreeObject*> public_free_list; // 跨线程释放链表
    Block*          next;               // 链表指针
    Block*          previous;           // 双向链表
    
    // === 第二缓存行：元数据 ===
    uint16_t        object_size;        // 对象大小（字节）
    uint16_t        allocated_count;    // 已分配对象计数
    uint16_t        _padding1;          // 对齐填充
    uint16_t        _padding2;
    
    std::atomic<TLSData*> owner_tls;    // 所有者线程 TLS
    MemoryPool*     pool;               // 所属内存池
    BackRefIdx      backref_idx;        // 反向引用索引
    ThreadId        owner_tid;          // 所有者线程 ID
    
public:
    // 初始化为空 block
    void init_empty(size_t obj_size, TLSData* tls);
    
    // 分配操作
    void* allocate();
    void* try_allocate_fast();
    
    // 释放操作
    void free_local(void* ptr);
    void free_public(void* ptr);
    
    // 状态查询
    bool is_empty() const;
    bool is_full() const;
    bool has_free_space() const;
    size_t get_available_count() const;
    
    // 管理操作
    void privatize_public_free_list();
    void reset();
};

static_assert(sizeof(Block) == 128, "Block must be exactly 128 bytes");
```

### 内存布局

```
Block 结构（16 KB 总大小）：

偏移 0x0000: [Block Header - 128 bytes]
  ├─ 0x0000-0x003F: 第一缓存行（64字节）
  │    ├─ bump_ptr (8 bytes)
  │    ├─ free_list (8 bytes)
  │    ├─ public_free_list (8 bytes)
  │    ├─ next (8 bytes)
  │    └─ previous (8 bytes)
  │
  └─ 0x0040-0x007F: 第二缓存行（64字节）
       ├─ object_size (2 bytes)
       ├─ allocated_count (2 bytes)
       ├─ padding (4 bytes)
       ├─ owner_tls (8 bytes)
       ├─ pool (8 bytes)
       ├─ backref_idx (8 bytes)
       └─ owner_tid (16 bytes)

偏移 0x0080: [Object Area - 16256 bytes]
  ├─ Object 1 (object_size bytes)
  ├─ Object 2 (object_size bytes)
  ├─ ...
  └─ Object N (最后一个对象)

总大小: 16384 bytes (16 KB)
```

### 关键操作实现

#### 1. Bump Pointer 分配（最快）

```cpp
inline void* Block::try_allocate_fast() {
    if (!bump_ptr) return nullptr;
    
    void* result = bump_ptr;
    
    // 计算下一个位置
    uintptr_t next_pos = reinterpret_cast<uintptr_t>(bump_ptr) 
                        + object_size;
    uintptr_t block_end = reinterpret_cast<uintptr_t>(this) 
                         + SLAB_SIZE;
    
    if (next_pos <= block_end) {
        bump_ptr = reinterpret_cast<FreeObject*>(next_pos);
        allocated_count++;
        return result;
    }
    
    // Bump pointer 耗尽
    bump_ptr = nullptr;
    return nullptr;
}
```

#### 2. FreeList 分配（次快）

```cpp
inline void* Block::allocate() {
    // 1. 尝试 bump pointer
    void* obj = try_allocate_fast();
    if (obj) return obj;
    
    // 2. 尝试本地 free list
    if (free_list) {
        void* obj = free_list;
        free_list = free_list->next;
        allocated_count++;
        return obj;
    }
    
    // 3. 私有化 public free list
    privatize_public_free_list();
    if (free_list) {
        void* obj = free_list;
        free_list = free_list->next;
        allocated_count++;
        return obj;
    }
    
    return nullptr; // Block 已满
}
```

#### 3. 本地释放

```cpp
inline void Block::free_local(void* ptr) {
    FreeObject* obj = static_cast<FreeObject*>(ptr);
    
    // 添加到 free list 头部（LIFO）
    obj->next = free_list;
    free_list = obj;
    
    allocated_count--;
}
```

#### 4. 跨线程释放（无锁）

```cpp
inline void Block::free_public(void* ptr) {
    FreeObject* obj = static_cast<FreeObject*>(ptr);
    
    // CAS 循环添加到 public free list
    FreeObject* old_head = public_free_list.load(
        std::memory_order_acquire
    );
    
    do {
        obj->next = old_head;
    } while (!public_free_list.compare_exchange_weak(
        old_head, obj,
        std::memory_order_release,
        std::memory_order_acquire
    ));
}
```

---

## 🗂️ Bin - 大小类管理

### 设计目标

- 快速查找 active block
- 管理同一大小类的多个 blocks
- 处理 mailbox 机制
- 支持 block 迁移

### 数据结构定义

```cpp
class Bin {
private:
    Block*          active_block;       // 当前活跃 block
    std::atomic<Block*> mailbox;        // 跨线程传递 block
    MallocMutex     mailbox_lock;       // 保护 mailbox 操作
    
public:
    // 获取对象
    void* allocate(size_t size);
    
    // 设置 active block
    void set_active_block(Block* block);
    
    // 从 mailbox 接收 blocks
    void process_mailbox();
    
    // 添加 block 到 mailbox
    void push_to_mailbox(Block* block);
    
    // 清理 public free lists
    bool cleanup_public_lists();
    
    // 验证状态
    void verify_integrity() const;
};
```

### Bin 索引计算

```cpp
// 小对象：8-64 字节，8 个 bins
inline size_t get_small_bin_index(size_t size) {
    // size: 1-8   -> bin 0
    // size: 9-16  -> bin 1
    // size: 17-24 -> bin 3 (64位系统16字节对齐)
    // ...
    size_t index = (size - 1) >> 3;
    
    #if ARCH_64BIT
    // 强制 16 字节对齐（除了 bin 0）
    if (index > 0) index |= 1;
    #endif
    
    return index;
}

// 分段对象：80-1024 字节，16 个 bins
inline size_t get_segregated_bin_index(size_t size) {
    // 计算是哪个 2 的幂次组
    size_t order = highest_bit_pos(size - 1);
    // order = 6: 64-128   (bins for 80,96,112,128)
    // order = 7: 128-256  (bins for 160,192,224,256)
    // order = 8: 256-512  (bins for 320,384,448,512)
    // order = 9: 512-1024 (bins for 640,768,896,1024)
    
    size_t alignment = 128 >> (9 - order);
    size_t base_idx = 8 + (order - 6) * 4;
    size_t offset = ((size - 1) >> (order - 2)) & 3;
    
    return base_idx + offset;
}

// 拟合对象：1792-8128 字节，5 个 bins
inline size_t get_fitting_bin_index(size_t size) {
    if (size <= 1792) return 24;
    if (size <= 2688) return 25;
    if (size <= 4032) return 26;
    if (size <= 5376) return 27;
    return 28; // size <= 8128
}

// 统一接口
inline size_t get_bin_index(size_t size) {
    if (size <= 64) {
        return get_small_bin_index(size);
    } else if (size <= 1024) {
        return get_segregated_bin_index(size);
    } else if (size <= 8128) {
        return get_fitting_bin_index(size);
    }
    return INVALID_BIN; // 大对象
}
```

---

## 🧵 TLSData - 线程本地存储

### 设计目标

- 完全线程本地，无竞争
- 包含所有快速路径数据
- 支持延迟初始化
- 可被其他线程清理

### 数据结构定义

```cpp
class alignas(128) TLSData {
private:
    // === 核心数据 ===
    Bin             bins[NUM_BINS];     // 31 个 bins
    BlockPool       block_pool;         // Block 缓存池
    LocalLOC        local_loc;          // 大对象本地缓存
    
    // === 元数据 ===
    MemoryPool*     memory_pool;        // 所属内存池
    std::atomic<bool> is_unused;        // 是否未使用
    uint32_t        cache_cleanup_idx;  // 清理索引
    
    // === 链表节点（用于全局 TLS 列表）===
    TLSData*        next;
    TLSData*        prev;
    
public:
    // 初始化
    void initialize(MemoryPool* pool);
    
    // 获取 bin
    Bin* get_bin(size_t size);
    
    // 分配对象
    void* allocate(size_t size);
    
    // 释放对象
    void free(void* ptr);
    
    // 标记为未使用
    void mark_unused();
    
    // 清理缓存
    bool cleanup(bool force);
    
    // 释放所有资源
    void release();
};
```

### 内存布局

```
TLSData 结构（约 4-8 KB）：

[Bin Array - 31 bins × ~64 bytes = ~2KB]
  ├─ Bin[0]:  8 字节对象
  ├─ Bin[1]:  16 字节对象
  ├─ Bin[3]:  24 字节对象
  ├─ ...
  └─ Bin[28]: 8128 字节对象

[BlockPool - ~1KB]
  ├─ Block* pool[32]
  ├─ pool_size
  └─ statistics

[LocalLOC - ~1KB]
  ├─ LargeBlock* head
  ├─ LargeBlock* tail
  ├─ total_size
  └─ block_count

[Metadata - ~128 bytes]
  ├─ memory_pool
  ├─ is_unused
  ├─ cache_cleanup_idx
  ├─ next/prev pointers
  └─ padding
```

---

## 🎒 BlockPool - Block 缓存池

### 设计目标

- 避免频繁的 backend 调用
- LIFO 策略提高缓存局部性
- 有上限避免内存浪费
- 支持外部清理

### 数据结构定义

```cpp
class BlockPool {
private:
    static constexpr size_t HIGH_MARK = 32;  // 上限
    static constexpr size_t LOW_MARK = 8;    // 清理后保留
    
    std::atomic<Block*> head;               // 池头部
    int                 size;               // 当前大小
    Backend*            backend;            // 后端引用
    
public:
    BlockPool(Backend* bknd) : backend(bknd), size(0) {
        head.store(nullptr, std::memory_order_relaxed);
    }
    
    // 获取 block（可能触发分配）
    struct GetResult {
        Block* block;
        bool cache_miss;
    };
    GetResult get_block();
    
    // 归还 block
    void return_block(Block* block);
    
    // 外部清理（可被其他线程调用）
    bool external_cleanup();
    
    // 完全清理
    void cleanup_all();
};
```

### 实现细节

```cpp
BlockPool::GetResult BlockPool::get_block() {
    Block* block = nullptr;
    bool miss = false;
    
    // 尝试从池中获取
    if (head.load(std::memory_order_acquire)) {
        // 这里需要加锁，因为可能有外部清理
        MallocMutex::scoped_lock lock(pool_lock);
        block = head.load(std::memory_order_relaxed);
        if (block) {
            head.store(block->next, std::memory_order_relaxed);
            size--;
        }
    }
    
    // 池为空，从 backend 分配
    if (!block) {
        block = backend->get_slab_block();
        miss = true;
    }
    
    return {block, miss};
}

void BlockPool::return_block(Block* block) {
    if (size >= HIGH_MARK) {
        // 池已满，直接归还 backend
        backend->return_slab_block(block);
        return;
    }
    
    MallocMutex::scoped_lock lock(pool_lock);
    block->next = head.load(std::memory_order_relaxed);
    head.store(block, std::memory_order_relaxed);
    size++;
}
```

---

## 🏢 LargeMemoryBlock - 大对象块

### 设计目标

- 支持任意大小分配
- 高效的缓存和复用
- BackRef 快速查找
- 支持合并减少碎片

### 数据结构定义

```cpp
struct LargeMemoryBlock {
    // === 链表指针 ===
    LargeMemoryBlock* next;             // 缓存链表
    LargeMemoryBlock* prev;
    LargeMemoryBlock* global_next;      // 全局链表
    LargeMemoryBlock* global_prev;
    
    // === 元数据 ===
    MemoryPool*       pool;             // 所属池
    size_t            object_size;      // 用户请求大小
    size_t            unaligned_size;   // 实际分配大小
    uintptr_t         age;              // 缓存年龄
    BackRefIdx        backref_idx;      // 反向引用
    
    // === 用户数据起始位置 ===
    // char user_data[object_size];
    
    // 获取用户数据指针
    void* get_user_ptr() {
        return reinterpret_cast<void*>(
            reinterpret_cast<uintptr_t>(this) + sizeof(LargeMemoryBlock)
        );
    }
    
    // 从用户指针获取 block
    static LargeMemoryBlock* from_user_ptr(void* ptr) {
        return reinterpret_cast<LargeMemoryBlock*>(
            reinterpret_cast<uintptr_t>(ptr) - sizeof(LargeMemoryBlock)
        );
    }
};
```

### 大对象头部

```cpp
struct LargeObjectHdr {
    LargeMemoryBlock* memory_block;     // 指向 block
    BackRefIdx        backref_idx;      // 快速查找索引
    
    // 放置在用户数据之前
    static LargeObjectHdr* get_header(void* user_ptr) {
        return reinterpret_cast<LargeObjectHdr*>(
            reinterpret_cast<uintptr_t>(user_ptr) 
            - sizeof(LargeObjectHdr)
        );
    }
};
```

### 内存布局

```
大对象内存布局：

[LargeMemoryBlock Header - 72 bytes]
  ├─ next/prev pointers (32 bytes)
  ├─ global_next/prev (16 bytes)
  ├─ pool pointer (8 bytes)
  ├─ object_size (8 bytes)
  ├─ unaligned_size (8 bytes)
  ├─ age (8 bytes)
  └─ backref_idx (8 bytes)

[Alignment Padding - 0-127 bytes]
  (对齐到 estimatedCacheLineSize)

[LargeObjectHdr - 16 bytes]
  ├─ memory_block pointer (8 bytes)
  └─ backref_idx (8 bytes)

[User Data - object_size bytes]
  用户可用空间

[Trailing Padding]
  (对齐到页边界)
```

---

## 🗄️ LocalLOC - 本地大对象缓存

### 设计目标

- Thread-local 缓存大对象
- 限制总大小和数量
- LRU/FIFO 驱逐策略
- 快速查找匹配大小

### 数据结构定义

```cpp
template<int LOW_MARK, int HIGH_MARK>
class LocalLOC {
private:
    static constexpr size_t MAX_TOTAL_SIZE = 4 * 1024 * 1024;  // 4MB
    
    LargeMemoryBlock*   head;               // 链表头
    LargeMemoryBlock*   tail;               // 链表尾
    size_t              total_size;         // 总大小
    int                 block_count;        // 块数量
    
public:
    // 尝试放入缓存
    bool put(LargeMemoryBlock* block);
    
    // 尝试从缓存获取
    LargeMemoryBlock* get(size_t size);
    
    // 外部清理
    bool external_cleanup();
    
    // 清空缓存
    void clear();
};
```

### 实现策略

```cpp
bool LocalLOC::put(LargeMemoryBlock* block) {
    // 检查是否超过限制
    if (block_count >= HIGH_MARK || 
        total_size + block->unaligned_size > MAX_TOTAL_SIZE) {
        return false;  // 不缓存，直接释放
    }
    
    // 添加到头部（最近使用）
    block->next = head;
    block->prev = nullptr;
    if (head) head->prev = block;
    head = block;
    if (!tail) tail = block;
    
    total_size += block->unaligned_size;
    block_count++;
    block->age = 0;
    
    return true;
}

LargeMemoryBlock* LocalLOC::get(size_t size) {
    // 查找匹配大小的块
    for (LargeMemoryBlock* block = head; 
         block; 
         block = block->next) {
        // 允许一定的大小偏差（例如 25%）
        if (block->object_size >= size && 
            block->object_size < size * 1.25) {
            // 从链表移除
            if (block->prev) 
                block->prev->next = block->next;
            else 
                head = block->next;
            
            if (block->next) 
                block->next->prev = block->prev;
            else 
                tail = block->prev;
            
            total_size -= block->unaligned_size;
            block_count--;
            
            return block;
        }
    }
    
    return nullptr;  // 未找到
}
```

---

## 🔗 BackRef - 反向引用系统

### 设计目标

- 从用户指针快速找到元数据
- 支持小对象和大对象
- 内存高效的索引结构
- 线程安全的访问

### 数据结构定义

```cpp
// 索引类型
struct BackRefIdx {
    using MainType = uint32_t;  // 64位系统用uint32_t
    
    MainType    main    : 31;   // 主索引
    uint32_t    is_large: 1;    // 是否大对象
    uint16_t    offset;         // 偏移量
    
    bool is_invalid() const { 
        return main == 0x7FFFFFFF; 
    }
    
    bool is_large_object() const { 
        return is_large; 
    }
};

// BackRef 主表
class BackRefMain {
private:
    struct BackRefBlock {
        void*   blocks[256];        // 指向 Block 或 LargeBlock
        bool    is_large[256];      // 标记类型
    };
    
    BackRefBlock*   table;          // 主表
    size_t          capacity;       // 容量
    std::atomic<size_t> size;       // 当前大小
    MallocMutex     expand_lock;    // 扩容锁
    
public:
    // 分配新索引
    BackRefIdx allocate(void* ptr, bool is_large);
    
    // 查找元数据
    void* lookup(BackRefIdx idx);
    
    // 更新指针
    void update(BackRefIdx idx, void* new_ptr);
    
    // 释放索引
    void free(BackRefIdx idx);
};
```

### 索引计算

```cpp
// 小对象：通过地址计算 Block
inline Block* get_block_from_object(void* obj) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(obj);
    // Block 地址按 16KB 对齐
    return reinterpret_cast<Block*>(addr & ~(SLAB_SIZE - 1));
}

// 大对象：通过 BackRef 查找
inline LargeMemoryBlock* get_large_block(void* obj) {
    LargeObjectHdr* hdr = LargeObjectHdr::get_header(obj);
    BackRefIdx idx = hdr->backref_idx;
    return static_cast<LargeMemoryBlock*>(
        g_backref_main.lookup(idx)
    );
}
```

---

## 📐 FreeObject - 空闲对象节点

### 极简设计

```cpp
struct FreeObject {
    FreeObject* next;  // 单链表，仅需一个指针
};

// 空闲链表操作
inline void push_free_object(FreeObject*& list, void* obj) {
    FreeObject* node = static_cast<FreeObject*>(obj);
    node->next = list;
    list = node;
}

inline void* pop_free_object(FreeObject*& list) {
    if (!list) return nullptr;
    void* obj = list;
    list = list->next;
    return obj;
}
```

---

## 🎯 数据结构大小总结

| 结构 | 大小 | 对齐 | 数量（典型） | 总开销 |
|------|------|------|-------------|--------|
| Block | 128 B | 128 B | 每线程32个 | ~4 KB |
| Bin | 64 B | 64 B | 每线程31个 | ~2 KB |
| TLSData | 4-8 KB | 128 B | 每线程1个 | 4-8 KB |
| LargeMemoryBlock | 72 B | 8 B | 按需 | 可变 |
| BackRefIdx | 8 B | 4 B | 每分配1个 | 可变 |
| FreeObject | 8 B | 8 B | 内嵌对象中 | 0 |

**Per-thread 总开销**: 约 10-14 KB

---

## 🔧 内存对齐策略

```cpp
// 缓存行大小检测
inline size_t detect_cache_line_size() {
#if defined(__cpp_lib_hardware_interference_size)
    return std::hardware_destructive_interference_size;
#elif defined(_SC_LEVEL1_DCACHE_LINESIZE)
    return sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
#else
    return 128;  // 保守值
#endif
}

// 对齐宏
#define CACHE_ALIGNED alignas(config::CACHE_LINE_SIZE)
#define PAGE_ALIGNED alignas(4096)

// 对齐函数
template<typename T>
inline T align_up(T value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

template<typename T>
inline T align_down(T value, size_t alignment) {
    return value & ~(alignment - 1);
}

template<typename T>
inline bool is_aligned(T value, size_t alignment) {
    return (value & (alignment - 1)) == 0;
}
```

---

## 🎯 下一步

阅读下一个文档：**[03_FRONTEND.md](./03_FRONTEND.md)** - 前端实现和API设计

---

**文档版本**: 1.0  
**最后更新**: 2025年10月31日
