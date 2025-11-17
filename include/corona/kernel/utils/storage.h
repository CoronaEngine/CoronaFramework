#pragma once

#include <oneapi/tbb/concurrent_queue.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <stdexcept>

namespace Corona::Kernel::Utils {

/**
 * @brief 线程安全的固定容量静态缓冲区
 *
 * StaticBuffer 提供了一个固定容量的对象池数据结构。
 * 每个槽位独立加锁以实现细粒度并发控制。
 *
 * @tparam T 存储的元素类型
 * @tparam Capacity 缓冲区容量，必须是 2 的幂次且 >= 2
 *
 * @note 此结构体仅作为 Storage 的内部数据容器使用，不提供分配/释放等高级操作
 * @note 包含三个核心成员：buffer（数据数组）、occupied（占用标志）、mutexes（槽位锁）
 */
template <typename T, std::size_t Capacity>
struct StaticBuffer {
    static_assert(Capacity >= 2, "Capacity must be at least 2");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

    StaticBuffer() {
        for (std::size_t i = 0; i < Capacity; ++i) {
            occupied[i].store(false, std::memory_order_relaxed);
        }
    }

    StaticBuffer(const StaticBuffer&) = delete;
    StaticBuffer& operator=(const StaticBuffer&) = delete;
    StaticBuffer(StaticBuffer&&) = delete;
    StaticBuffer& operator=(StaticBuffer&&) = delete;

    ~StaticBuffer() = default;

    std::array<T, Capacity> buffer{};                    ///< 数据存储数组
    std::array<std::atomic<bool>, Capacity> occupied{};  ///< 槽位占用标志，原子操作确保可见性
    std::array<std::shared_mutex, Capacity> mutexes{};   ///< 每个槽位的独立共享锁
};

/**
 * @brief 动态扩容的线程安全对象池
 *
 * Storage 基于 StaticBuffer 实现，通过指针句柄策略提供动态扩容的对象池。
 * 当所有现有 buffer 满时，自动创建新的 StaticBuffer 并将新槽位加入空闲队列。
 *
 * @tparam T 存储的元素类型
 * @tparam BufferCapacity 每个底层 StaticBuffer 的容量，必须是 2 的幂次且 >= 2
 * @tparam InitialBuffers 初始 buffer 数量，必须 >= 1
 *
 * @note 句柄类型：
 * - Handle 是 std::uintptr_t 类型，存储槽位的实际内存地址
 * - 通过 get_parent_buffer() 根据地址范围反查所属的 StaticBuffer
 *
 * @note 线程安全性：
 * - allocate：多线程安全，扩容时使用独占锁保护 buffer 列表
 * - deallocate：多线程安全，使用独占锁标记槽位空闲后回收句柄
 * - read/write：多线程安全，分别使用共享锁/独占锁访问槽位
 * - for_each_read/for_each_write：多线程安全，采用 try_lock 避免死锁，跳过的槽位稍后重试
 */
template <typename T, std::size_t BufferCapacity = 128, std::size_t InitialBuffers = 2>
class Storage {
    static_assert(InitialBuffers >= 1, "InitialBuffers must be at least 1");
    static_assert(BufferCapacity >= 2, "BufferCapacity must be at least 2");
    static_assert((BufferCapacity & (BufferCapacity - 1)) == 0, "BufferCapacity must be a power of two");

   public:
    using Handle = std::uintptr_t;  ///< 句柄类型，存储指向槽位的实际内存地址

   public:
    /**
     * @brief 构造函数，创建初始 buffer 并预分配所有槽位句柄
     *
     * @note 创建 InitialBuffers 个 StaticBuffer，每个包含 BufferCapacity 个槽位
     * @note 将所有槽位的地址作为句柄加入空闲队列
     */
    Storage() {
        for (std::size_t i = 0; i < InitialBuffers; ++i) {
            buffers_.emplace_back();
            for (std::size_t j = 0; j < BufferCapacity; ++j) {
                free_slots_.push(reinterpret_cast<Handle>(&(buffers_.back().buffer[j])));
            }
        }
    }

    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;
    Storage(Storage&&) = delete;
    Storage& operator=(Storage&&) = delete;

    ~Storage() = default;

