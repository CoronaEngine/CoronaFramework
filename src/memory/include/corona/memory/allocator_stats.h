// Copyright (c) 2025 Corona Engine. All rights reserved.

#pragma once

#include <cstdint>

namespace corona::memory {

/// 内存分配器统计信息
struct AllocatorStats {
    /// 总分配次数
    uint64_t total_allocations{0};

    /// 总释放次数
    uint64_t total_deallocations{0};

    /// 当前活跃分配数量
    uint64_t active_allocations{0};

    /// 已分配的总字节数
    uint64_t bytes_allocated{0};

    /// 已释放的总字节数
    uint64_t bytes_deallocated{0};

    /// 当前使用的字节数
    uint64_t bytes_in_use{0};

    /// 小对象分配次数
    uint64_t small_object_allocations{0};

    /// 大对象分配次数
    uint64_t large_object_allocations{0};

    /// 缓存命中次数
    uint64_t cache_hits{0};

    /// 缓存未命中次数
    uint64_t cache_misses{0};

    /// 跨线程释放次数
    uint64_t cross_thread_deallocations{0};

    /// 从操作系统映射的总内存
    uint64_t total_memory_mapped{0};

    /// 当前从操作系统保留的内存
    uint64_t current_memory_reserved{0};

    /// 重置所有统计信息
    void reset() noexcept;

    /// 计算缓存命中率 (0.0 - 1.0)
    [[nodiscard]] double cache_hit_rate() const noexcept;

    /// 计算内存利用率 (0.0 - 1.0)
    [[nodiscard]] double memory_utilization() const noexcept;
};

}  // namespace corona::memory
