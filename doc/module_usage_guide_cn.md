# Corona Framework 模块指南

### 1. Kernel 模块（`src/kernel`）
- **KernelContext**（`src/kernel/core/kernel_context.cpp`）：单例入口，负责组装日志、事件总线、事件流、插件管理器、虚拟文件系统和系统管理器。必须在访问任何服务之前调用 `KernelContext::instance().initialize()`，结束时调用 `shutdown()` 做资源回收。
- **Logger**（`src/kernel/core/logger.cpp`）：提供同步控制台输出以及异步文件写入。可在初始化阶段注册自定义 sink，将日志导向游戏内 UI、网络或其他目标。
- **事件总线**（`include/corona/kernel/event/i_event_bus.h` 与 `src/kernel/event/event_bus.cpp`）：基于模板的同步发布/订阅。实现会在持锁状态下复制订阅者，再解锁后调用处理函数，避免长时间持有互斥量。典型用法：
  ```cpp
  auto bus = KernelContext::instance().event_bus();
  auto token = bus->subscribe<MyEvent>([](const MyEvent& evt) { /* 处理逻辑 */ });
  bus->publish(MyEvent{...});
  ```
  保持 `SubscribeToken` 有效以维持订阅；如需及时解绑，可主动销毁。
- **事件流**（`include/corona/kernel/event/i_event_stream.h`）：基于队列的异步消息管道，支持多种背压策略（丢弃旧消息、阻塞发布者等）。通过 `KernelContext::instance().event_stream()->get_stream<T>()` 获取流；发布端调用 `publish`，订阅端使用 `EventSubscription<T>` 轮询或注册回调。
- **SystemBase 与 SystemManager**（`include/corona/kernel/system/system_base.h`、`src/kernel/system/system_manager.cpp`）：用于构建多线程系统。继承 `SystemBase` 并重写 `on_initialize`、`on_update`、`on_shutdown`。使用 `SystemManager::register_system<YourSystem>()` 注册后，通过 `initialize_all()`、`start_all()` 控制生命周期；优先级决定初始化顺序。
- **虚拟文件系统**（`src/kernel/core/vfs.cpp`）：统一路径格式并委托至 PAL 文件系统。先挂载物理路径，再通过虚拟路径访问，如 `virtual_file_system()->open("root:/config.json")`。
- **PluginManager**（`src/kernel/core/plugin_manager.cpp`）：加载平台动态库，要求导出 `create_plugin`/`destroy_plugin`。使用 `register_plugin` 加载并在 `unload_all` 清理。建议在 PAL 层封装平台细节，保证内核模块保持跨平台。

### 2. 平台抽象层（`src/pal`）
- **StdFileSystem**（`src/pal/common/file_system.cpp`）：默认文件系统实现，封装操作系统 API 并兼顾 VFS 的路径规范和错误处理。
- **动态库接口**：接口定义在 `include/corona/pal/dynamic_library/`。Windows 版本位于 `src/pal/platform/windows/win_dynamic_library.cpp`；Linux/macOS 当前仍为占位实现，会抛出 `std::runtime_error`。跨平台部署时需在调用前加平台判断。
- 扩展 PAL 时，在对应平台目录添加实现并在 `src/pal/CMakeLists.txt` 中注册。保持平台分支集中管理，避免核心模块直接引用系统头文件。

### 3. 内存工具（`src/memory`）
- 提供针对引擎优化的分配器与容器，如 Cache 对齐分配器、无锁队列等。公共接口位于 `src/memory/include`。
- 可参考 `tests/kernel/cache_aligned_allocator_test.cpp`、`lockfree_queue_test.cpp` 等测试了解用法与边界情况。
- 将这些分配器集成到系统中时，可作为 STL 兼容分配器使用，具备标准 `allocate`/`deallocate` 接口和 Traits 定义。

### 4. 示例程序（`examples/`）
- `01_event_bus` 至 `07_log_helper` 逐步展示框架能力，从基础事件到完整游戏循环。
- `06_game_loop/main.cpp` 演示多个系统协同、事件流与系统管理器驱动主循环的完整流程。
- 构建示例命令：`cmake --build build --target <sample> --config <cfg>`，可执行文件位于 `build/examples/<cfg>/`。

### 5. 测试体系（`tests/`）
- 每个特性均有独立测试，包括多线程压力测试。阅读这些代码可快速了解预期行为及并发保障方案。
- 构建后运行 `ctest -C Debug`，或直接执行单个测试二进制（如 `build/tests/Debug/kernel_event_stream_test.exe`）进行定向调试。

### 6. 并发与错误处理约定
- 在持锁期间复制共享集合，随后释放锁再调用回调，是事件总线与事件流的通用模式。
- 系统需确保安全停机：在 `on_shutdown` 中停止后台任务并释放资源，避免崩溃或挂起。
- 共享标志位尽量使用 `std::atomic`；复杂状态结合 `std::mutex` 或 `std::shared_mutex`，可参考现有模块的实现。

### 7. 扩展框架的建议流程
1. 扩展内核接口或新增服务，记得在 `KernelContext` 初始化流程中注册，以供系统访问。
2. 若涉及平台差异，在 PAL 层新增实现并通过内核抽象暴露，例如自定义文件系统或网络模块。
3. 编写与现有风格一致的测试，特别是多线程压力测试，以验证性能与稳定性。
4. 将新增特性记录于 `doc/` 中，并在示例或 README 中补充使用说明。
