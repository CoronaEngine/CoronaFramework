# 核心系统 (Kernel) 设计与实现

## 1. 简介

核心系统 (Kernel) 是引擎的基础服务层，它构建于平台抽象层 (PAL) 之上，为所有上层系统和插件提供稳定、通用的功能。Kernel 的设计目标是高内聚和高性能，其模块不应涉及任何与游戏逻辑相关的概念。

### 1.1 设计原则
*   **服务定位器模式**: Kernel 提供一个全局的上下文或服务注册表，允许其他系统查询和获取核心服务的实例。
*   **生命周期管理**: Kernel 负责在引擎启动时初始化所有核心服务，并在关闭时按正确的顺序销毁它们。
*   **数据驱动**: 服务的配置（如日志级别）应通过外部配置文件加载，而不是硬编码。

---

## 2. 核心模块定义

### 2.1 日志系统 (`Logger`)
*   **职责**: 提供一个高性能、线程安全、可配置的日志记录系统。
*   **接口定义 (`i_logger.h`)**:
    ```cpp
    #pragma once
    #include <string_view>
    #include <source_location>

    namespace Corona::Kernel {
        enum class LogLevel { Trace, Debug, Info, Warn, Error, Fatal };

        class ILogger {
        public:
            virtual ~ILogger() = default;
            virtual void log(LogLevel level, std::string_view message, const std::source_location& loc = std::source_location::current()) = 0;
        };
    }
    ```
*   **实现策略**:
    *   使用 `fast_io` 库进行高性能 I/O 操作。
    *   内部使用一个无锁队列来缓冲日志消息，由一个专门的后台线程负责将消息写入文件或控制台，避免阻塞调用线程。
    *   提供宏（如 `CORONA_LOG_INFO(...)`）来简化调用，并利用 `std::source_location` 自动捕获上下文。

### 2.2 虚拟文件系统 (`VFS`)
*   **职责**: 在 PAL 的物理文件系统之上构建一个虚拟层，支持挂载点、文件包（如 `.zip`）的透明访问。
*   **接口定义 (`i_vfs.h`)**:
    ```cpp
    #pragma once
    #include <string_view>
    #include <span>
    #include <vector>
    #include <cstdint>

    namespace Corona::Kernel {
        class IVFS {
        public:
            virtual ~IVFS() = default;
            virtual void mount(std::string_view virtual_path, std::string_view physical_path) = 0;
            virtual std::vector<std::byte> read_all_bytes(std::string_view path) = 0;
            // ... 其他接口
        };
    }
    ```
*   **实现策略**:
    *   内部维护一个从虚拟路径到物理路径（或包内路径）的映射表。
    *   当请求一个路径时，VFS 会遍历挂载点，找到最匹配的前缀，然后将剩余的相对路径附加到物理路径上。

### 2.3 事件系统 (`EventBus`)
*   **职责**: 实现一个全局的、类型安全的事件发布-订阅系统，用于模块间的解耦。
*   **接口定义 (`i_event_bus.h`)**:
    ```cpp
    #pragma once
    #include <functional>
    #include <any>

    namespace Corona::Kernel {
        // 使用 C++20 Concepts 约束事件类型
        export template<typename T>
        concept IsEvent = std::is_class_v<T>;

        class IEventBus {
        public:
            virtual ~IEventBus() = default;

            template<IsEvent T>
            virtual void subscribe(std::function<void(const T&)> callback) = 0;

            template<IsEvent T>
            virtual void publish(const T& event) = 0;
        };
    }
    ```
*   **实现策略**:
    *   内部使用一个 `std::unordered_map<std::type_index, std::vector<std::function<void(const std::any&)>>>` 来存储事件类型和对应的监听器列表。
    *   `subscribe` 时，将回调函数包装成一个接受 `std::any` 的 lambda，并存入 map。
    *   `publish` 时，根据事件类型 `T` 查找到所有监听器，并调用它们。

### 2.4 插件管理器 (`PluginManager`)
*   **职责**: 发现、加载、初始化和卸载插件。
*   **接口定义 (`i_plugin_manager.h`)**:
    ```cpp
    #pragma once
    #include "corona/pal/i_plugin.h" // 引用插件接口
    #include <memory>

    namespace Corona::Kernel {
        class IPluginManager {
        public:
            virtual ~IPluginManager() = default;
            virtual void load_plugins_from_config(std::string_view config_file) = 0;
            virtual void unload_all() = 0;
            virtual Corona::PAL::IPlugin* get_plugin(std::string_view name) = 0;
        };
    }
    ```
*   **实现策略**:
    *   读取配置文件（如 `plugins.json`）获取插件列表。
    *   对每个插件，使用 `PAL::DynamicLibrary` 加载其动态库。
    *   从库中查找一个约定的导出函数（如 `create_plugin_instance`），调用它来获取 `IPlugin` 的实例。
    *   调用插件的 `install()` 方法，并将其存储在一个列表中。
    *   `unload_all` 时，反向遍历列表，调用 `uninstall()` 并卸载库。

---

## 3. C++20 特性应用

*   **Concepts**: 在事件总线中使用，确保只有合法的类型可以作为事件被发布和订阅，提供了编译期的类型安全。
*   **`std::jthread`**: 在日志系统中使用 `std::jthread` 作为后台工作线程。`jthread` 在析构时会自动请求停止并 `join()`，简化了线程的生命周期管理，使其代码更安全、更简洁。

---

## 4. 服务上下文 (`KernelContext`)

所有核心服务都由一个 `KernelContext` 类来持有和管理。
```cpp
// kernel_context.h
#pragma once
#include "corona/kernel/i_logger.h"
// ... 其他服务接口

namespace Corona::Kernel {
    class KernelContext {
    public:
        // 初始化所有服务
        void initialize();
        // 销毁所有服务
        void shutdown();

        static ILogger* get_logger();
        // ... 其他服务的静态 getter
    private:
        static std::unique_ptr<ILogger> s_logger_;
        // ...
    };
}
```
引擎的 `main` 函数将负责创建、初始化和销毁 `KernelContext` 实例，从而管理整个引擎的生命周期。
