// Copyright (c) 2025 Corona Engine. All rights reserved.

#pragma once

#include "allocator_stats.h"

#include <cstddef>
#include <limits>
#include <new>

namespace corona::memory {

/// 内存分配器选项
enum class AllocatorOption {
    /// 是否使用 Huge Pages (值: 0=禁用, 1=启用)
    kUseHugePages,

    /// 单线程最大缓存大小 (字节)
    kMaxThreadCacheSize,

    /// 是否启用统计信息收集 (值: 0=禁用, 1=启用)
    kEnableStatistics,

    /// Block 池高水位标记
    kBlockPoolHighMark,

    /// Block 池低水位标记
    kBlockPoolLowMark,

    /// 大对象本地缓存最大大小 (字节)
    kLocalLocMaxSize,

    /// 大对象本地缓存最大数量
    kLocalLocMaxCount,
};

/// 初始化内存分配器
/// @note 分配器支持自动惰性初始化，此函数可选调用
/// @return 初始化是否成功
[[nodiscard]] bool initialize() noexcept;

/// 关闭内存分配器，释放所有资源
/// @note 调用后不应再进行任何分配操作
void shutdown() noexcept;

/// 检查分配器是否已初始化
[[nodiscard]] bool is_initialized() noexcept;

// ============================================================================
// 基本分配接口
// ============================================================================

/// 分配指定大小的内存块
/// @param size 请求的字节数
/// @return 分配的内存指针，失败返回 nullptr
/// @note 返回的内存至少按 config::kMinAlignment 对齐
[[nodiscard]] void* allocate(size_t size) noexcept;

/// 释放之前分配的内存
/// @param ptr 要释放的内存指针，可以是 nullptr
/// @note 传入 nullptr 是安全的，不会执行任何操作
void deallocate(void* ptr) noexcept;

/// 重新分配内存块大小
/// @param ptr 之前分配的内存指针，可以是 nullptr
/// @param new_size 新的大小（字节）
/// @return 新的内存指针，失败返回 nullptr
/// @note 如果 ptr 为 nullptr，等同于 allocate(new_size)
/// @note 如果 new_size 为 0，等同于 deallocate(ptr) 并返回 nullptr
/// @note 原有数据会被保留（复制 min(old_size, new_size) 字节）
[[nodiscard]] void* reallocate(void* ptr, size_t new_size) noexcept;

// ============================================================================
// 对齐分配接口
// ============================================================================

/// 分配按指定对齐要求的内存块
/// @param size 请求的字节数
/// @param alignment 对齐要求（必须是 2 的幂次方）
/// @return 分配的内存指针，失败返回 nullptr
/// @note alignment 必须是 2 的幂次方，否则返回 nullptr
[[nodiscard]] void* allocate_aligned(size_t size, size_t alignment) noexcept;

/// 释放对齐分配的内存
/// @param ptr 要释放的内存指针
/// @note 内部会自动识别是否是对齐分配
void deallocate_aligned(void* ptr) noexcept;

/// 重新分配对齐内存块
/// @param ptr 之前分配的内存指针
/// @param new_size 新的大小（字节）
/// @param alignment 对齐要求
/// @return 新的内存指针，失败返回 nullptr
[[nodiscard]] void* reallocate_aligned(void* ptr, size_t new_size, size_t alignment) noexcept;

// ============================================================================
// 查询接口
// ============================================================================

/// 获取已分配内存块的实际大小
/// @param ptr 内存指针
/// @return 实际分配的字节数，如果 ptr 无效则返回 0
[[nodiscard]] size_t get_allocation_size(void* ptr) noexcept;

/// 检查指针是否是由此分配器分配的
/// @param ptr 待检查的内存指针
/// @return 如果是由此分配器分配则返回 true
[[nodiscard]] bool is_allocated(void* ptr) noexcept;

// ============================================================================
// 配置接口
// ============================================================================

/// 设置分配器选项
/// @param option 选项类型
/// @param value 选项值
/// @return 设置是否成功
bool set_option(AllocatorOption option, size_t value) noexcept;

/// 获取分配器选项值
/// @param option 选项类型
/// @return 选项的当前值
[[nodiscard]] size_t get_option(AllocatorOption option) noexcept;

// ============================================================================
// 统计接口
// ============================================================================

/// 获取当前的分配器统计信息
/// @return 统计信息结构
/// @note 仅在 kEnableStatistics 启用时有效
[[nodiscard]] AllocatorStats get_statistics() noexcept;

/// 重置统计信息
void reset_statistics() noexcept;

/// 打印统计信息到标准输出
/// @note 用于调试和性能分析
void print_statistics() noexcept;

// ============================================================================
// 内存管理和清理
// ============================================================================

/// 触发内存清理，释放未使用的缓存
/// @param aggressive 是否进行激进清理（释放更多缓存）
/// @return 释放的字节数
size_t trim_memory(bool aggressive = false) noexcept;

/// 释放当前线程的所有缓存
/// @note 通常在线程退出前调用
void release_thread_cache() noexcept;

// ============================================================================
// C++ STL 分配器适配器
// ============================================================================

/// STL 兼容的分配器模板
/// @tparam T 元素类型
template <typename T>
class StlAllocator {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    template <typename U>
    struct rebind {
        using other = StlAllocator<U>;
    };

    StlAllocator() noexcept = default;

    template <typename U>
    explicit StlAllocator(const StlAllocator<U>& /*unused*/) noexcept {}

    [[nodiscard]] T* allocate(std::size_t n) {
        if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            throw std::bad_alloc();
        }
        if (auto p = static_cast<T*>(corona::memory::allocate(n * sizeof(T)))) {
            return p;
        }
        throw std::bad_alloc();
    }

    void deallocate(T* p, std::size_t /*n*/) noexcept { corona::memory::deallocate(p); }

    template <typename U>
    bool operator==(const StlAllocator<U>& /*unused*/) const noexcept {
        return true;
    }

    template <typename U>
    bool operator!=(const StlAllocator<U>& /*unused*/) const noexcept {
        return false;
    }
};

}  // namespace corona::memory