    /**
     * @brief 获取对象池的总容量
     *
     * @return 当前所有 buffer 的槽位总数（buffer 数量 × 每个 buffer 的容量）
     *
     * @note 返回值为瞬时快照，多线程环境下可能在返回后立即变化
     */
    std::uint64_t capacity() const {
        return buffer_count_.load(std::memory_order_relaxed) * BufferCapacity;
    }

    /**
     * @brief 分配一个槽位并初始化
     *
     * @param initializer 初始化函数，签名为 void(T&)，在持有槽位独占锁的情况下调用
     * @return 成功返回槽位句柄（内存地址），失败返回 0
     *
     * @note 分配流程：
     *       1. 从空闲队列获取槽位句柄
     *       2. 若队列为空，则扩容（创建新 buffer 并入队所有新槽位）
     *       3. 通过 get_parent_buffer() 查找槽位所属的 StaticBuffer
     *       4. 计算槽位在 buffer 内的索引，加锁后初始化并标记为已占用
     *
     * @note 线程安全，扩容时使用独占锁保护 buffer 列表
     * @throws std::runtime_error 若无法找到槽位所属的 buffer（理论上不应发生）
     */
    [[nodiscard]]
    Handle allocate(const std::function<void(T&)>& initializer) {
        Handle id;
        if (!free_slots_.try_pop(id)) {
            // 扩容
            std::unique_lock lock(list_mutex_);
            buffers_.emplace_back();
            for (std::size_t j = 0; j < BufferCapacity; ++j) {
                free_slots_.push(reinterpret_cast<Handle>(&(buffers_.back().buffer[j])));
            }
            buffer_count_.fetch_add(1, std::memory_order_relaxed);
            lock.unlock();

            // 再次尝试分配
            if (!free_slots_.try_pop(id)) {
                return 0;  // 分配失败
            }
        }

        // 初始化槽位
        T* ptr = reinterpret_cast<T*>(id);
        StaticBuffer<T, BufferCapacity>* parent_buffer = get_parent_buffer(id);

        if (parent_buffer) {
            std::size_t index = (id - reinterpret_cast<Handle>(&(parent_buffer->buffer[0]))) / sizeof(T);
            {
                std::unique_lock slot_lock(parent_buffer->mutexes[index]);
                *ptr = T{};
                initializer(*ptr);
                parent_buffer->occupied[index].store(true, std::memory_order_release);
                occupied_count_.fetch_add(1, std::memory_order_relaxed);
            }
        } else {
            throw std::runtime_error("Parent buffer not found during allocation");
        }

        return id;
    }

    /**
     * @brief 释放指定槽位
     *
     * @param id 槽位句柄（由 allocate() 返回）
     *
     * @note 释放流程：
     *       1. 通过 get_parent_buffer() 查找槽位所属的 StaticBuffer
     *       2. 计算槽位在 buffer 内的索引
     *       3. 加独占锁后标记槽位为未占用
     *       4. 将句柄重新加入空闲队列以供复用
     *
     * @note 线程安全，使用独占锁保护槽位状态
     * @throws std::runtime_error 若无法找到槽位所属的 buffer（可能是无效句柄）
     */
    void deallocate(Handle id) {
        T* ptr = reinterpret_cast<T*>(id);
        StaticBuffer<T, BufferCapacity>* parent_buffer = get_parent_buffer(id);

        if (parent_buffer) {
            std::size_t index = (id - reinterpret_cast<std::uint64_t>(&(parent_buffer->buffer[0]))) / sizeof(T);
            {
                std::unique_lock slot_lock(parent_buffer->mutexes[index]);
                parent_buffer->occupied[index].store(false, std::memory_order_release);
                occupied_count_.fetch_sub(1, std::memory_order_relaxed);
            }
            free_slots_.push(id);
        } else {
            throw std::runtime_error("Parent buffer not found during deallocation");
        }
    }

