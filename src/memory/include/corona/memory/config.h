// Copyright (c) 2025 Corona Engine. All rights reserved.

#pragma once

#include <cstddef>

namespace corona::memory {

/// 内存分配器配置常量
namespace config {

/// Block 大小 (16 KB = 4 个页面)
/// 必须是 2 的幂次方
constexpr size_t kSlabSize = 16 * 1024;

/// 缓存行大小 (保守值，适配多数现代CPU)
constexpr size_t kCacheLineSize = 128;

/// Bin 数量配置
constexpr size_t kSmallBinCount = 8;      ///< 小对象 bins: 8-64 字节
constexpr size_t kSegregatedBinCount = 16; ///< 分段对象 bins: 80-1024 字节
constexpr size_t kFittingBinCount = 5;     ///< 拟合对象 bins: 1792-8128 字节
constexpr size_t kTotalBinCount = kSmallBinCount + kSegregatedBinCount + kFittingBinCount;

/// 对象大小边界
constexpr size_t kMaxSmallObjectSize = 8128;  ///< 小对象最大大小
constexpr size_t kMinAlignment = 8;            ///< 最小对齐字节数

/// Block 池配置
constexpr size_t kBlockPoolHighMark = 32;  ///< Block 池上限
constexpr size_t kBlockPoolLowMark = 8;    ///< 清理后保留数量

/// 大对象本地缓存配置 (Local LOC)
constexpr size_t kLocalLocMaxSize = 4 * 1024 * 1024;  ///< 4 MB per-thread
constexpr size_t kLocalLocMaxCount = 32;              ///< 最多缓存对象数

/// 调试选项
#ifdef NDEBUG
constexpr bool kDebugMode = false;
constexpr bool kCollectStats = false;
#else
constexpr bool kDebugMode = true;
constexpr bool kCollectStats = true;
#endif

/// 内存对齐辅助函数
/// 向上对齐到指定边界
template <typename T>
constexpr T align_up(T value, size_t alignment) noexcept {
    return static_cast<T>((value + alignment - 1) & ~(alignment - 1));
}

/// 向下对齐到指定边界
template <typename T>
constexpr T align_down(T value, size_t alignment) noexcept {
    return static_cast<T>(value & ~(alignment - 1));
}

/// 检查是否已对齐
template <typename T>
constexpr bool is_aligned(T value, size_t alignment) noexcept {
    return (value & (alignment - 1)) == 0;
}

/// 检查是否是 2 的幂次方
constexpr bool is_power_of_two(size_t value) noexcept {
    return value != 0 && (value & (value - 1)) == 0;
}

}  // namespace config
}  // namespace corona::memory
