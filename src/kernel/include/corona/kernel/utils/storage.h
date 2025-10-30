#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace Corona::Kernel::Utils {

template <typename T, std::size_t Size>
class StaticBuffer {
   public:
    StaticBuffer() = default;
    ~StaticBuffer() = default;
    StaticBuffer(const StaticBuffer&) = delete;
    StaticBuffer& operator=(const StaticBuffer&) = delete;
    StaticBuffer(StaticBuffer&&) = delete;
    StaticBuffer& operator=(StaticBuffer&&) = delete;

   private:
    std::array<T, Size> buffer_;
    std::array<std::atomic<bool>, Size> occupied_{};
};

}  // namespace Corona::Kernel::Utils