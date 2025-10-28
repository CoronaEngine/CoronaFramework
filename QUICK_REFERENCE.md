# Corona Framework - 快速参考

> 核心 API 和使用示例

---

## 📋 目录结构

```
CoronaFramework/
├── src/
│   ├── kernel/              # 核心框架
│   │   ├── include/corona/kernel/
│   │   │   ├── i_event_bus.h        ✅ 事件总线（推送）
│   │   │   ├── i_event_stream.h     ✅ 事件流（队列）
│   │   │   ├── i_logger.h           ✅ 日志系统
│   │   │   ├── i_system.h           ✅ 系统接口
│   │   │   └── i_system_manager.h   ✅ 系统管理
│   │   └── src/
│   └── pal/                 # 平台抽象层
│
├── tests/kernel/            # 单元测试 (45 tests)
│   ├── event_stream_test.cpp
│   ├── thread_safety_test.cpp
│   ├── system_test.cpp
│   └── performance_test.cpp
│
├── doc/                     # 文档 (6 files, 110KB)
│   ├── architecture_comparison.md
│   ├── system_design.md
│   ├── event_stream_design.md
│   ├── eventbus_concepts_refactor.md
│   ├── thread_safety_review.md
│   └── pal_design.md
│
├── CODE_REVIEW.md          # 完整代码分析
└── README.md               # 项目说明
```

---

## 🚀 快速开始

### 1. EventBus（推送模式）

```cpp
#include "corona/kernel/i_event_bus.h"

// 定义事件
struct GameStartEvent {
    int level;
    std::string mode;
};

// 获取 EventBus
auto* bus = context->get_event_bus();

// 订阅事件
auto id = bus->subscribe<GameStartEvent>([](const GameStartEvent& evt) {
    std::cout << "Game started: level " << evt.level << "\n";
});

// 发布事件
bus->publish(GameStartEvent{1, "easy"});

// 取消订阅
bus->unsubscribe(id);
```

### 2. EventStream（队列模式）

```cpp
#include "corona/kernel/i_event_stream.h"

using namespace Corona::Kernel;

// 创建事件流
EventStream<SensorData> stream;

// 订阅（配置队列）
auto sub = stream.subscribe({
    .max_queue_size = 1000,
    .policy = BackpressurePolicy::DropOldest
});

// 发布者线程
std::thread publisher([&] {
    while (running) {
        auto data = read_sensor();
        stream.publish(data);
    }
});

// 消费者线程
std::thread consumer([&] {
    while (running) {
        // 非阻塞获取
        if (auto data = sub.try_pop()) {
            process(*data);
        }
        
        // 或者阻塞等待
        if (auto data = sub.wait_for(100ms)) {
            process(*data);
        }
    }
});
```

### 3. Logger 系统

```cpp
#include "corona/kernel/i_logger.h"

// 获取 Logger
auto* logger = context->get_logger();

// 基本日志
logger->info("Application started");
logger->warning("Low memory");
logger->error("Connection failed");

// 格式化日志
logger->info("Player {} scored {} points", player_name, score);

// 性能关键路径（无格式化）
logger->log(LogLevel::Debug, "Fast path");
```

### 4. 系统管理

```cpp
#include "corona/kernel/i_system.h"
#include "corona/kernel/i_system_manager.h"

// 定义系统
class PhysicsSystem : public ISystem {
public:
    void on_initialize() override {
        // 初始化物理引擎
    }
    
    void on_update(float delta_time) override {
        // 更新物理模拟
    }
    
    void on_shutdown() override {
        // 清理资源
    }
};

// 注册系统
auto* manager = context->get_system_manager();
manager->register_system(std::make_unique<PhysicsSystem>());

// 系统生命周期
manager->initialize_all();  // 初始化所有系统

while (running) {
    float dt = calculate_delta_time();
    manager->update_all(dt);  // 更新所有系统
}

manager->shutdown_all();  // 关闭所有系统
```

---

## 📊 性能最佳实践

### EventBus

```cpp
// ✅ 推荐：使用轻量级事件
struct PlayerMoved {
    uint32_t player_id;
    Vector3 position;  // 12 bytes
};

// ❌ 避免：巨大事件
struct WorldSnapshot {
    std::vector<Entity> all_entities;  // 可能数MB
};

// ✅ 改进：使用引用/索引
struct WorldUpdated {
    uint64_t snapshot_id;  // 8 bytes
};
```

### EventStream