    /**
     * @brief 只读访问指定槽位
     *
     * @param id 槽位句柄（由 allocate() 返回）
     * @param reader 读取函数，签名为 void(const T&)，在持有槽位共享锁的情况下调用
     * @return 槽位已占用且成功读取返回 true，槽位未占用返回 false
     *
     * @note 访问流程：
     *       1. 通过 get_parent_buffer() 查找槽位所属的 StaticBuffer
     *       2. 计算槽位在 buffer 内的索引
     *       3. 加共享锁后检查占用标志，若已占用则调用 reader
     *
     * @note 线程安全，使用共享锁允许多个读者并发访问
     * @throws std::runtime_error 若无法找到槽位所属的 buffer（可能是无效句柄）
     */
    bool read(Handle id, const std::function<void(const T&)>& reader) {
        T* ptr = reinterpret_cast<T*>(id);
        StaticBuffer<T, BufferCapacity>* parent_buffer = get_parent_buffer(id);

        if (parent_buffer) {
            std::size_t index = (id - reinterpret_cast<Handle>(&(parent_buffer->buffer[0]))) / sizeof(T);
            std::shared_lock slot_lock(parent_buffer->mutexes[index]);
            if (parent_buffer->occupied[index].load(std::memory_order_acquire)) {
                reader(*ptr);
                return true;
            } else {
                return false;  // 槽位未被占用
            }
        } else {
            throw std::runtime_error("Parent buffer not found during read");
        }
    }

    /**
     * @brief 可写访问指定槽位
     *
     * @param id 槽位句柄（由 allocate() 返回）
     * @param writer 写入函数，签名为 void(T&)，在持有槽位独占锁的情况下调用
     * @return 槽位已占用且成功写入返回 true，槽位未占用返回 false
     *
     * @note 访问流程：
     *       1. 通过 get_parent_buffer() 查找槽位所属的 StaticBuffer
     *       2. 计算槽位在 buffer 内的索引
     *       3. 加独占锁后检查占用标志，若已占用则调用 writer
     *
     * @note 线程安全，使用独占锁确保独占写入
     * @throws std::runtime_error 若无法找到槽位所属的 buffer（可能是无效句柄）
     */
    bool write(Handle id, const std::function<void(T&)>& writer) {
        T* ptr = reinterpret_cast<T*>(id);
        StaticBuffer<T, BufferCapacity>* parent_buffer = get_parent_buffer(id);

        if (parent_buffer) {
            std::size_t index = (id - reinterpret_cast<Handle>(&(parent_buffer->buffer[0]))) / sizeof(T);
            std::unique_lock slot_lock(parent_buffer->mutexes[index]);
            if (parent_buffer->occupied[index].load(std::memory_order_acquire)) {
                writer(*ptr);
                return true;
            } else {
                return false;  // 槽位未被占用
            }
        } else {
            throw std::runtime_error("Parent buffer not found during write");
        }
    }

    /**
     * @brief 只读遍历所有已占用槽位
     *
     * @param reader 读取函数，签名为 void(const T&)，对每个已占用槽位调用一次
     *
     * @note 遍历流程：
     *       1. 加 buffer 列表共享锁，遍历所有 buffer 的所有槽位
     *       2. 对每个已占用槽位尝试 try_lock_shared，成功则立即调用 reader
     *       3. 锁定失败的槽位记录到跳过队列，稍后重试
     *       4. 释放列表锁后，循环处理跳过队列中的槽位，直到全部处理完毕
     *
     * @note 线程安全，使用 try_lock 避免死锁，确保最终所有槽位都被访问
     * @note 遍历期间新分配的槽位可能被观察到，已释放的槽位会被跳过
     */
    void for_each_read(const std::function<void(const T&)>& reader) {
        std::shared_lock list_lock(list_mutex_);
        std::queue<Handle> skipped_elements;
        std::queue<Handle> skipped_locks;
        for (auto& buffer : buffers_) {
            for (std::size_t i = 0; i < BufferCapacity; ++i) {
                if (buffer.occupied[i].load(std::memory_order_acquire)) {
                    if (buffer.mutexes[i].try_lock_shared()) {
                        reader(buffer.buffer[i]);
                        buffer.mutexes[i].unlock_shared();
                    } else {
                        skipped_elements.push(reinterpret_cast<Handle>(&(buffer.buffer[i])));
                        skipped_locks.push(reinterpret_cast<Handle>(&(buffer.mutexes[i])));
                    }
                }
            }
        }

        list_lock.unlock();
        // 处理跳过的槽位
        while (!skipped_elements.empty()) {
            Handle elem_id = skipped_elements.front();
            Handle lock_id = skipped_locks.front();
            skipped_elements.pop();
            skipped_locks.pop();

            T* ptr = reinterpret_cast<T*>(elem_id);
            std::shared_mutex* mutex_ptr = reinterpret_cast<std::shared_mutex*>(lock_id);

            if (mutex_ptr->try_lock_shared()) {
                reader(*ptr);
                mutex_ptr->unlock_shared();
            } else {
                skipped_elements.push(elem_id);
                skipped_locks.push(lock_id);
            }
        }
    }

