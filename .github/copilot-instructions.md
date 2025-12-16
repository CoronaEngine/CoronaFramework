# Corona Framework - AI Coding Agent Instructions

## Architecture Overview
Corona is a C++20 game framework with two static libraries:
- **corona_kernel** (`src/kernel/`) - Core services: events, systems, ECS, VFS, plugins
- **corona_pal** (`src/pal/`) - Platform abstraction: file system, dynamic library loading

Entry point: `KernelContext::instance().initialize()` must be called before using any service. See [examples/06_game_loop/main.cpp](examples/06_game_loop/main.cpp) for complete usage.

## Build Commands
```powershell
cmake --preset ninja-msvc          # or vs2022, ninja-clang
cmake --build build --config Debug
ctest -C Debug --test-dir build    # run all tests
.\build\tests\Debug\kernel_event_bus_test.exe  # run single test
.\code-format.ps1                  # format before commit (or -Check to verify)
```
Dependencies: TBB (Intel Threading Building Blocks), Quill (logging) - auto-fetched via CMake.

## Key Patterns

### Events (two mechanisms)
1. **EventBus** - Synchronous pub/sub. Handlers called immediately on `publish()`:
   ```cpp
   auto id = event_bus->subscribe<MyEvent>([](const MyEvent& e) { ... });
   event_bus->publish(MyEvent{...});
   ```
2. **EventStream** - Async queued messaging with backpressure (`Block`/`DropOldest`/`DropNewest`):
   ```cpp
   auto stream = event_stream->get_stream<MyEvent>();
   auto sub = stream->subscribe({.max_queue_size = 256, .policy = BackpressurePolicy::Block});
   if (auto evt = sub.try_pop()) { ... }  // non-blocking
   ```

### Systems
Derive from `SystemBase` for managed threading, FPS throttling, pause/resume:
```cpp
class PhysicsSystem : public SystemBase {
    std::string_view get_name() const override { return "Physics"; }
    int get_priority() const override { return 90; }  // higher = init first
    bool initialize(ISystemContext* ctx) override { ... }
    void update() override { world_->step(delta_time()); }
};
```
Register via `system_manager->register_system(std::make_unique<PhysicsSystem>())`.

### Plugins (Windows only)
DLLs must export `extern "C"` symbols `create_plugin()` / `destroy_plugin()`. Framework wraps with custom deleter. See [doc/plugin_guide.md](doc/plugin_guide.md).

## Concurrency Rules
- **Copy-then-release**: Copy handler lists under mutex, invoke outside lock (see `event_bus.cpp:publish_impl`)
- Systems run on dedicated threads - guard shared state with atomics/mutexes
- Multi-thread tests (`*_mt_test.cpp`) are the validation reference for concurrency changes

## Adding Code
- New kernel sources: add to `CORONA_KERNEL_SOURCES` in [src/kernel/CMakeLists.txt](src/kernel/CMakeLists.txt)
- New tests: use `corona_add_test(name path.cpp)` in [tests/CMakeLists.txt](tests/CMakeLists.txt)
- Test macros in [tests/test_framework.h](tests/test_framework.h): `TEST(Suite, Name)`, `ASSERT_TRUE/EQ/NE` throw on failure

## ECS (in development)
Archetype-based storage: `Archetype` manages `Chunk`s of entities with identical component signatures. See [include/corona/kernel/ecs/](include/corona/kernel/ecs/).

## File Structure Reference
```
include/corona/kernel/  - Public headers (interfaces, concepts)
src/kernel/             - Implementations
  core/                 - KernelContext, Logger, VFS, PluginManager
  event/                - EventBus, EventStream
  system/               - SystemBase, SystemManager
  ecs/                  - Archetype, Chunk, Component
examples/01-07/         - Progressive usage examples
tests/kernel/           - Unit tests (each is standalone executable)
```
