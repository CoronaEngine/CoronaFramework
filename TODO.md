# Corona 框架开发计划

本文档概述了构建 Corona 框架的开发任务，重点是平台抽象层 (PAL) 和核心系统 (Kernel)。

## 阶段 1: 核心基础设施搭建

### 1. 项目结构搭建
- **状态:** 未开始
- **描述:** 在 `src` 目录中为 `pal` 和 `kernel` 创建必要的目录结构。每个模块都将有自己的 `CMakeLists.txt` 来管理其源文件和依赖项。
- **详细信息:**
    - `src/pal/`
    - `src/pal/include/corona/pal/`
    - `src/pal/windows/`
    - `src/kernel/`
    - `src/kernel/include/corona/kernel/`
    - `src/kernel/src/`

### 2. PAL - 接口定义
- **状态:** 未开始
- **描述:** 根据 `doc/pal_design.md` 为所有 PAL 接口创建头文件。
- **要创建的文件:**
    - `src/pal/include/corona/pal/i_window.h`
    - `src/pal/include/corona/pal/i_file_system.h`
    - `src/pal/include/corona/pal/i_dynamic_library.h`

### 3. PAL - 实现
- **状态:** 未开始
- **描述:** 实现 PAL 接口。文件系统将使用 `fast_io` 进行跨平台实现，而窗口系统等仍需平台特定代码。
- **要创建的文件:**
    - `src/pal/src/fast_io_file_system.cpp` (跨平台)
    - `src/pal/windows/win_window.cpp`
    - `src/pal/windows/win_dynamic_library.cpp`
    - `src/pal/factory.cpp` (用于创建特定于平台的实例)

### 4. Kernel - 接口定义
- **状态:** 未开始
- **描述:** 根据 `doc/kernel_design.md` 为所有 Kernel 服务接口创建头文件。
- **要创建的文件:**
    - `src/kernel/include/corona/kernel/i_logger.h`
    - `src/kernel/include/corona/kernel/i_vfs.h`
    - `src/kernel/include/corona/kernel/i_event_bus.h`
    - `src/kernel/include/corona/kernel/i_plugin_manager.h`
    - `src/kernel/include/corona/kernel/kernel_context.h`

### 5. Kernel - 日志系统实现
- **状态:** 未开始
- **描述:** 实现 `Logger` 服务。这将是第一个实现的服务，用于后续开发的调试。它将使用 `fast_io` 库。
- **要创建的文件:**
    - `src/kernel/src/logger.cpp`

### 6. Kernel - 其他服务实现
- **状态:** 未开始
- **描述:** 实现其余的核心服务。
- **要创建的文件:**
    - `src/kernel/src/event_bus.cpp`
    - `src/kernel/src/vfs.cpp`
    - `src/kernel/src/plugin_manager.cpp`

### 7. Kernel - 上下文实现
- **状态:** 未开始
- **描述:** 实现 `KernelContext` 来管理所有核心服务的生命周期。
- **要创建的文件:**
    - `src/kernel/src/kernel_context.cpp`

### 8. 主程序集成
- **状态:** 未开始
- **描述:** 修改 `main.cpp` 以初始化 `KernelContext`，并使用新的日志系统打印 "Hello, World!"，而不是 `std::cout`。

### 9. 构建与验证
- **状态:** 未开始
- **描述:** 编译整个项目，运行并验证窗口创建和日志记录功能是否按预期工作。
