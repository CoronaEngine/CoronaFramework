# Corona Framework - AI Coding Agent Instructions

## Quick Orientation
- Core services live in `corona_kernel` (`src/kernel`); platform adapters are in `corona_pal` (`src/pal`). `KernelContext` (`src/kernel/src/core/kernel_context.cpp`) wires logger, event bus, event stream, VFS, plugin manager, and system manager and must be initialized via `KernelContext::instance().initialize()` before systems run.
- Startup/usage patterns are demonstrated in `examples/`, especially `examples/06_game_loop/main.cpp` for multi-system coordination via event streams.

## Core Patterns
- Events: `IEventBus` (`src/kernel/include/corona/kernel/event/i_event_bus.h`) leverages C++20 concepts; `event_bus.cpp` copies handlers outside locks before invoking them—follow this pattern when extending publish logic.
- Event streams: `EventStream<T>` and `EventSubscription<T>` (`i_event_stream.h`) provide queue-based async messaging with `BackpressurePolicy`. Acquire shared streams through `KernelContext::instance().event_stream()->get_stream<T>()`.
- Systems: Derive from `SystemBase` (`src/kernel/include/corona/kernel/system/system_base.h`) to get a managed thread, FPS throttling, and perf counters. `SystemManager` sorts systems by `get_priority()` inside `initialize_all()` and drives `start/pause/resume/stop` for each.
- Context access: `SystemContext` (in `system_manager.cpp`) hands systems the kernel services; call `context()->logger()/event_bus()/event_stream()` for cross-component communication.

## Platform Abstractions
- `corona_pal` currently ships `StdFileSystem` (`src/pal/src/common/file_system.cpp`) and on Windows `WinDynamicLibrary` (`src/pal/src/platform/windows/win_dynamic_library.cpp`). Linux/macOS dynamic loading is stubbed—gate new behavior with platform checks.
- Plugins: `PluginManager` (`src/kernel/src/core/plugin_manager.cpp`) expects DLL exports `create_plugin`/`destroy_plugin` and wraps plugin lifetimes with custom deleters; ensure new plugins follow that ABI.

## Build & Test Workflow
- Configure with presets (e.g., `cmake --preset ninja-msvc` or `cmake --preset vs2022`), then `cmake --build build --config Debug`. Both libraries are STATIC; add new sources directly to `add_library` blocks in `src/kernel/CMakeLists.txt` or `src/pal/CMakeLists.txt`.
- No external deps today (`cmake/dependencies.cmake` is empty); everything is C++20 standard library.
- Run the full suite with `cd build; ctest -C Debug` or execute a single binary such as `.\build\tests\Debug\kernel_event_stream_test.exe`. Each test in `tests/kernel/` is its own executable linking `corona_kernel` + `corona_pal`.
- Custom macros live in `tests/test_framework.h`; they throw `std::runtime_error` on failure. Multi-thread stress tests (`*_mt_test.cpp`) are the reference when validating concurrency changes.
- Format code via `.\code-format.ps1` before committing.

## Logging & I/O
- `Logger` (`src/kernel/src/core/logger.cpp`) fans out to sinks; console sink is synchronous, file sink is asynchronous with a worker thread—avoid long blocking sections when adding sinks.
- `VirtualFileSystem` (`src/kernel/src/core/vfs.cpp`) normalizes mount paths and delegates to the PAL file system. Always resolve virtual paths (ensures trailing slash handling) before doing raw I/O.

## Concurrency Gotchas
- Copy shared collections under a mutex and release the lock before heavy work (`event_bus.cpp`, `EventStream<T>::publish_impl`).
- Systems run on dedicated threads; ensure destructors call `stop()` and guard shared state with atomics or mutexes.
- `SystemBase` tracks frame timing; use `reset_stats()` when tests rely on clean performance metrics.

## Docs & References
- Chinese design notes live in `doc/项目架构概览.md` and friends; scan them before large architectural changes.
- Quick references: `examples/01_event_bus` (basic pub/sub) through `examples/06_game_loop` (complete loop) show canonical usage of events, streams, and systems.
