#pragma once
#include <cstdint>

namespace Corona::PAL {
struct WindowEvent { /* ... */
};

class IWindow {
   public:
    virtual ~IWindow() = default;
    virtual bool create(const char* title, std::uint32_t width, std::uint32_t height) = 0;
    virtual void poll_events() = 0;
    virtual bool is_open() const = 0;
    virtual void* get_native_handle() const = 0;  // e.g., HWND on Windows
};
}  // namespace Corona::PAL
