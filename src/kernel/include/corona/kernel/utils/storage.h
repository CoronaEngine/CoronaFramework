#pragma once
#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <stdexcept>

namespace Corona::Kernel::Utils {

template <typename T, std::size_t N>
class StorageBuffer {
   public:
    StorageBuffer() {
        for (auto& v : valid_) {
            v.store(false);
        }
    }

    StorageBuffer(const StorageBuffer&) = delete;
    StorageBuffer& operator=(const StorageBuffer&) = delete;
    StorageBuffer(StorageBuffer&&) = delete;
    StorageBuffer& operator=(StorageBuffer&&) = delete;

   public:
    void write(std::size_t index, const std::function<void(T&)>& writer) {
        if (index >= N) {
            throw std::out_of_range("Index out of range in StorageBuffer");
        }

        std::unique_lock lock(mutexes_[index]);
        writer(buffer_[index]);
        valid_[index].store(true, std::memory_order_relaxed);
        valid_count_.fetch_add(1, std::memory_order_relaxed);
    }

    void free(std::size_t index) {
        if (index < N) {
            std::unique_lock lock(mutexes_[index]);
            valid_[index].store(false, std::memory_order_relaxed);
            valid_count_.fetch_sub(1, std::memory_order_relaxed);
            buffer_[index] = T{};
        } else {
            throw std::out_of_range("Index out of range in StorageBuffer");
        }
    }

    void read(std::size_t index, std::function<void(const T&)> reader) const {
        if (index >= N) {
            throw std::out_of_range("Index out of range in StorageBuffer");
        }

        if (!valid_[index].load(std::memory_order_relaxed)) {
            throw std::runtime_error("Attempt to read invalid storage in StorageBuffer");
        }

        std::shared_lock lock(mutexes_[index]);
        reader(buffer_[index]);
    }

    void for_each_read(const std::function<void(const T&)>& reader) const {
        std::queue<std::size_t> skipped;
        for (std::size_t i = 0; i < N; ++i) {
            if (valid_[i].load(std::memory_order_relaxed)) {
                if (mutexes_[i].try_lock_shared()) {
                    reader(buffer_[i]);
                    mutexes_[i].unlock_shared();
                } else {
                    skipped.push(i);
                }
            }
        }

        while (!skipped.empty()) {
            const std::size_t i = skipped.front();
            skipped.pop();
            if (mutexes_[i].try_lock_shared()) {
                reader(buffer_[i]);
                mutexes_[i].unlock_shared();
            } else {
                skipped.push(i);
            }
        }
    }

    void for_each_write(const std::function<void(T&)>& writer) {
        std::queue<std::size_t> skipped;
        for (std::size_t i = 0; i < N; ++i) {
            if (valid_[i].load(std::memory_order_relaxed)) {
                if (mutexes_[i].try_lock()) {
                    writer(buffer_[i]);
                    mutexes_[i].unlock();
                } else {
                    skipped.push(i);
                }
            }
        }

        while (!skipped.empty()) {
            const std::size_t i = skipped.front();
            skipped.pop();
            if (mutexes_[i].try_lock()) {
                writer(buffer_[i]);
                mutexes_[i].unlock();
            } else {
                skipped.push(i);
            }
        }
    }

    [[nodiscard]] std::size_t size() const {
        return N;
    }

    [[nodiscard]] std::size_t used_count() const {
        return valid_count_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t free_count() const {
        return N - valid_count_.load(std::memory_order_relaxed);
    }

   private:
    std::array<T, N> buffer_{};
    std::array<std::atomic<bool>, N> valid_{};
    std::atomic<std::size_t> valid_count_{0};
    mutable std::array<std::shared_mutex, N> mutexes_{};
};

}  // namespace Corona::Kernel::Utils