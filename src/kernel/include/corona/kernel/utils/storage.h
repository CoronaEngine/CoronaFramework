#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <utility>
#include <vector>

#include "lock_free_queue.h"

namespace Corona::Kernel::Utils {

/**
 * @brief 线程安全的固定容量静态缓冲区
 *
 * StaticBuffer 提供了一个固定容量的对象池，支持并发的分配、访问、释放和遍历操作。
 * 内部使用无锁环形队列管理空闲索引，每个槽位独立加锁以实现细粒度并发控制。
 *
 * @tparam T 存储的元素类型
 * @tparam Capacity 缓冲区容量，必须是 2 的幂次且 >= 2
 *
 * @note 线程安全性：
 * - allocate/deallocate：多线程安全
 * - access/access_mut：多线程安全，读写使用共享锁/独占锁
 * - for_each/for_each_const：多线程安全，采用 try-lock 避免阻塞
 */
template <typename T, std::size_t Capacity>
class StaticBuffer {
    static_assert(Capacity >= 2, "Capacity must be at least 2");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

   public:
    /**
     * @brief 构造函数，初始化所有槽位为空闲状态
     */
    StaticBuffer() {
        for (std::size_t i = 0; i < Capacity; ++i) {
            free_indices_.enqueue(i);
            occupied_[i].store(false, std::memory_order_relaxed);
        }
    }

    StaticBuffer(const StaticBuffer&) = delete;
    StaticBuffer& operator=(const StaticBuffer&) = delete;
    StaticBuffer(StaticBuffer&&) = delete;
    StaticBuffer& operator=(StaticBuffer&&) = delete;

    ~StaticBuffer() = default;

    /**
     * @brief 分配一个槽位并初始化
     *
     * @tparam Func 可调用对象类型，签名为 void(T&)
     * @param writer 初始化回调，在持有锁的情况下调用
     * @return 成功返回槽位索引，失败返回 std::nullopt
     *
     * @note 如果 writer 抛出异常，分配会回滚，槽位重新标记为空闲
     * @note 线程安全，无锁获取索引 + 独占锁写入数据
     */
    template <typename Func>
    std::optional<std::size_t> allocate(Func&& writer) {
        std::size_t index;
        if (!free_indices_.dequeue(index)) {
            return std::nullopt;
        }

        std::unique_lock<std::shared_mutex> lock(mutexes_[index]);
        try {
            std::invoke(std::forward<Func>(writer), buffer_[index]);
        } catch (...) {
            occupied_[index].store(false, std::memory_order_relaxed);
            lock.unlock();
            free_indices_.enqueue(index);
            throw;
        }

        occupied_[index].store(true, std::memory_order_release);
        lock.unlock();
        return index;
    }

    /**
     * @brief 释放指定槽位
     *
     * @param index 要释放的槽位索引
     * @throws std::out_of_range 如果索引超出范围
     *
     * @note 线程安全，独占锁标记空闲 + 无锁回收索引
     */
    void deallocate(std::size_t index) {
        if (index >= Capacity) {
            throw std::out_of_range("StaticBuffer::deallocate: index out of range");
        }

        {
            std::unique_lock<std::shared_mutex> lock(mutexes_[index]);
            occupied_[index].store(false, std::memory_order_release);
        }

        free_indices_.enqueue(index);
    }

    /**
     * @brief 只读访问指定槽位
     *
     * @tparam Func 可调用对象类型，签名为 void(const T&)
     * @param index 槽位索引
     * @param reader 读取回调，在持有共享锁的情况下调用
     * @return 成功访问返回 true，槽位未占用或索引无效返回 false
     *
     * @note 线程安全，使用共享锁允许多个读者并发访问
     * @note 双重检查 occupied_ 标志避免访问已释放的槽位
     */
    template <typename Func>
    bool access(std::size_t index, Func&& reader) {
        if (index >= Capacity) {
            return false;
        }

        if (!occupied_[index].load(std::memory_order_acquire)) {
            return false;
        }

        std::shared_lock<std::shared_mutex> lock(mutexes_[index]);
        if (!occupied_[index].load(std::memory_order_acquire)) {
            return false;
        }

        std::invoke(std::forward<Func>(reader), buffer_[index]);
        return true;
    }

