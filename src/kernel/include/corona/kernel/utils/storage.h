#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <functional>
#include <list>
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
     * @brief 在指定索引处分配并初始化槽位（用于外部句柄管理）
     *
     * @tparam Func 可调用对象类型，签名为 void(T&)
     * @param index 要分配的槽位索引
     * @param writer 初始化回调，在持有锁的情况下调用
     * @return 成功返回 true，槽位已占用或索引无效返回 false
     *
     * @note 此方法用于外部管理槽位索引的场景（如 Storage 的句柄预分配）
     * @note 如果 writer 抛出异常，异常会透传给调用者，槽位保持未占用状态
     * @note 线程安全，独占锁写入数据
     */
    template <typename Func>
    bool allocate_at(std::size_t index, Func&& writer) {
        if (index >= Capacity) {
            return false;
        }

        std::unique_lock<std::shared_mutex> lock(mutexes_[index]);
        if (occupied_[index].load(std::memory_order_acquire)) {
            return false;  // 槽位已被占用
        }

        // 异常透传给调用者，锁由 RAII 自动释放
        std::invoke(std::forward<Func>(writer), buffer_[index]);
        occupied_[index].store(true, std::memory_order_release);
        return true;
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
     * @note 如果 reader 抛出异常，异常会透传给调用者，但锁会被正确释放
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

        // 异常透传给调用者，锁由 RAII 自动释放
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
     * @note 如果 writer 抛出异常，异常会透传给调用者，但锁会被正确释放；数据可能处于部分修改状态
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

        // 异常透传给调用者，锁由 RAII 自动释放
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
     * @note 如果 reader 抛出异常，该槽位的异常会被静默吞掉，继续遍历其他槽位
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
                    try {
                        std::invoke(std::forward<Func>(reader), buffer_[i]);
                    } catch (...) {
                        // 静默吞掉异常，继续处理下一个槽位
                    }
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
                try {
                    std::invoke(std::forward<Func>(reader), buffer_[index]);
                } catch (...) {
                    // 静默吞掉异常，继续处理下一个槽位
                }
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
     * @note 如果 writer 抛出异常，该槽位的异常会被静默吞掉，继续遍历其他槽位；数据可能处于部分修改状态
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
                    try {
                        std::invoke(std::forward<Func>(writer), buffer_[i]);
                    } catch (...) {
                        // 静默吞掉异常，继续处理下一个槽位
                    }
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
                try {
                    std::invoke(std::forward<Func>(writer), buffer_[index]);
                } catch (...) {
                    // 静默吞掉异常，继续处理下一个槽位
                }
            }
        }
    }

    /**
     * @brief 获取缓冲区总容量
     *
     * @return 缓冲区容量（编译期常量）
     *
     * @note 线程安全，无副作用
     */
    [[nodiscard]] consteval std::size_t capacity() const noexcept {
        return Capacity;
    }

    /**
     * @brief 检查缓冲区是否为空（所有槽位均空闲）
     *
     * @return 如果所有槽位都未被占用则返回 true
     *
     * @note 线程安全，但结果为瞬时快照，可能在返回后立即失效
     */
    [[nodiscard]] bool empty() const noexcept {
        return free_indices_.size() == Capacity;
    }

    /**
     * @brief 检查缓冲区是否已满（所有槽位均已占用）
     *
     * @return 如果没有空闲槽位则返回 true
     *
     * @note 线程安全，但结果为瞬时快照，可能在返回后立即失效
     */
    [[nodiscard]] bool full() const noexcept {
        return free_indices_.empty();
    }

   private:
    std::array<T, Capacity> buffer_{};                               ///< 数据存储数组
    std::array<std::atomic<bool>, Capacity> occupied_{};             ///< 槽位占用标志，原子操作确保可见性
    std::array<std::shared_mutex, Capacity> mutexes_{};              ///< 每个槽位的独立共享锁
    LockFreeRingBufferQueue<std::size_t, Capacity> free_indices_{};  ///< 空闲索引的无锁队列
};

/**
 * @brief 动态扩容的线程安全对象池
 *
 * Storage 基于 StaticBuffer 实现，通过句柄预分配策略提供无限容量的抽象。
 * 当所有现有 buffer 满时，自动创建新的 StaticBuffer 并扩充句柄队列。
 *
 * @tparam T 存储的元素类型
 * @tparam BufferCapacity 每个底层 StaticBuffer 的容量，必须是 2 的幂次且 >= 2
 *
 * @note 线程安全性：
 * - allocate：多线程安全，扩容时使用独占锁
 * - deallocate：多线程安全，无锁回收句柄
 * - access/access_mut：多线程安全，通过 StaticBuffer 实现
 */
template <typename T, std::size_t BufferCapacity = (1 << 7)>
class Storage {
    static_assert(BufferCapacity >= 2, "BufferCapacity must be at least 2");
    static_assert((BufferCapacity & (BufferCapacity - 1)) == 0, "BufferCapacity must be a power of two");

   public:
    /**
     * @brief 句柄结构，用于定位跨 buffer 的槽位
     */
    struct Handle {
        typename std::list<StaticBuffer<T, BufferCapacity>>::iterator buffer_it;
        std::size_t index{0};

        bool operator==(const Handle& other) const noexcept {
            return buffer_it == other.buffer_it && index == other.index;
        }
    };