    /**
     * @brief 可写遍历所有已占用槽位
     *
     * @param writer 写入函数，签名为 void(T&)，对每个已占用槽位调用一次
     *
     * @note 遍历流程：
     *       1. 加 buffer 列表共享锁，遍历所有 buffer 的所有槽位
     *       2. 对每个已占用槽位尝试 try_lock，成功则立即调用 writer
     *       3. 锁定失败的槽位记录到跳过队列，稍后重试
     *       4. 释放列表锁后，循环处理跳过队列中的槽位，直到全部处理完毕
     *
     * @note 线程安全，使用 try_lock 避免死锁，确保最终所有槽位都被访问
     * @note 遍历期间新分配的槽位可能被观察到，已释放的槽位会被跳过
     */
    void for_each_write(const std::function<void(T&)>& writer) {
        std::shared_lock list_lock(list_mutex_);
        std::queue<Handle> skipped_elements;
        std::queue<Handle> skipped_locks;
        for (auto& buffer : buffers_) {
            for (std::size_t i = 0; i < BufferCapacity; ++i) {
                if (buffer.occupied[i].load(std::memory_order_acquire)) {
                    if (buffer.mutexes[i].try_lock()) {
                        writer(buffer.buffer[i]);
                        buffer.mutexes[i].unlock();
                    } else {
                        skipped_elements.push(reinterpret_cast<Handle>(&(buffer.buffer[i])));
                        skipped_locks.push(reinterpret_cast<Handle>(&(buffer.mutexes[i])));
                    }
                }
            }
        }

        list_lock.unlock();
        // 处理跳过的槽位
        while (!skipped_elements.empty()) {
            Handle elem_id = skipped_elements.front();
            Handle lock_id = skipped_locks.front();
            skipped_elements.pop();
            skipped_locks.pop();

            T* ptr = reinterpret_cast<T*>(elem_id);
            std::shared_mutex* mutex_ptr = reinterpret_cast<std::shared_mutex*>(lock_id);

            if (mutex_ptr->try_lock()) {
                writer(*ptr);
                mutex_ptr->unlock();
            } else {
                skipped_elements.push(elem_id);
                skipped_locks.push(lock_id);
            }
        }
    }

    std::size_t count() const {
        return occupied_count_.load(std::memory_order_relaxed);
    }

    bool empty() const {
        return count() == 0;
    }

   private:
    /**
     * @brief 根据槽位句柄查找其所属的 StaticBuffer
     *
     * @param id 槽位句柄（内存地址）
     * @return 找到则返回 StaticBuffer 指针，否则返回 nullptr
     *
     * @note 查找逻辑：
     *       1. 加 buffer 列表共享锁
     *       2. 遍历所有 buffer，检查 id 是否在该 buffer 的地址范围内
     *       3. 地址范围：[&buffer[0], &buffer[BufferCapacity-1]]
     *
     * @note 线程安全，使用共享锁保护 buffer 列表
     */
    [[nodiscard]]
    StaticBuffer<T, BufferCapacity>* get_parent_buffer(Handle id) {
        std::shared_lock lock(list_mutex_);
        for (auto& buffer : buffers_) {
            if (id >= reinterpret_cast<Handle>(&(buffer.buffer[0])) &&
                id <= reinterpret_cast<Handle>(&(buffer.buffer[BufferCapacity - 1]))) {
                return &buffer;
            }
        }
        return nullptr;
    }

   private:
    std::atomic<std::size_t> occupied_count_{0};             ///< 已占用槽位计数
    std::shared_mutex list_mutex_;                           ///< 保护 buffers_ 列表的锁
    tbb::concurrent_queue<Handle> free_slots_;               ///< 空闲槽位的无锁队列
    std::list<StaticBuffer<T, BufferCapacity>> buffers_;     ///< 底层 buffer 列表
    std::atomic<std::size_t> buffer_count_{InitialBuffers};  ///< 当前 buffer 数量
};

}  // namespace Corona::Kernel::Utils