#pragma once
#include <type_traits>

namespace Corona::Kernel {

// Event concept: must be copyable and movable
template <typename T>
concept Event = std::is_copy_constructible_v<T> && std::is_move_constructible_v<T>;

}  // namespace Corona::Kernel