```cpp
// ✅ 选择合适的队列大小
EventStreamOptions opts{
    .max_queue_size = 256,      // 低频事件
    .policy = BackpressurePolicy::Block
};

EventStreamOptions high_freq_opts{
    .max_queue_size = 4096,     // 高频事件
    .policy = BackpressurePolicy::DropOldest
};

// ✅ 批量处理
while (auto event = sub.try_pop()) {
    batch.push_back(*event);
}
process_batch(batch);
```

---

## 🧪 测试

### 运行所有测试

```bash
# 构建
cmake --build build --config Release

# 运行测试
cd build
ctest --output-on-failure

# 或单独运行
./tests/Release/kernel_event_stream_test
./tests/Release/kernel_thread_safety_test
```

### 测试覆盖

| 测试套件 | 测试数 | 通过率 | 说明 |
|---------|--------|--------|------|
| event_stream_test | 19 | 100% | EventStream 完整功能 |
| thread_safety_test | 10 | 100% | 多线程竞争条件 |
| system_test | 10 | 100% | 系统生命周期 |
| performance_test | 6 | 100% | 性能基准 |
| **总计** | **45** | **100%** | **全部通过** ✅ |

---

## 📈 性能指标

### EventBus（推送模式）

| 操作 | 延迟 | 说明 |
|------|------|------|
| subscribe() | ~10 ns | 注册处理器 |
| publish (1 订阅者) | ~50 ns | 单订阅者 |
| publish (10 订阅者) | ~500 ns | 多订阅者 |
| unsubscribe() | ~20 ns | 取消订阅 |

**对比**: 比 std::any 实现快 **89%**

### EventStream（队列模式）

| 操作 | 延迟 | 吞吐量 |
|------|------|--------|
| publish() | ~10 ns | 100M events/s |
| try_pop() | ~20 ns | 50M events/s |
| wait_for() | ~100 ns | 10M events/s |

**实测**: **135万 events/sec** (100K events in 74ms)

---

## 🔧 编译选项

### CMake 配置

```bash
# Release 构建（最优性能）
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Debug 构建（调试信息）
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# 启用 ThreadSanitizer
cmake -B build -DENABLE_THREAD_SANITIZER=ON
```

### 编译器要求

- **MSVC**: 2022+ (Windows)
- **Clang**: 15+ (Windows/Linux/macOS)
- **GCC**: 11+ (Linux)

---

## 📚 深入阅读

### 核心文档

1. [架构对比](doc/architecture_comparison.md) (1047 行)
   - 新旧架构详细对比
   - 79% 代码减少分析
   - 10-50x 性能提升

2. [系统设计](doc/system_design.md) (910 行)
   - 系统架构详细设计
   - 模块化与可扩展性
   - 线程模型

3. [EventStream 设计](doc/event_stream_design.md) (671 行)
   - 队列式发布-订阅模式
   - 背压策略详解
   - 使用模式和最佳实践

4. [EventBus 重构](doc/eventbus_concepts_refactor.md) (515 行)
   - C++20 concepts 应用
   - 性能优化技术
   - 迁移指南

5. [线程安全](doc/thread_safety_review.md) (871 行)
   - 多线程安全分析
   - 锁策略
   - 竞争条件预防

6. [PAL 设计](doc/pal_design.md) (126 行)
   - 平台抽象层
   - 跨平台支持

### 完整分析

- [CODE_REVIEW.md](CODE_REVIEW.md) - 完整代码分析报告
  - 代码统计
  - 质量评估
  - 改进建议
  - 路线图

---

## 🤝 常见问题

### Q: EventBus 和 EventStream 如何选择？

**EventBus（推送）**:
- 实时性要求高
- 简单的事件通知
- 订阅者可以立即处理

**EventStream（队列）**:
- 发布者和消费者解耦
- 需要背压控制
- 异步处理
- 多线程环境

### Q: 如何保证线程安全？

所有核心 API 都是线程安全的：
- EventBus: 内部使用细粒度锁
- EventStream: 每个订阅者独立队列
- Logger: Sink 级别加锁
- SystemManager: 初始化/关闭加锁

### Q: 性能优化技巧？

1. 使用轻量级事件结构
2. 避免频繁 subscribe/unsubscribe
3. EventStream 选择合适的队列大小
4. 批量处理事件
5. Release 模式编译

---

## 📧 获取帮助

- **文档**: [docs/](doc/)
- **问题**: [GitHub Issues](https://github.com/CoronaEngine/CoronaFramework/issues)
- **示例**: [tests/kernel/](tests/kernel/)

---

*Last Updated: 2025-10-28*
