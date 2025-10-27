# 平台抽象层 (PAL) 设计与实现

## 1. 简介

平台抽象层 (Platform Abstraction Layer, PAL) 是 Corona 引擎的最底层，其唯一职责是封装所有与操作系统和硬件相关的接口，为上层系统提供一套统一、跨平台的 API。这使得引擎的核心逻辑可以完全与具体平台（Windows, Linux, macOS 等）解耦。

### 1.1 设计原则
*   **最小化接口**: 只抽象那些在不同平台上行为有显著差异且必不可少的功能（如窗口管理、文件 I/O、线程）。
*   **编译时多态**: 优先使用编译时技术（如 `#if defined`、平台特定的源文件）来选择实现，避免运行时的虚函数调用开销。
*   **零开销抽象**: PAL 的封装应尽可能薄，避免引入不必要的性能损耗。

---

## 2. 核心模块定义

PAL 主要包含以下几个核心模块：

### 2.1 窗口系统 (`PAL::Window`)
*   **职责**: 管理应用程序的窗口创建、销毁、尺寸调整和消息循环。
*   **接口定义 (`i_window.h`)**:
    ```cpp
    #pragma once
    #include <cstdint>
    
    namespace Corona::PAL {
        struct WindowEvent { /* ... */ };

        class IWindow {
        public:
            virtual ~IWindow() = default;
            virtual bool create(const char* title, std::uint32_t width, std::uint32_t height) = 0;
            virtual void poll_events() = 0;
            virtual bool is_open() const = 0;
            virtual void* get_native_handle() const = 0; // e.g., HWND on Windows
        };
    }
    ```

### 2.2 输入系统 (`PAL::Input`)
*   **职责**: 从操作系统获取原始输入数据（键盘、鼠标、手柄）。
*   **实现策略**: 通常与窗口系统紧密耦合，在窗口消息循环中捕获输入事件，然后分发给核心层的事件总线。

### 2.3 文件系统 (`PAL::FileSystem`)
*   **职责**: 提供最基础的物理文件系统操作。
*   **接口定义 (`i_file_system.h`)**:
    ```cpp
    #pragma once
    #include <string_view>
    #include <span>
    #include <vector>
    #include <cstdint>

    namespace Corona::PAL {
        class IFileSystem {
        public:
            virtual ~IFileSystem() = default;
            virtual std::vector<std::byte> read_all_bytes(std::string_view path) = 0;
            virtual bool write_all_bytes(std::string_view path, std::span<const std::byte> data) = 0;
            virtual bool exists(std::string_view path) = 0;
        };
    }
    ```

### 2.4 动态库加载 (`PAL::DynamicLibrary`)
*   **职责**: 封装动态链接库（`.dll`, `.so`, `.dylib`）的加载和函数符号的查找。这是插件系统的基础。
*   **接口定义 (`i_dynamic_library.h`)**:
    ```cpp
    #pragma once
    #include <string_view>

    namespace Corona::PAL {
        using FunctionPtr = void(*)();

        class IDynamicLibrary {
        public:
            virtual ~IDynamicLibrary() = default;
            virtual bool load(std::string_view path) = 0;
            virtual void unload() = 0;
            virtual FunctionPtr get_symbol(std::string_view name) = 0;
        };
    }
    ```

### 2.5 线程与同步 (`PAL::Threading`)
*   **职责**: 封装线程、互斥锁、条件变量等并发原语。
*   **实现策略**: C++11/20 的 `<thread>`, `<mutex>` 等标准库已经提供了良好的跨平台封装，PAL 层可以直接 `using` 或进行极简包装，以备未来需要特殊优化（如实现自旋锁）时使用。

---

## 3. 实现策略

### 3.1 文件系统实现: `fast_io`
文件系统 (`IFileSystem`) 的实现将不采用平台特定的代码，而是统一使用 `fast_io` 库。`fast_io` 作为一个高性能的跨平台 I/O 库，其本身已经处理了底层的平台差异，使其成为 PAL 文件系统实现的理想选择。我们将创建一个 `FastIoFileSystem` 类来实现 `IFileSystem` 接口。

### 3.2 目录结构
```
src/
└── pal/
    ├── include/
    │   ├── corona/pal/i_window.h
    │   ├── corona/pal/i_file_system.h
    │   └── ...
    ├── src/
    │   └── fast_io_file_system.cpp  // fast_io 实现
    ├── windows/
    │   ├── win_window.cpp           // 平台相关的窗口实现
    │   └── ...
    ├── linux/
    │   ├── x11_window.cpp
    │   └── ...
    └── CMakeLists.txt
```
*   `include/`: 存放所有平台的公共接口头文件 (`.h`)。
*   `src/`: 存放与平台无关的实现代码（如 `fast_io` 的封装）。
*   `windows/`, `linux/` 等: 存放必须与平台相关的实现文件（如窗口系统）。
*   `CMakeLists.txt`: 根据平台编译对应的源文件。

### 3.3 C++20 特性应用
*   **`std::source_location`**: 在断言和错误处理函数中使用，可以自动捕获文件名、行号和函数名，提供更丰富的调试信息，而无需使用平台特定的宏。
    ```cpp
    void assert(bool condition, const char* msg, const std::source_location& loc = std::source_location::current());
    ```

---

## 4. 示例：工厂创建流程

### 4.1 窗口创建 (平台相关)
1.  **应用层调用**: `auto window = PAL::create_platform_window();`
2.  **工厂函数 (`factory.cpp`)**:
    ```cpp
    std::unique_ptr<IWindow> PAL::create_platform_window() {
        #if defined(_WIN32)
            return std::make_unique<WinWindow>();
        #elif defined(__linux__)
            return std::make_unique<X11Window>();
        #else
            #error "Unsupported platform"
            return nullptr;
        #endif
    }
    ```

### 4.2 文件系统创建 (平台无关)
1.  **应用层调用**: `auto fs = PAL::create_file_system();`
2.  **工厂函数 (`factory.cpp`)**:
    ```cpp
    #include "src/fast_io_file_system.cpp" // 或其头文件

    std::unique_ptr<IFileSystem> PAL::create_file_system()
    {
        // 无论在哪个平台，都统一使用 FastIoFileSystem
        return std::make_unique<FastIoFileSystem>();
    }
    ```

