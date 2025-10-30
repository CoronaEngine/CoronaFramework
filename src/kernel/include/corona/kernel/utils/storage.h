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

template <typename T, std::size_t Capacity>
class StaticBuffer {
    static_assert(Capacity >= 2, "Capacity must be at least 2");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

   public:
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

    template <typename Func>
    void for_each_const(Func&& reader) {
        auto&& reader_ref = reader;
        std::vector<std::size_t> skipped;
        skipped.reserve(Capacity);

        for (std::size_t i = 0; i < Capacity; ++i) {
            if (!occupied_[i].load(std::memory_order_acquire)) {
                continue;
            }

            std::shared_lock<std::shared_mutex> lock(mutexes_[i], std::defer_lock);
            if (lock.try_lock()) {
                if (occupied_[i].load(std::memory_order_acquire)) {
                    std::invoke(reader_ref, buffer_[i]);
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
                std::invoke(reader_ref, buffer_[index]);
            }
        }
    }

    template <typename Func>
    void for_each(Func&& writer) {
        auto&& writer_ref = writer;
        std::vector<std::size_t> skipped;
        skipped.reserve(Capacity);

        for (std::size_t i = 0; i < Capacity; ++i) {
            if (!occupied_[i].load(std::memory_order_acquire)) {
                continue;
            }

            std::unique_lock<std::shared_mutex> lock(mutexes_[i], std::defer_lock);
            if (lock.try_lock()) {
                if (occupied_[i].load(std::memory_order_acquire)) {
                    std::invoke(writer_ref, buffer_[i]);
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
                std::invoke(writer_ref, buffer_[index]);
            }
        }
    }

   private:
    std::array<T, Capacity> buffer_;
    std::array<std::atomic<bool>, Capacity> occupied_{};
    std::array<std::shared_mutex, Capacity> mutexes_;
    LockFreeRingBufferQueue<std::size_t, Capacity> free_indices_;
};

}  // namespace Corona::Kernel::Utils