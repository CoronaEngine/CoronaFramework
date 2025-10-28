# Corona Framework - AI Coding Agent Instructions

## Architecture Overview

Corona Framework is a **modular game engine framework** with a layered architecture:

- **Kernel Layer** (`src/kernel/`): Core engine systems using event-driven architecture
  - Event Bus: Type-safe pub-sub using C++20 concepts (`i_event_bus.h`)
  - Event Stream: Queue-based reactive streams with backpressure (`i_event_stream.h`)
  - System Manager: Multi-threaded system lifecycle with FPS control (`i_system_manager.h`)
  - Plugin Manager, VFS, Logger: Extensibility and I/O abstractions
- **PAL Layer** (`src/pal/`): Platform Abstraction Layer for cross-platform support
  - File system, dynamic library loading (Windows implemented, Linux/macOS via stubs)
  - Uses **fast_io** library (FetchContent dependency) for high-performance I/O

**Key Design Principle**: Interface-based architecture with factory functions (`create_*()`) returning `std::unique_ptr`. Implementations are private classes in `.cpp` files.

## Critical Patterns

### 1. C++20 Concepts for Type Safety
Events and handlers use concepts instead of inheritance:
```cpp
template <typename T>
concept Event = std::is_copy_constructible_v<T> && std::is_move_constructible_v<T>;

template <typename Handler, typename T>
concept EventHandler = Event<T> && std::invocable<Handler, const T&>;
```
**When adding events**: Define a plain struct (copyable/movable). Subscribe with lambdas or callables.

### 2. Thread-Safe Event Publishing
Event handlers are copied outside locks before invocation to avoid deadlocks:
```cpp
// From event_bus.cpp - ALWAYS follow this pattern
std::vector<TypeErasedHandler> handlers_copy;
{
    std::lock_guard<std::mutex> lock(mutex_);
    // Copy handlers under lock
}
// Call handlers WITHOUT holding lock
```

### 3. INTERFACE Libraries (CMake)
Both `corona_kernel` and `corona_pal` use `INTERFACE` libraries (header-only style):
```cmake
add_library(corona_kernel INTERFACE)
target_sources(corona_kernel INTERFACE src/event_bus.cpp)  # Sources marked INTERFACE
```
**When adding files**: Add to `target_sources(..., INTERFACE ...)`, not as regular sources.

### 4. System Lifecycle
Systems run on independent threads with FPS throttling:
- `initialize()`: One-time setup (main thread)
- `update()`: Runs in loop at target FPS (dedicated thread)
- `sync()`: Optional frame sync point (main thread)
- States: `idle` → `running` ↔ `paused` → `stopping` → `stopped`

### 5. Custom Test Framework
Uses lightweight macros (no Google Test/Catch2):
```cpp
TEST(EventBus, BasicPublishSubscribe) {
    ASSERT_TRUE(condition);
    ASSERT_FALSE(condition);
    ASSERT_EQ(expected, actual);
}
```
Tests auto-register via static initializers. Run with `ctest` or directly via executables.

## Build System

### CMake Configuration
- **Presets**: Use `ninja-msvc`, `ninja-clang`, `vs2022` (Windows); `ninja-linux-gcc`, `ninja-macos` (other platforms)
- **Standard**: C++20 required (`CMAKE_CXX_STANDARD=20`)
- **Multi-config**: Debug/Release/RelWithDebInfo via Ninja Multi-Config

### Common Commands
```powershell
# Configure (pick preset based on platform/compiler)
cmake --preset ninja-msvc

# Build (default: all configs)
cmake --build build --config Debug

# Build specific target
cmake --build build --target corona_kernel --config Release

# Run tests
cd build; ctest -C Debug

# Run specific test
.\build\tests\Debug\kernel_event_stream_test.exe
```

### Code Formatting
```powershell
.\code-format.ps1  # Uses clang-format (configuration in project root)
```

## File Organization

```
src/
├── kernel/
│   ├── include/corona/kernel/  # Public interfaces (i_*.h)
│   └── src/                    # Private implementations
├── pal/
│   ├── include/corona/pal/     # Platform abstractions
│   ├── src/                    # Common implementations
│   └── windows/                # Windows-specific code
tests/
├── test_framework.h            # Custom test macros
└── kernel/                     # Kernel layer tests
```

**Namespace Convention**: `Corona::Kernel`, `Corona::PAL` (note capitalization).

## Dependencies

- **fast_io**: High-performance I/O library (fetched via CMake FetchContent)
  - Used in PAL file system implementation
  - Headers available after CMake configuration
- **Standard Library**: C++20 features (`<concepts>`, `<ranges>`, `std::jthread`, etc.)

## Integration Points

### Adding a New System
1. Inherit from `ISystem` (implement all pure virtuals)
2. Use `ISystemContext*` in `initialize()` to access EventBus, Logger, VFS, etc.
3. Implement `update()` for per-frame logic (runs on dedicated thread)
4. Register via `ISystemManager::register_system(std::make_shared<YourSystem>())`

### Cross-Component Communication
- **Preferred**: Use EventBus for loose coupling
- **Direct**: Systems can call each other via `ISystemManager::get_system(name)`
- **Shared State**: Avoid. Use EventStream for reactive data flows.

## Testing Strategy

- **Unit Tests**: Per-component tests in `tests/kernel/`
- **Thread Safety**: `thread_safety_test.cpp` uses multiple threads to stress-test concurrency
- **Performance**: `performance_test.cpp` validates FPS control and frame timing
- **Coverage**: 45 tests covering EventBus, EventStream, SystemManager, threading

**When adding features**: Write tests using `TEST()` macro, link against `corona_kernel` and `corona_pal`.

## Common Pitfalls

1. **Don't use regular library types** for `corona_kernel`/`corona_pal` - they're INTERFACE libraries
2. **Lock discipline**: Always copy data under lock, process outside lock (avoid deadlocks)
3. **System thread safety**: Systems run on separate threads - protect shared state with mutexes
4. **Event lifetime**: Events are copied when published - avoid non-copyable types
5. **CMake presets**: Match preset to your compiler (MSVC vs Clang have different presets)

## Documentation

Detailed design docs in `doc/` (Chinese):
- `architecture_comparison.md`: Event system design comparison
- `event_stream_design.md`: Reactive streams architecture
- `system_design.md`: System manager and threading model
- `CODE_REVIEW.md`: Current implementation status

**Before proposing changes**: Check if design rationale is documented in `doc/`.