    /**
     * @brief 可写访问指定槽位
     *
     * @tparam Func 可调用对象类型，签名为 void(T&)
     * @param index 槽位索引
     * @param writer 写入回调，在持有独占锁的情况下调用
     * @return 成功访问返回 true，槽位未占用或索引无效返回 false
     *
     * @note 线程安全，使用独占锁确保独占写入
     * @note 双重检查 occupied_ 标志避免访问已释放的槽位
     */
    template <typename Func>
    bool access_mut(std::size_t index, Func&& writer) {
        if (index >= Capacity) {
            return false;
        }

        if (!occupied_[index].load(std::memory_order_acquire)) {
            return false;
        }

        std::unique_lock<std::shared_mutex> lock(mutexes_[index]);
        if (!occupied_[index].load(std::memory_order_acquire)) {
            return false;
        }

        std::invoke(std::forward<Func>(writer), buffer_[index]);
        return true;
    }

    /**
     * @brief 只读遍历所有已占用槽位
     *
     * @tparam Func 可调用对象类型，签名为 void(const T&)
     * @param reader 读取回调，对每个已占用槽位调用一次
     *
     * @note 线程安全，使用 try_lock 避免阻塞：
     *       1. 第一轮尝试快速锁定所有槽位，锁定失败的槽位记录到 skipped
     *       2. 第二轮对 skipped 中的槽位阻塞等待锁
     * @note 遍历期间新分配的槽位可能被观察到，已释放的槽位会被跳过
     */
    template <typename Func>
    void for_each_const(Func&& reader) {
        std::vector<std::size_t> skipped;
        skipped.reserve(Capacity);

        for (std::size_t i = 0; i < Capacity; ++i) {
            if (!occupied_[i].load(std::memory_order_acquire)) {
                continue;
            }

            std::shared_lock<std::shared_mutex> lock(mutexes_[i], std::defer_lock);
            if (lock.try_lock()) {
                if (occupied_[i].load(std::memory_order_acquire)) {
                    std::invoke(std::forward<Func>(reader), buffer_[i]);
                }
            } else {
                skipped.push_back(i);
            }
        }

        for (std::size_t index : skipped) {
            if (!occupied_[index].load(std::memory_order_acquire)) {
                continue;
            }
            std::shared_lock<std::shared_mutex> lock(mutexes_[index]);
            if (occupied_[index].load(std::memory_order_acquire)) {
                std::invoke(std::forward<Func>(reader), buffer_[index]);
            }
        }
    }

    /**
     * @brief 可写遍历所有已占用槽位
     *
     * @tparam Func 可调用对象类型，签名为 void(T&)
     * @param writer 写入回调，对每个已占用槽位调用一次
     *
     * @note 线程安全，使用 try_lock 避免阻塞：
     *       1. 第一轮尝试快速锁定所有槽位，锁定失败的槽位记录到 skipped
     *       2. 第二轮对 skipped 中的槽位阻塞等待锁
     * @note 遍历期间新分配的槽位可能被观察到，已释放的槽位会被跳过
     */
    template <typename Func>
    void for_each(Func&& writer) {
        std::vector<std::size_t> skipped;
        skipped.reserve(Capacity);

        for (std::size_t i = 0; i < Capacity; ++i) {
            if (!occupied_[i].load(std::memory_order_acquire)) {
                continue;
            }

            std::unique_lock<std::shared_mutex> lock(mutexes_[i], std::defer_lock);
            if (lock.try_lock()) {
                if (occupied_[i].load(std::memory_order_acquire)) {
                    std::invoke(std::forward<Func>(writer), buffer_[i]);
                }
            } else {
                skipped.push_back(i);
            }
        }

        for (std::size_t index : skipped) {
            if (!occupied_[index].load(std::memory_order_acquire)) {
                continue;
            }
            std::unique_lock<std::shared_mutex> lock(mutexes_[index]);
            if (occupied_[index].load(std::memory_order_acquire)) {
                std::invoke(std::forward<Func>(writer), buffer_[index]);
            }
        }
    }

   private:
    std::array<T, Capacity> buffer_{};                               ///< 数据存储数组
    std::array<std::atomic<bool>, Capacity> occupied_{};             ///< 槽位占用标志，原子操作确保可见性
    std::array<std::shared_mutex, Capacity> mutexes_{};              ///< 每个槽位的独立共享锁
    LockFreeRingBufferQueue<std::size_t, Capacity> free_indices_{};  ///< 空闲索引的无锁队列
};

}  // namespace Corona::Kernel::Utils