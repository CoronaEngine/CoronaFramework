// Copyright (c) 2025 Corona Engine. All rights reserved.

#include "corona/memory/allocator_stats.h"

namespace corona::memory {

void AllocatorStats::reset() noexcept {
    // TODO: 重置所有统计字段
    total_allocations = 0;
    total_deallocations = 0;
    active_allocations = 0;
    bytes_allocated = 0;
    bytes_deallocated = 0;
    bytes_in_use = 0;
    small_object_allocations = 0;
    large_object_allocations = 0;
    cache_hits = 0;
    cache_misses = 0;
    cross_thread_deallocations = 0;
    total_memory_mapped = 0;
    current_memory_reserved = 0;
}

double AllocatorStats::cache_hit_rate() const noexcept {
    // TODO: 计算缓存命中率
    const uint64_t total_requests = cache_hits + cache_misses;
    if (total_requests == 0) {
        return 0.0;
    }
    return static_cast<double>(cache_hits) / static_cast<double>(total_requests);
}

double AllocatorStats::memory_utilization() const noexcept {
    // TODO: 计算内存利用率
    if (current_memory_reserved == 0) {
        return 0.0;
    }
    return static_cast<double>(bytes_in_use) / static_cast<double>(current_memory_reserved);
}

}  // namespace corona::memory