    /**
     * @brief 构造函数，创建初始 buffer 并预分配所有句柄
     */
    Storage() {
        buffers_.emplace_back();
        auto it = buffers_.begin();
        for (std::size_t i = 0; i < BufferCapacity; ++i) {
            free_handles_.enqueue(Handle{it, i});
        }
    }

    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;
    Storage(Storage&&) = delete;
    Storage& operator=(Storage&&) = delete;

    ~Storage() = default;

    /**
     * @brief 分配一个槽位并初始化
     *
     * @tparam Func 可调用对象类型，签名为 void(T&)
     * @param writer 初始化回调，在持有锁的情况下调用
     * @return 成功返回句柄，失败返回 std::nullopt
     *
     * @note 如果所有 buffer 满，会自动创建新 buffer 并扩充句柄队列
     * @note 如果 writer 抛出异常，分配会回滚，句柄重新入队
     * @note 线程安全，扩容时使用独占锁保护 buffer 列表
     */
    template <typename Func>
    std::optional<Handle> allocate(Func&& writer) {
        Handle handle;
        if (!free_handles_.dequeue(handle)) {
            // 没有空闲句柄，需要扩容
            expand_buffers();

            if (!free_handles_.dequeue(handle)) {
                return std::nullopt;  // 扩容失败或竞争失败
            }
        }

        // 使用句柄中指定的索引分配槽位
        if (!handle.buffer_it->allocate_at(handle.index, std::forward<Func>(writer))) {
            // 分配失败（槽位已被占用，理论上不应发生），回收句柄
            free_handles_.enqueue(handle);
            return std::nullopt;
        }

        return handle;
    }

    /**
     * @brief 释放指定句柄对应的槽位
     *
     * @param handle 要释放的句柄
     *
     * @note 线程安全，独占锁标记空闲 + 无锁回收句柄
     * @note 释放后句柄会重新入队，可被后续 allocate 复用
     */
    void deallocate(const Handle& handle) {
        handle.buffer_it->deallocate(handle.index);
        free_handles_.enqueue(handle);
    }

    /**
     * @brief 只读访问指定句柄对应的槽位
     *
     * @tparam Func 可调用对象类型，签名为 void(const T&)
     * @param handle 槽位句柄
     * @param reader 读取回调
     * @return 成功访问返回 true，槽位未占用返回 false
     *
     * @note 线程安全，使用共享锁允许多个读者并发访问
     * @note 如果 reader 抛出异常，异常会透传给调用者
     */
    template <typename Func>
    bool access(const Handle& handle, Func&& reader) const {
        return handle.buffer_it->access(handle.index, std::forward<Func>(reader));
    }

    /**
     * @brief 可写访问指定句柄对应的槽位
     *
     * @tparam Func 可调用对象类型，签名为 void(T&)
     * @param handle 槽位句柄
     * @param writer 写入回调
     * @return 成功访问返回 true，槽位未占用返回 false
     *
     * @note 线程安全，使用独占锁确保独占写入
     * @note 如果 writer 抛出异常，异常会透传给调用者
     */
    template <typename Func>
    bool access_mut(const Handle& handle, Func&& writer) {
        return handle.buffer_it->access_mut(handle.index, std::forward<Func>(writer));
    }

    /**
     * @brief 只读遍历所有已占用槽位
     *
     * @tparam Func 可调用对象类型，签名为 void(const T&)
     * @param reader 读取回调，对每个已占用槽位调用一次
     *
     * @note 线程安全，遍历所有 buffer 的所有槽位
     * @note 如果 reader 抛出异常，该槽位的异常会被静默吞掉，继续遍历其他槽位
     */
    template <typename Func>
    void for_each_const(Func&& reader) const {
        std::shared_lock<std::shared_mutex> lock(list_mutex_);
        for (auto& buffer : buffers_) {
            buffer.for_each_const(std::forward<Func>(reader));
        }
    }

    /**
     * @brief 可写遍历所有已占用槽位
     *
     * @tparam Func 可调用对象类型，签名为 void(T&)
     * @param writer 写入回调，对每个已占用槽位调用一次
     *
     * @note 线程安全，遍历所有 buffer 的所有槽位
     * @note 如果 writer 抛出异常，该槽位的异常会被静默吞掉，继续遍历其他槽位
     */
    template <typename Func>
    void for_each(Func&& writer) {
        std::shared_lock<std::shared_mutex> lock(list_mutex_);
        for (auto& buffer : buffers_) {
            buffer.for_each(std::forward<Func>(writer));
        }
    }

   private:
    /**
     * @brief 扩容：创建新 buffer 并生成对应的句柄
     *
     * @note 使用独占锁保护 buffer 列表
     */
    void expand_buffers() {
        std::unique_lock<std::shared_mutex> lock(list_mutex_);

        buffers_.emplace_back();
        auto new_buffer_it = std::prev(buffers_.end());

        // 为新 buffer 的所有槽位生成句柄并入队
        for (std::size_t i = 0; i < BufferCapacity; ++i) {
            free_handles_.enqueue(Handle{new_buffer_it, i});
        }
    }

    mutable std::shared_mutex list_mutex_;                ///< 保护 buffers_ 列表的锁
    std::list<StaticBuffer<T, BufferCapacity>> buffers_;  ///< 底层 buffer 列表
    LockFreeQueue<Handle> free_handles_;                  ///< 空闲句柄的无锁队列
};

}  // namespace Corona::Kernel::Utils