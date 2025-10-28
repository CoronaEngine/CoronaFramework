# Corona Framework

> 现代化、高性能的 C++20 游戏引擎核心框架

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Tests](https://img.shields.io/badge/tests-45%20passing-brightgreen.svg)](tests/)

---

## 🎯 项目概述

Corona Framework 是 CoronaEngine 的核心基础框架，采用 C++20 构建，提供：

- **事件系统**: EventBus（推送模式）+ EventStream（队列模式）
- **日志系统**: 多 Sink 架构，支持控制台和文件输出
- **系统管理**: 模块化系统架构，支持动态加载/卸载
- **插件管理**: 灵活的插件系统
- **虚拟文件系统**: 统一的文件访问接口
- **平台抽象层**: 跨平台支持（Windows/Linux/macOS）

---

## ✨ 核心特性

### 🚀 高性能事件系统

#### EventBus（推送模式）
```cpp
// C++20 concepts 编译期类型安全
struct PlayerDiedEvent { int player_id; };

event_bus->subscribe<PlayerDiedEvent>([](const PlayerDiedEvent& evt) {
    // 处理事件
});

event_bus->publish(PlayerDiedEvent{123});
```

**性能**: 89% 性能提升（vs std::any），零分配设计

#### EventStream（队列模式）
```cpp
EventStream<SensorData> stream;
auto sub = stream.subscribe({
    .max_queue_size = 1000,
    .policy = BackpressurePolicy::DropOldest
});

// 发布者线程
stream.publish(SensorData{...});

// 消费者线程
if (auto data = sub.try_pop()) {
    process(*data);
}
```

**性能**: 135万 events/sec 吞吐量

### 🎨 现代 C++ 设计

- **C++20 Concepts**: 编译期类型约束
- **零分配**: 关键路径无堆分配
- **RAII**: 资源自动管理
- **线程安全**: 完整的多线程支持

### 📦 模块化架构

```
Corona Framework
├── Kernel Layer        - 核心抽象
│   ├── Event System   - EventBus + EventStream
│   ├── Logger         - 日志系统
│   ├── System Manager - 系统管理
│   └── Plugin Manager - 插件管理
│
└── PAL Layer          - 平台抽象
    ├── File System    - 文件操作
    ├── Threading      - 线程支持
    └── Memory         - 内存管理
```

---

## 🛠️ 构建说明

### 前置要求

- **编译器**: 
  - Windows: MSVC 2022+ / Clang 15+
  - Linux: GCC 11+ / Clang 15+
  - macOS: Clang 15+ (Xcode 14+)
- **CMake**: 3.20+
- **C++20** 支持

### 构建步骤

```bash
# 克隆仓库
git clone https://github.com/CoronaEngine/CoronaFramework.git
cd CoronaFramework

# 生成构建文件
cmake -B build -S .

# 编译
cmake --build build --config Release

# 运行测试
cd build
ctest --output-on-failure
```

### Windows (Visual Studio)

```powershell
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
.\build\tests\Release\kernel_thread_safety_test.exe
```

---

## 🧪 测试

框架包含 **45 个单元测试**，覆盖所有核心功能：

```bash
# 运行所有测试
ctest

# 运行特定测试
./build/tests/kernel_event_stream_test
./build/tests/kernel_thread_safety_test
./build/tests/kernel_performance_test
```

### 测试覆盖

- ✅ **线程安全测试** (10 个): 多线程竞争条件验证
- ✅ **系统测试** (10 个): 系统生命周期管理
- ✅ **性能测试** (6 个): 性能基准和回归测试
- ✅ **事件流测试** (19 个): EventStream 完整功能

**通过率**: 100% ✅

---

## 📚 文档

- [架构对比](doc/architecture_comparison.md) - 新旧架构对比分析
- [系统设计](doc/system_design.md) - 系统架构详细设计
- [EventBus 重构](doc/eventbus_concepts_refactor.md) - C++20 concepts 重构
- [EventStream 设计](doc/event_stream_design.md) - 队列式事件流设计
- [PAL 设计](doc/pal_design.md) - 平台抽象层设计
- [线程安全](doc/thread_safety_review.md) - 线程安全分析

---

## 📊 性能指标

### EventBus（推送模式）
- **Subscribe**: ~10 ns/op
- **Publish (1 订阅者)**: ~50 ns/op
- **Publish (10 订阅者)**: ~500 ns/op
- **性能提升**: 89% vs std::any

### EventStream（队列模式）
- **Publish**: ~10 ns/event
- **try_pop()**: ~20 ns/event
- **吞吐量**: 135万 events/sec
- **队列操作**: 零分配

---

## 🗺️ 路线图

### ✅ Phase 1: 核心框架（已完成）
- [x] 事件系统（EventBus + EventStream）
- [x] 日志系统
- [x] 系统管理
- [x] 插件架构
- [x] 测试框架

### 🚧 Phase 2: 渲染系统（进行中）
- [ ] 渲染器抽象接口
- [ ] Vulkan 后端
- [ ] D3D12 后端
- [ ] 材质系统

### 📅 Phase 3: 游戏系统
- [ ] 物理引擎集成
- [ ] 音频系统
- [ ] 动画系统
- [ ] 脚本系统（Lua/Python）

---

## 🤝 贡献

欢迎贡献！请查看 [CONTRIBUTING.md](CONTRIBUTING.md) 了解详情。

### 开发规范

- 遵循 C++20 最佳实践
- 使用 `.clang-format` 格式化代码
- 所有 PR 必须通过测试
- 增加新功能需要添加测试

---

## 📄 许可证

本项目采用 MIT 许可证。详见 [LICENSE](LICENSE) 文件。

---

## 🙏 致谢

- [fast_io](https://github.com/cppfastio/fast_io) - 高性能 I/O 库
- C++20 标准委员会

---

## 📧 联系方式

- **项目主页**: https://github.com/CoronaEngine/CoronaFramework
- **问题反馈**: https://github.com/CoronaEngine/CoronaFramework/issues

---

*Built with ❤️ using C++20*
