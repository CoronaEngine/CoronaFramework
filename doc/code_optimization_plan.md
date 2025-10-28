# Corona Framework 代码整理与优化方案

**文档版本**: 2.0  
**生成日期**: 2025-10-28  
**状态**: 执行计划（已确认用户偏好）

---

## 📋 目录

1. [总体评估](#总体评估)
2. [代码质量分析](#代码质量分析)
3. [优化方案（已选择方案B）](#优化方案)
4. [实施优先级](#实施优先级)
5. [详细实施计划](#详细实施计划)
6. [风险评估](#风险评估)

---

## 用户选择总结

根据文档标注，用户已明确以下偏好：

✅ **选定方案**: **方案 B - 全面重构方案**  
✅ **CMake 方案**: 改为 **STATIC 库**  
✅ **目录结构**: 采用按功能模块划分的结构，`main.cpp` 移至 `examples/`  
✅ **依赖管理**: fast_io 继续使用 `master` 分支（库无固定版本）  
❌ **暂不实施**: CPack 打包、Conan/vcpkg 包管理

---

## 总体评估

### ✅ 优秀方面

1. **架构设计优秀**
   - 接口与实现分离清晰（`i_*.h` 接口，实现在 `.cpp` 中）
   - 使用 C++20 concepts 替代传统继承，类型安全性强
   - 事件驱动架构设计合理，EventBus 和 EventStream 分工明确
   - 多线程系统管理器设计完善，支持 FPS 控制

2. **代码规范良好**
   - 命名一致性高（接口以 `I` 开头，工厂函数统一为 `create_*`）
   - 命名空间使用规范（`Corona::Kernel`, `Corona::PAL`）
   - 线程安全意识强，锁的使用基本正确

3. **测试覆盖率高**
   - 45 个单元测试，覆盖核心功能
   - 包含线程安全测试和性能测试
   - 自定义测试框架轻量高效

### ⚠️ 需要改进的方面

1. **代码组织**
   - 存在功能重复的头文件（`system_base.h` vs `i_system.h`）
   - PAL 层功能不完整（Linux/macOS 支持缺失）
   - 部分功能未实现（VFS 的目录操作）

2. **构建系统**
   - INTERFACE 库的使用可能导致混淆
   - 缺少预编译头文件支持
   - 没有安装规则（install targets）

3. **文档管理**
   - `doc/` 目录为空，设计文档未被纳入版本控制
   - 缺少 API 参考文档
   - 代码注释不足

---

## 代码质量分析

### 1. Kernel 层分析

#### 1.1 EventBus 实现 ✅

**优点**:
- 线程安全实现正确（copy-then-invoke 模式避免死锁）
- 使用 C++20 concepts 实现类型安全
- 性能优化良好（类型擦除 + lambda）

**改进建议**:
```cpp
// 当前实现
void publish_impl(std::type_index type, const void* event_ptr) override {
    std::vector<TypeErasedHandler> handlers_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = subscriptions_.find(type);
        if (it != subscriptions_.end()) {
            for (const auto& sub : it->second) {
                handlers_copy.push_back(sub.handler);
            }
        }
    }
    // 调用处理器...
}

// 优化建议：使用 reserve 减少内存分配
std::vector<TypeErasedHandler> handlers_copy;
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(type);
    if (it != subscriptions_.end()) {
        handlers_copy.reserve(it->second.size());  // ← 添加
        for (const auto& sub : it->second) {
            handlers_copy.push_back(sub.handler);
        }
    }
}
```

#### 1.2 EventStream 实现 ✅

**优点**:
- 队列管理完善，支持背压策略
- RAII 订阅管理避免资源泄漏
- 多订阅者支持良好

**待完成**:
- `event_stream.cpp` 中的模板实例化较少，考虑添加常用类型

#### 1.3 SystemManager 实现 ✅

**优点**:
- 系统生命周期管理完善
- 支持优先级排序
- 性能统计功能完整

**改进建议**:
```cpp
// 当前系统按优先级排序，但每次 initialize_all() 都重新排序
bool initialize_all() override {
    std::lock_guard<std::mutex> lock(mutex_);
    std::sort(systems_.begin(), systems_.end(), /*...*/);
    // ...
}

// 优化：在 register_system 时插入到正确位置，或添加 sorted_ 标志
void register_system(std::shared_ptr<ISystem> system) override {
    std::lock_guard<std::mutex> lock(mutex_);
    // 插入到正确位置保持排序
    auto pos = std::lower_bound(systems_.begin(), systems_.end(), system,
        [](const auto& a, const auto& b) { 
            return a->get_priority() > b->get_priority(); 
        });
    systems_.insert(pos, system);
    systems_by_name_[std::string(system->get_name())] = system;
}
```

#### 1.4 SystemBase 实现 ✅

**优点**:
- 提供完整的线程管理和 FPS 控制
- 性能统计功能完善
- 状态机设计合理

**潜在问题**:
```cpp
// system_base.h 中的线程循环
void thread_loop() {
    while (should_run_.load(std::memory_order_acquire)) {
        // 检查暂停
        if (is_paused_.load(std::memory_order_acquire)) {
            std::unique_lock<std::mutex> lock(pause_mutex_);
            pause_cv_.wait(lock, [this] {
                return !is_paused_.load(std::memory_order_acquire) || 
                       !should_run_.load(std::memory_order_acquire);
            });
        }
        // ...
    }
}
```

**改进**: 条件变量的谓词检查可以简化，避免重复 load

#### 1.5 Logger 实现 ⚠️

**问题**:
1. `level_to_string` 函数重复定义了 3 次
2. 时间格式化使用 `snprintf`，可以考虑 C++20 `<chrono>` 格式化

**优化建议**:
```cpp
// 将 level_to_string 移到匿名命名空间，只定义一次
namespace {
    constexpr std::string_view level_to_string(LogLevel level) {
        switch (level) {
            case LogLevel::trace:   return "TRACE";
            case LogLevel::debug:   return "DEBUG";
            case LogLevel::info:    return "INFO ";
            case LogLevel::warning: return "WARN ";
            case LogLevel::error:   return "ERROR";
            case LogLevel::fatal:   return "FATAL";
            default:                return "UNKNOWN";
        }
    }
}

// ConsoleSink 和 FileSink 可以共享时间格式化逻辑
class LogFormatter {
public:
    static std::string format_timestamp(std::chrono::system_clock::time_point tp) {
        // 实现时间格式化
    }
};
```

#### 1.6 VFS 实现 ⚠️

**未完成功能**:
- `list_directory()` - 返回空向量
- `create_directory()` - 返回 false

**建议**: 
1. 在 PAL 层添加目录操作接口
2. 实现 Windows 版本的目录遍历
3. 添加路径规范化工具函数

#### 1.7 PluginManager 实现 ✅

**优点**:
- 自定义 deleter 管理插件生命周期
- 线程安全的插件加载/卸载

**待改进**:
- 插件依赖排序（代码中有 TODO 注释）

### 2. PAL 层分析

#### 2.1 FileSystem 实现 ⚠️

**问题**:
- `exists()` 方法通过尝试打开文件判断存在性，效率低
- 缺少更多文件系统操作（删除、重命名、属性查询）

**优化建议**:
```cpp
bool exists(std::string_view path) override {
    try {
        // Windows: 使用 GetFileAttributesW
        // Linux: 使用 stat/access
        // 而不是打开文件
        fast_io::native_file file(path, fast_io::open_mode::in);
        return true;
    } catch (...) {
        return false;
    }
}
```

#### 2.2 DynamicLibrary 实现 ⚠️

**问题**:
- 仅支持 Windows
- UTF-8 到 UTF-16 转换使用原始 `new[]`，存在异常安全问题

**优化建议**:
```cpp
// 使用 std::wstring 或 std::vector 管理内存
bool load(std::string_view path) override {
    int path_length = MultiByteToWideChar(CP_UTF8, 0, path.data(), 
                                          static_cast<int>(path.size()), 
                                          nullptr, 0);
    std::wstring wide_path(path_length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.data(), 
                        static_cast<int>(path.size()), 
                        wide_path.data(), path_length);

    handle_ = LoadLibraryW(wide_path.c_str());
    return handle_ != nullptr;
}
```

### 3. 测试代码分析

#### 3.1 测试框架 ✅

**优点**:
- 轻量级，无外部依赖
- 自动注册机制简洁
- 输出格式清晰

**限制**:
- 缺少测试套件分组
- 没有参数化测试支持
- 断言宏较少（仅 `ASSERT_TRUE`, `ASSERT_FALSE`, `ASSERT_EQ`）

**改进建议**:
```cpp
// 添加更多断言宏
#define ASSERT_NE(expected, actual) // 不等断言
#define ASSERT_THROW(expression, exception_type) // 异常断言
#define ASSERT_NO_THROW(expression) // 无异常断言
#define ASSERT_NEAR(val1, val2, epsilon) // 浮点数比较

// 添加测试夹具支持
#define TEST_F(fixture, test_name) // 使用夹具的测试
```

#### 3.2 测试覆盖率 ✅

**已覆盖**:
- EventBus: 基本发布订阅、多订阅者、线程安全
- EventStream: 队列管理、背压策略、订阅生命周期
- SystemManager: 系统生命周期、性能统计、线程控制
- 并发测试: 多线程竞争、数据竞争检测

**缺失测试**:
- Logger: 没有单独的 logger 测试
- VFS: 没有 VFS 测试
- PluginManager: 没有插件加载测试
- PAL 层: 没有 PAL 测试

### 4. CMake 构建系统分析

#### 4.1 当前结构 ⚠️

**问题**:
1. **INTERFACE 库使用混乱**
   ```cmake
   add_library(corona_kernel INTERFACE)
   target_sources(corona_kernel INTERFACE src/event_bus.cpp)
   ```
   - INTERFACE 库通常用于纯头文件库
   - 将 `.cpp` 标记为 INTERFACE 会导致它们被包含到使用者的编译单元中
   - 应该使用 `OBJECT` 或 `STATIC` 库

2. **缺少导出配置**
   - 没有 `install()` 规则
   - 无法作为第三方库被其他项目使用

3. **依赖管理**
   - fast_io 使用 `master` 分支（确认：该库无固定版本或 tag）

#### 4.2 优化建议（已选定方案）

**✅ 选定方案: 改为 STATIC 库**

```cmake
# src/kernel/CMakeLists.txt
add_library(corona_kernel STATIC
    src/logger.cpp
    src/event_bus.cpp
    src/event_stream.cpp
    src/vfs.cpp
    src/plugin_manager.cpp
    src/kernel_context.cpp
    src/system_manager.cpp
)

target_include_directories(corona_kernel PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(corona_kernel PUBLIC cxx_std_20)
target_link_libraries(corona_kernel PUBLIC corona::pal)
add_library(corona::kernel ALIAS corona_kernel)

# 添加安装规则
install(TARGETS corona_kernel
    EXPORT CoronaFrameworkTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(DIRECTORY include/corona
    DESTINATION include
)
```

同样的，`corona_pal` 也需要改为 STATIC 库：

```cmake
# src/pal/CMakeLists.txt
add_library(corona_pal STATIC
    src/fast_io_file_system.cpp
    src/factory.cpp
)

# Add platform-specific sources
if(WIN32)
    target_sources(corona_pal PRIVATE
        windows/win_dynamic_library.cpp
    )
elseif(UNIX AND NOT APPLE)
    target_sources(corona_pal PRIVATE
        linux/linux_dynamic_library.cpp  # 待实现
    )
elseif(APPLE)
    target_sources(corona_pal PRIVATE
        macos/macos_dynamic_library.cpp  # 待实现
    )
endif()

target_include_directories(corona_pal PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${fast_io_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(corona_pal PUBLIC cxx_std_20)
add_library(corona::pal ALIAS corona_pal)

# 添加安装规则
install(TARGETS corona_pal
    EXPORT CoronaFrameworkTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(DIRECTORY include/corona
    DESTINATION include
)
```

### 5. 文件组织分析

#### 5.1 重复文件 ⚠️

检测到以下潜在重复：
- `system_base.h` 和 `dummy_system.h` 都在 `include/corona/kernel/` 下
  - `dummy_system.h` 是测试用的，不应该在公共头文件中
  - 应该移到 `tests/` 或 `examples/` 目录

#### 5.2 目录结构优化（已确定方案）

**当前结构**:
```
src/
├── kernel/
│   ├── include/corona/kernel/
│   └── src/
├── pal/
│   ├── include/corona/pal/
│   ├── src/
│   └── windows/
└── main.cpp
```

**✅ 目标结构**（将按此结构重组）:

```
src/
├── kernel/
│   ├── include/corona/kernel/
│   │   ├── core/              # 核心接口
│   │   │   ├── i_logger.h
│   │   │   ├── i_sink.h
│   │   │   ├── i_vfs.h
│   │   │   ├── i_plugin_manager.h
│   │   │   └── kernel_context.h
│   │   ├── event/             # 事件系统
│   │   │   ├── i_event_bus.h
│   │   │   └── i_event_stream.h
│   │   └── system/            # 系统管理
│   │       ├── i_system.h
│   │       ├── i_system_context.h
│   │       ├── i_system_manager.h
│   │       └── system_base.h
│   └── src/
│       ├── core/
│       │   ├── logger.cpp
│       │   ├── vfs.cpp
│       │   ├── plugin_manager.cpp
│       │   └── kernel_context.cpp
│       ├── event/
│       │   ├── event_bus.cpp
│       │   └── event_stream.cpp
│       └── system/
│           └── system_manager.cpp
├── pal/
│   ├── include/corona/pal/
│   │   ├── i_file_system.h
│   │   └── i_dynamic_library.h
│   ├── src/
│   │   ├── common/            # 公共实现
│   │   │   ├── factory.cpp
│   │   │   └── fast_io_file_system.cpp
│   │   └── platform/          # 平台特定
│   │       ├── windows/
│   │       │   └── win_dynamic_library.cpp
│   │       ├── linux/
│   │       │   └── linux_dynamic_library.cpp  # 待实现
│   │       └── macos/
│   │           └── macos_dynamic_library.cpp  # 待实现
└── examples/                  # 示例代码
    ├── basic/
    │   └── main.cpp           # 从 src/ 移动过来
    ├── plugin/
    │   └── plugin_example.cpp
    └── dummy_system/
        └── dummy_system.h     # 从公共接口移动过来
```

**重组优势**:
1. **清晰的模块划分**: 按功能分类，易于定位和维护
2. **公共接口整洁**: 移除测试代码（`dummy_system.h`）
3. **平台代码分离**: 便于添加新平台支持
4. **示例独立**: `examples/` 目录便于用户学习

---

## 优化方案

### ✅ 已选定：方案 B - 全面重构方案

**目标**: 优化架构和代码组织，为长期发展打好基础

根据用户标注，将实施以下任务（已调整优先级）：

### 第一阶段：基础设施改造（P0 - 必须立即完成）

**预计时间**: 3-5 天

#### 1. 修复 CMake 构建系统 ✅
- 将 `corona_kernel` 和 `corona_pal` 改为 STATIC 库
- 添加完整的安装规则（`install()` 命令）
- 确保 `find_package()` 支持
- 保留 fast_io 使用 master 分支

#### 2. 重组文件结构 ✅
- 按功能模块划分子目录（core/event/system）
- 将 `dummy_system.h` 移至 `examples/dummy_system/`
- 将 `main.cpp` 移至 `examples/basic/`
- 分离平台特定代码到 `src/platform/`

#### 3. 消除代码重复
- 提取 `logger.cpp` 中的公共函数到匿名命名空间
- 创建 `LogFormatter` 辅助类统一时间格式化

#### 4. 修复内存安全问题
- `win_dynamic_library.cpp` 使用 `std::wstring` 替代裸指针

### 第二阶段：功能完善（P1 - 高优先级）

**预计时间**: 1-2 周

#### 5. 增强测试覆盖率
- 添加 `tests/kernel/logger_test.cpp`
- 添加 `tests/kernel/vfs_test.cpp`
- 添加 `tests/kernel/plugin_manager_test.cpp`
- 增强测试框架断言宏（`ASSERT_NE`, `ASSERT_THROW`, `ASSERT_NEAR`）

#### 6. 完成未实现功能
- 实现 VFS 的 `list_directory()` 和 `create_directory()`
- 在 PAL 层添加目录操作接口
- 实现 PluginManager 的依赖排序

#### 7. 性能优化
- EventBus: 在 `publish_impl()` 中添加 `reserve()`
- SystemManager: 在 `register_system()` 时保持排序
- 优化条件变量的谓词检查

### 第三阶段：跨平台支持（P2 - 中期目标）

**预计时间**: 2-3 周

#### 8. 实现 Linux 平台支持
- 实现 `linux_dynamic_library.cpp`（基于 `dlopen`/`dlsym`）
- 添加 Linux 特定的文件系统操作
- 在 CI 中添加 Linux 构建测试

#### 9. 实现 macOS 平台支持
- 实现 `macos_dynamic_library.cpp`
- 适配 macOS 的文件系统特性
- 在 CI 中添加 macOS 构建测试

### 第四阶段：文档与示例（P3 - 持续进行）

**预计时间**: 持续进行

#### 10. 完善文档
- 将设计文档整理到 `doc/` 目录
- 为公共 API 添加 Doxygen 注释
- 创建 API 参考文档（使用 Doxygen 生成）
- 编写插件开发指南

#### 11. 添加示例程序
- `examples/basic/` - 基础使用示例
- `examples/plugin/` - 插件开发示例
- `examples/multi_system/` - 多系统协作示例
- `examples/event_stream/` - 事件流示例

#### 12. 构建系统现代化（部分）
- 添加预编译头文件支持
- 支持 `find_package()` 导入
- ❌ 暂不添加 CPack 打包支持
- ❌ 暂不添加 Conan/vcpkg 支持

### 第五阶段：生态系统建设（长期目标）

**预计时间**: 3-6 个月

#### 13. 持续集成与部署
- 建立 GitHub Actions CI/CD
- 多平台自动化测试
- 自动生成文档并发布

#### 14. 性能基准测试
- 添加性能测试套件
- 事件系统吞吐量测试
- 系统调度性能测试
- 内存使用分析

---

## 详细实施计划

### 阶段 1：基础设施改造（第 1-3 天）

#### Day 1: CMake 构建系统改造

**任务清单**:
- [ ] 备份当前 `src/kernel/CMakeLists.txt` 和 `src/pal/CMakeLists.txt`
- [ ] 修改 `corona_kernel` 为 STATIC 库
- [ ] 修改 `corona_pal` 为 STATIC 库
- [ ] 添加 `install()` 规则
- [ ] 测试构建（Debug/Release 配置）
- [ ] 验证链接无错误

**验收标准**:
```bash
cmake --preset ninja-clang
cmake --build build --config Debug
cmake --build build --config Release
cmake --install build --prefix install
```

#### Day 2-3: 文件结构重组

**任务清单**:
- [ ] 创建新目录结构
  ```bash
  mkdir -p src/kernel/include/corona/kernel/{core,event,system}
  mkdir -p src/kernel/src/{core,event,system}
  mkdir -p src/pal/src/{common,platform/{windows,linux,macos}}
  mkdir -p examples/{basic,plugin,dummy_system}
  ```
- [ ] 移动头文件到对应模块目录
- [ ] 移动实现文件到对应模块目录
- [ ] 移动 `main.cpp` 到 `examples/basic/`
- [ ] 移动 `dummy_system.h` 到 `examples/dummy_system/`
- [ ] 更新所有 `#include` 路径
- [ ] 更新 CMakeLists.txt 文件
- [ ] 测试编译和运行

**文件移动清单**:

```bash
# Kernel 头文件
git mv src/kernel/include/corona/kernel/i_logger.h src/kernel/include/corona/kernel/core/
git mv src/kernel/include/corona/kernel/i_sink.h src/kernel/include/corona/kernel/core/
git mv src/kernel/include/corona/kernel/i_vfs.h src/kernel/include/corona/kernel/core/
git mv src/kernel/include/corona/kernel/i_plugin_manager.h src/kernel/include/corona/kernel/core/
git mv src/kernel/include/corona/kernel/kernel_context.h src/kernel/include/corona/kernel/core/

git mv src/kernel/include/corona/kernel/i_event_bus.h src/kernel/include/corona/kernel/event/
git mv src/kernel/include/corona/kernel/i_event_stream.h src/kernel/include/corona/kernel/event/

git mv src/kernel/include/corona/kernel/i_system.h src/kernel/include/corona/kernel/system/
git mv src/kernel/include/corona/kernel/i_system_context.h src/kernel/include/corona/kernel/system/
git mv src/kernel/include/corona/kernel/i_system_manager.h src/kernel/include/corona/kernel/system/
git mv src/kernel/include/corona/kernel/system_base.h src/kernel/include/corona/kernel/system/

# Kernel 实现文件
git mv src/kernel/src/logger.cpp src/kernel/src/core/
git mv src/kernel/src/vfs.cpp src/kernel/src/core/
git mv src/kernel/src/plugin_manager.cpp src/kernel/src/core/
git mv src/kernel/src/kernel_context.cpp src/kernel/src/core/

git mv src/kernel/src/event_bus.cpp src/kernel/src/event/
git mv src/kernel/src/event_stream.cpp src/kernel/src/event/

git mv src/kernel/src/system_manager.cpp src/kernel/src/system/

# PAL 文件
git mv src/pal/src/factory.cpp src/pal/src/common/
git mv src/pal/src/fast_io_file_system.cpp src/pal/src/common/
git mv src/pal/windows/win_dynamic_library.cpp src/pal/src/platform/windows/

# 示例和测试代码
git mv src/main.cpp examples/basic/
git mv src/kernel/include/corona/kernel/dummy_system.h examples/dummy_system/
```

#### Day 3: 代码清理和修复

**任务清单**:
- [ ] 消除 Logger 代码重复
- [ ] 修复 `win_dynamic_library.cpp` 内存安全问题
- [ ] 添加性能优化（`reserve()` 等）
- [ ] 运行所有测试确保无回归

### 阶段 2：功能完善（第 4-10 天）

#### Day 4-5: 增强测试覆盖率

**任务清单**:
- [ ] 创建 `tests/kernel/logger_test.cpp`
  - 测试不同日志级别
  - 测试多 Sink 支持
  - 测试文件日志
- [ ] 创建 `tests/kernel/vfs_test.cpp`
  - 测试路径解析
  - 测试文件读写
  - 测试挂载点管理
- [ ] 创建 `tests/kernel/plugin_manager_test.cpp`
  - 测试插件加载/卸载
  - 测试插件查询
- [ ] 扩展测试框架宏
  ```cpp
  #define ASSERT_NE(expected, actual)
  #define ASSERT_THROW(expression, exception_type)
  #define ASSERT_NO_THROW(expression)
  #define ASSERT_NEAR(val1, val2, epsilon)
  ```

#### Day 6-7: 完成 VFS 功能

**任务清单**:
- [ ] 在 `i_file_system.h` 添加目录操作接口
  ```cpp
  virtual std::vector<std::string> list_directory(std::string_view path) = 0;
  virtual bool create_directory(std::string_view path) = 0;
  virtual bool remove_directory(std::string_view path) = 0;
  ```
- [ ] 在 `fast_io_file_system.cpp` 实现 Windows 版本
- [ ] 在 `vfs.cpp` 中实现相应功能
- [ ] 添加测试

#### Day 8-10: 性能优化

**任务清单**:
- [ ] EventBus: 添加 `reserve()` 优化
- [ ] SystemManager: 优化系统注册排序
- [ ] SystemBase: 优化条件变量检查
- [ ] 添加性能基准测试
- [ ] 运行性能对比

### 阶段 3：跨平台支持（第 11-25 天）

#### Day 11-17: Linux 平台支持

**任务清单**:
- [ ] 创建 `src/pal/src/platform/linux/linux_dynamic_library.cpp`
  ```cpp
  // 使用 dlopen/dlsym/dlclose
  #include <dlfcn.h>
  ```
- [ ] 在 CMakeLists.txt 添加平台检测
- [ ] 实现 Linux 文件系统特定功能
- [ ] 在 Linux 环境测试
- [ ] 修复发现的问题

#### Day 18-24: macOS 平台支持

**任务清单**:
- [ ] 创建 `src/pal/src/platform/macos/macos_dynamic_library.cpp`
- [ ] 适配 macOS 特性
- [ ] 在 macOS 环境测试
- [ ] 修复发现的问题

#### Day 25: 跨平台测试

**任务清单**:
- [ ] 在 Windows 上完整测试
- [ ] 在 Linux 上完整测试
- [ ] 在 macOS 上完整测试
- [ ] 修复跨平台兼容性问题

### 阶段 4：文档与示例（持续进行）

#### 文档任务

**任务清单**:
- [ ] 为所有公共 API 添加 Doxygen 注释
- [ ] 生成 API 参考文档
- [ ] 编写架构设计文档
- [ ] 编写插件开发指南
- [ ] 编写系统开发指南
- [ ] 创建快速开始教程

#### 示例程序

**任务清单**:
- [ ] `examples/basic/main.cpp` - 基础用法
- [ ] `examples/plugin/` - 插件开发示例
- [ ] `examples/multi_system/` - 多系统协作
- [ ] `examples/event_stream/` - 事件流使用
- [ ] `examples/vfs_usage/` - VFS 使用示例

### 阶段 5：CI/CD 建设（第 26-30 天）

**任务清单**:
- [ ] 创建 `.github/workflows/build.yml`
- [ ] 配置 Windows/Linux/macOS 多平台构建
- [ ] 添加自动化测试
- [ ] 配置代码覆盖率报告
- [ ] 添加静态分析（clang-tidy）
- [ ] 自动生成和发布文档

### 🔴 P0 - 必须立即修复

1. **CMake 构建系统问题**
   - **问题**: INTERFACE 库不应该包含 `.cpp` 源文件
   - **影响**: 可能导致链接错误或重复符号
   - **修复时间**: 2-4 小时
   - **修复方案**: 见"优化方案 - CMake 构建系统"

2. **内存安全问题**
   - **问题**: `win_dynamic_library.cpp` 中使用裸指针 `new[]`
   - **影响**: 异常安全性问题
   - **修复时间**: 1 小时
   - **修复方案**: 使用 `std::wstring`

### 🟡 P1 - 应该尽快修复

3. **代码重复**
   - **问题**: `logger.cpp` 中 `level_to_string` 重复 3 次
   - **影响**: 维护困难，容易出错
   - **修复时间**: 1 小时

4. **测试覆盖缺失**
   - **问题**: Logger, VFS, PluginManager 没有测试
   - **影响**: 代码质量无法保证
   - **修复时间**: 4-6 小时

5. **公共接口污染**
   - **问题**: `dummy_system.h` 在公共头文件中
   - **影响**: API 不清晰
   - **修复时间**: 1 小时

### 🟢 P2 - 可以稍后处理

6. **性能优化**
   - **问题**: 缺少 `reserve`, 频繁排序
   - **影响**: 性能略低
   - **修复时间**: 2-4 小时

7. **功能完成**
   - **问题**: VFS 目录操作未实现
   - **影响**: 功能不完整
   - **修复时间**: 4-6 小时

8. **跨平台支持**
   - **问题**: 仅支持 Windows
   - **影响**: 平台限制
   - **修复时间**: 1-2 周

### 🔵 P3 - 增强性功能

9. **文档改进**
   - **问题**: 文档分散，API 参考缺失
   - **影响**: 上手难度
   - **修复时间**: 1-2 周

10. **构建系统现代化**
    - **问题**: 缺少安装规则、包管理支持
    - **影响**: 集成难度
    - **修复时间**: 1-2 周

---

## 风险评估

### 高风险改动

1. **更改 CMake 库类型**
   - **风险**: 可能破坏现有构建配置
   - **缓解**: 充分测试，保留备份
   - **建议**: 分支开发，逐步合并

2. **重组文件结构**
   - **风险**: 破坏现有代码引用
   - **缓解**: 使用符号链接或别名过渡
   - **建议**: 分阶段进行

### 中风险改动

3. **优化 EventBus/SystemManager**
   - **风险**: 影响性能或线程安全
   - **缓解**: 保留原有测试，添加性能测试
   - **建议**: 性能对比后再决定

4. **PAL 层扩展**
   - **风险**: 平台兼容性问题
   - **缓解**: 在多个平台测试
   - **建议**: 先实现 Linux，再实现 macOS

### 低风险改动

5. **消除代码重复**
   - **风险**: 几乎无风险
   - **建议**: 优先实施

6. **添加测试**
   - **风险**: 无风险，只有好处
   - **建议**: 持续进行

---

## 具体实施步骤

### 第一步：修复 CMake（立即执行）

```cmake
# src/kernel/CMakeLists.txt
add_library(corona_kernel STATIC
    src/logger.cpp
    src/event_bus.cpp
    src/event_stream.cpp
    src/vfs.cpp
    src/plugin_manager.cpp
    src/kernel_context.cpp
    src/system_manager.cpp
)

target_include_directories(corona_kernel PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(corona_kernel PUBLIC cxx_std_20)
target_link_libraries(corona_kernel PUBLIC corona::pal)
add_library(corona::kernel ALIAS corona_kernel)
```

### 第二步：消除 Logger 重复（立即执行）

```cpp
// logger.cpp
namespace Corona::Kernel {
namespace {
    // 将 level_to_string 移到匿名命名空间
    constexpr std::string_view level_to_string(LogLevel level) {
        switch (level) {
            case LogLevel::trace:   return "TRACE";
            case LogLevel::debug:   return "DEBUG";
            case LogLevel::info:    return "INFO ";
            case LogLevel::warning: return "WARN ";
            case LogLevel::error:   return "ERROR";
            case LogLevel::fatal:   return "FATAL";
            default:                return "UNKNOWN";
        }
    }
    
    // 提取时间格式化逻辑
    std::string format_timestamp(std::chrono::system_clock::time_point tp) {
        auto time_t_now = std::chrono::system_clock::to_time_t(tp);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            tp.time_since_epoch()) % 1000;

        std::tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &time_t_now);
#else
        localtime_r(&time_t_now, &tm_buf);
#endif

        char buffer[32];
        std::snprintf(buffer, sizeof(buffer),
                     "[%04d-%02d-%02d %02d:%02d:%02d.%03lld]",
                     tm_buf.tm_year + 1900, tm_buf.tm_mon + 1,
                     tm_buf.tm_mday, tm_buf.tm_hour, tm_buf.tm_min,
                     tm_buf.tm_sec, static_cast<long long>(ms.count()));
        return buffer;
    }
}  // anonymous namespace

// ConsoleSink 和 FileSink 都使用上述函数
}  // namespace Corona::Kernel
```

### 第三步：修复内存安全（立即执行）

```cpp
// win_dynamic_library.cpp
bool load(std::string_view path) override {
    // 使用 std::wstring 代替裸指针
    int path_length = MultiByteToWideChar(
        CP_UTF8, 0, path.data(), static_cast<int>(path.size()), nullptr, 0);
    
    std::wstring wide_path(path_length, L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, path.data(), static_cast<int>(path.size()),
        wide_path.data(), path_length);

    handle_ = LoadLibraryW(wide_path.c_str());
    return handle_ != nullptr;
}
```

### 第四步：添加缺失测试（本周内）

创建以下测试文件：
- `tests/kernel/logger_test.cpp`
- `tests/kernel/vfs_test.cpp`
- `tests/kernel/plugin_manager_test.cpp`

### 第五步：移除测试代码（本周内）

```bash
# 将 dummy_system.h 移到 tests/ 目录
git mv src/kernel/include/corona/kernel/dummy_system.h tests/kernel/
```

---

## 成功指标

### 短期目标（1-2 周）

- [ ] 所有 P0 问题修复
- [ ] CMake 可以正确构建 STATIC 库
- [ ] 代码通过静态分析（clang-tidy）
- [ ] 测试覆盖率 > 80%

### 中期目标（1-2 个月）

- [ ] 支持 Linux 平台
- [ ] 完成所有 TODO 标记的功能
- [ ] 添加 10+ 个示例程序
- [ ] 建立 CI/CD 流程

### 长期目标（3-6 个月）

- [ ] 支持 3 个平台（Windows/Linux/macOS）
- [ ] 成为可被 Conan/vcpkg 安装的包
- [ ] 拥有完整的 API 文档
- [ ] 有外部项目使用本框架

---

## 附录

### A. 工具推荐

1. **静态分析**
   - clang-tidy: 代码质量检查
   - cppcheck: 错误检测
   - include-what-you-use: 头文件优化

2. **性能分析**
   - perf (Linux): CPU 性能分析
   - VTune (Intel): 全面性能分析
   - Tracy: 游戏引擎性能分析

3. **构建工具**
   - ccache: 编译缓存
   - ninja: 快速构建
   - CMake presets: 配置管理

### B. 代码规范补充

1. **命名约定**
   - 接口：`IClassName`
   - 工厂函数：`create_xxx()`
   - 成员变量：`member_name_`
   - 常量：`kConstantName` 或 `CONSTANT_NAME`

2. **注释规范**
   - 公共 API 使用 Doxygen 格式
   - 复杂算法添加说明注释
   - TODO 注释格式：`// TODO(author): description`

3. **错误处理**
   - 使用异常处理资源获取失败
   - 使用 `std::optional` 表示可选返回值
   - 使用 `std::expected` (C++23) 或自定义 Result 类型

### C. 参考资料

1. **C++20 最佳实践**
   - C++ Core Guidelines
   - Effective Modern C++
   - C++20 Ranges 教程

2. **游戏引擎设计**
   - Game Engine Architecture (Jason Gregory)
   - Game Programming Patterns (Robert Nystrom)

3. **并发编程**
   - C++ Concurrency in Action (Anthony Williams)
   - The Art of Multiprocessor Programming

---

## 总结

Corona Framework 已经拥有优秀的架构设计和实现质量。根据用户标注，当前将采用**方案 B - 全面重构方案**，主要任务是：

1. ✅ **修复 CMake 构建系统**（改为 STATIC 库）
2. ✅ **重组文件结构**（按功能模块划分，移动示例代码）
3. **消除代码重复**（提高可维护性）
4. **增强测试覆盖率**（保证代码质量）
5. **实现跨平台支持**（Linux/macOS）
6. **完善文档和示例**（提高可用性）

**暂不实施**:
- ❌ CPack 打包支持
- ❌ Conan/vcpkg 包管理支持

**依赖管理说明**:
- fast_io 将继续使用 `master` 分支（该库无固定版本或 tag）

---

## 实施时间表

### 短期目标（1-2 周）

**Week 1**:
- [x] ~~评审优化方案~~（已完成）
- [ ] Day 1: CMake 构建系统改造
- [ ] Day 2-3: 文件结构重组
- [ ] Day 3: 代码清理和修复
- [ ] Day 4-5: 增强测试覆盖率

**Week 2**:
- [ ] Day 6-7: 完成 VFS 目录操作
- [ ] Day 8-10: 性能优化和基准测试
- [ ] 验证所有测试通过
- [ ] 代码覆盖率达到 80%+

### 中期目标（1-2 个月）

**Week 3-4**: Linux 平台支持
- [ ] Day 11-17: 实现 Linux 动态库加载
- [ ] 添加 Linux 文件系统支持
- [ ] Linux 环境测试

**Week 5-6**: macOS 平台支持
- [ ] Day 18-24: 实现 macOS 动态库加载
- [ ] 适配 macOS 特性
- [ ] macOS 环境测试

**Week 7-8**: 文档和示例
- [ ] Day 25: 跨平台综合测试
- [ ] 编写 API 参考文档
- [ ] 创建示例程序（10+ 个）
- [ ] 编写插件开发指南

### 长期目标（3-6 个月）

**Month 3-4**: CI/CD 建设
- [ ] 建立 GitHub Actions 工作流
- [ ] 多平台自动化构建
- [ ] 自动化测试和覆盖率报告
- [ ] 自动生成和发布文档

**Month 5-6**: 生态系统完善
- [ ] 性能基准测试套件
- [ ] 插件生态系统建设
- [ ] 社区文档和教程
- [ ] 外部项目集成案例

---

## 下一步行动

### 立即执行（本周）

1. **备份当前代码**
   ```bash
   git checkout -b feature/refactor-structure
   git push -u origin feature/refactor-structure
   ```

2. **开始 CMake 改造**
   - 修改 `src/kernel/CMakeLists.txt`
   - 修改 `src/pal/CMakeLists.txt`
   - 测试构建

3. **创建新目录结构**
   - 执行文件移动脚本
   - 更新 `#include` 路径
   - 验证编译

4. **提交第一阶段改动**
   ```bash
   git add .
   git commit -m "refactor: Convert to STATIC libs and reorganize file structure"
   git push
   ```

### 跟踪进度

建议使用 GitHub Issues 跟踪各项任务：

- [ ] #1: 将 INTERFACE 库改为 STATIC 库
- [ ] #2: 重组文件结构（core/event/system）
- [ ] #3: 消除 Logger 代码重复
- [ ] #4: 修复 win_dynamic_library 内存安全
- [ ] #5: 添加 Logger/VFS/PluginManager 测试
- [ ] #6: 实现 VFS 目录操作
- [ ] #7: 性能优化（reserve、排序优化）
- [ ] #8: Linux 平台支持
- [ ] #9: macOS 平台支持
- [ ] #10: 完善文档和示例
