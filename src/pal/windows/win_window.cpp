#include <cstdint>
#include <memory>

#include "corona/pal/i_window.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace Corona::PAL {

class WinWindow : public IWindow {
   public:
    WinWindow() : hwnd_(nullptr), is_open_(false) {}

    ~WinWindow() override {
        if (hwnd_) {
            DestroyWindow(hwnd_);
        }
    }

    bool create(const char* title, std::uint32_t width, std::uint32_t height) override {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = window_proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"CoronaWindowClass";

        RegisterClassExW(&wc);

        int title_length = MultiByteToWideChar(CP_UTF8, 0, title, -1, nullptr, 0);
        wchar_t* wide_title = new wchar_t[title_length];
        MultiByteToWideChar(CP_UTF8, 0, title, -1, wide_title, title_length);

        hwnd_ = CreateWindowExW(
            0,
            L"CoronaWindowClass",
            wide_title,
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            static_cast<int>(width), static_cast<int>(height),
            nullptr, nullptr,
            GetModuleHandleW(nullptr),
            this);

        delete[] wide_title;

        if (!hwnd_) {
            return false;
        }

        ShowWindow(hwnd_, SW_SHOW);
        is_open_ = true;
        return true;
    }

    void poll_events() override {
        MSG msg;
        while (PeekMessageW(&msg, hwnd_, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                is_open_ = false;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    bool is_open() const override {
        return is_open_;
    }

    void* get_native_handle() const override {
        return hwnd_;
    }

   private:
    HWND hwnd_;
    bool is_open_;

    static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        WinWindow* window = nullptr;

        if (msg == WM_CREATE) {
            CREATESTRUCTW* create_struct = reinterpret_cast<CREATESTRUCTW*>(lparam);
            window = reinterpret_cast<WinWindow*>(create_struct->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        } else {
            window = reinterpret_cast<WinWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (window) {
            switch (msg) {
                case WM_CLOSE:
                    window->is_open_ = false;
                    return 0;
                case WM_DESTROY:
                    PostQuitMessage(0);
                    return 0;
            }
        }

        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
};

// Factory function implementation
std::unique_ptr<IWindow> create_window() {
    return std::make_unique<WinWindow>();
}

}  // namespace Corona::PAL

#endif  // _WIN32
