# 现有实现的多线程安全性分析

## 概述

本文档分析 Corona Framework 现有 Kernel 层实现在多线程环境下的安全性，并提出改进建议。

---

## 1. Logger 系统

### 当前实现
```cpp
class ConsoleSink : public ISink {
    void log(const LogMessage& msg) override {
        std::lock_guard<std::mutex> lock(mutex_);  // ✅ 有锁保护
        // ... 格式化和输出
    }
private:
    std::mutex mutex_;
};

class FileSink : public ISink {
    void log(const LogMessage& msg) override {
        std::lock_guard<std::mutex> lock(mutex_);  // ✅ 有锁保护
        // ... 格式化和输出
    }
private:
    fast_io::native_file file_;
    std::mutex mutex_;
};

class Logger : public ILogger {
    void log(LogLevel level, std::string_view message, ...) override {
        std::lock_guard<std::mutex> lock(mutex_);  // ✅ 有锁保护
        for (auto& sink : sinks_) {
            sink->log(msg);
        }
    }
private:
    std::vector<std::shared_ptr<ISink>> sinks_;
    std::mutex mutex_;
};
```

### 线程安全性评估
✅ **基本线程安全**
- 每个 sink 有独立的 mutex 保护
- Logger 本身也有 mutex 保护 sinks 容器
- `log()` 方法可以安全地从多线程调用

⚠️ **潜在问题**
1. **性能瓶颈**: 每次日志都需要获取两次锁（Logger 锁 + Sink 锁）
2. **死锁风险**: 虽然当前实现不会死锁，但如果 sink 回调中调用其他服务可能有风险
3. **阻塞问题**: 所有线程共享同一个互斥锁，高并发时会阻塞

### 改进建议
```cpp
// 使用无锁队列实现异步日志
class AsyncLogger : public ILogger {
public:
    void log(LogLevel level, std::string_view message, ...) override {
        LogMessage msg = {...};
        log_queue_.push(msg);  // 无锁队列，立即返回
    }

private:
    void worker_thread() {
        while (running_) {
            LogMessage msg;
            if (log_queue_.wait_pop(msg)) {
                for (auto& sink : sinks_) {
                    sink->log(msg);  // 在专用线程执行
                }
            }
        }
    }

    concurrent_queue<LogMessage> log_queue_;  // 无锁并发队列
    std::thread worker_;
    std::atomic<bool> running_;
    std::vector<std::shared_ptr<ISink>> sinks_;  // 只在 worker 线程访问，无需锁
};
```

**优势**:
- 调用 `log()` 的线程不会阻塞
- 消除锁竞争
- 更好的缓存局部性（专用线程）

---

## 2. EventBus 系统

### 当前实现
```cpp
class EventBus : public IEventBus {
    EventId subscribe(std::type_index type, EventHandler handler) override {
        std::lock_guard<std::mutex> lock(mutex_);  // ✅ 有锁保护
        subscriptions_[type].push_back({id, std::move(handler)});
        return id;
    }

    void publish(std::type_index type, const std::any& event) override {
        std::vector<EventHandler> handlers_copy;
        {
            std::lock_guard<std::mutex> lock(mutex_);  // ✅ 有锁保护
            // 拷贝处理器列表
            handlers_copy = ...;
        }
        // 不持锁调用处理器 ✅ 避免死锁
        for (const auto& handler : handlers_copy) {
            handler(event);
        }
    }

private:
    std::map<std::type_index, std::vector<Subscription>> subscriptions_;
    std::mutex mutex_;
};
```

### 线程安全性评估
✅ **良好的线程安全设计**
- 所有操作都有锁保护
- `publish` 时拷贝处理器列表，避免回调时持锁（防止死锁）
- 不同事件类型的订阅者共享一个 map，但通过 mutex 串行化

⚠️ **潜在问题**
1. **性能**: 所有事件类型共享一个全局锁，高并发时会成为瓶颈
2. **拷贝开销**: `publish` 时拷贝所有 handler（`std::function` 拷贝可能有堆分配）
3. **ABA 问题**: 如果在回调中取消订阅，可能有时序问题

### 改进建议
```cpp
// 使用分段锁提高并发度
class EventBus : public IEventBus {
private:
    struct EventTypeData {
        std::shared_mutex mutex_;  // 读写锁
        std::vector<Subscription> subscriptions_;
    };

    std::unordered_map<std::type_index, std::unique_ptr<EventTypeData>> event_types_;
    std::shared_mutex global_mutex_;  // 保护 event_types_ 本身

public:
    void publish(std::type_index type, const std::any& event) override {
        std::shared_ptr<EventTypeData> data;
        
        {
            std::shared_lock global_lock(global_mutex_);
            auto it = event_types_.find(type);
            if (it == event_types_.end()) return;
            data = it->second;  // 增加引用计数
        }

        // 读锁允许多个 publish 并发
        std::shared_lock lock(data->mutex_);
        for (const auto& sub : data->subscriptions_) {
            sub.handler(event);
        }
    }
};
```

**优势**:
- 不同事件类型可以并发发布
- 使用读写锁允许多个读者（publish）
- 避免拷贝整个处理器列表

⚠️ **注意**: 回调中不能调用 `subscribe`/`unsubscribe`，否则死锁

---

## 3. VFS 系统

### 当前实现
```cpp
class VirtualFileSystem : public IVirtualFileSystem {
    bool mount(std::string_view virtual_path, ...) override {
        std::lock_guard<std::mutex> lock(mutex_);  // ✅ 有锁保护
        mount_points_[vpath] = ppath;
        return true;
    }

    std::string resolve(std::string_view virtual_path) const override {
        std::lock_guard<std::mutex> lock(mutex_);  // ✅ 有锁保护
        // 查找最长匹配的挂载点
        for (const auto& [mount_vpath, mount_ppath] : mount_points_) {
            // ...
        }
    }

    std::vector<std::byte> read_file(std::string_view virtual_path) override {
        std::string physical_path = resolve(virtual_path);  // ❌ 获取锁
        return file_system_->read_all_bytes(physical_path);  // 可能很慢（I/O）
    }

private:
    std::map<std::string, std::string> mount_points_;
    mutable std::mutex mutex_;
    std::unique_ptr<PAL::IFileSystem> file_system_;
};
```

### 线程安全性评估
⚠️ **有线程安全问题**
1. **持锁 I/O**: `read_file` 先调用 `resolve`（持锁），然后可能在持锁期间执行文件 I/O
   - 当前实现中 `resolve` 释放锁后才执行 I/O，这是安全的 ✅
2. **性能问题**: `mount_points_` 在引擎运行时很少改变，但每次 `resolve` 都要加锁

### 改进建议
```cpp
// 使用读写锁，读操作（resolve, read_file）并发，写操作（mount）独占
class VirtualFileSystem : public IVirtualFileSystem {
private:
    std::map<std::string, std::string> mount_points_;
    mutable std::shared_mutex mutex_;  // 读写锁

public:
    bool mount(std::string_view virtual_path, ...) override {
        std::unique_lock lock(mutex_);  // 写锁
        mount_points_[vpath] = ppath;
        return true;
    }

    std::string resolve(std::string_view virtual_path) const override {
        std::shared_lock lock(mutex_);  // 读锁，允许并发
        // ... 查找逻辑
    }
};
```

**优势**:
- 多个线程可以同时 `resolve` 和 `read_file`
- 只有 `mount`/`unmount` 时才独占

**进一步优化（如果挂载点很少改变）**:
```cpp
// RCU 模式：读完全无锁
class VirtualFileSystem {
private:
    std::atomic<std::shared_ptr<const std::map<...>>> mount_points_;

public:
    std::string resolve(std::string_view path) const {
        auto points = mount_points_.load(std::memory_order_acquire);  // 原子读，无锁
        // 使用 points 查找
    }

    void mount(...) {
        // 创建新 map，原子替换
        auto new_points = std::make_shared<std::map<...>>(*mount_points_.load());
        (*new_points)[vpath] = ppath;
        mount_points_.store(new_points, std::memory_order_release);
    }
};
```

---

## 4. PluginManager 系统

### 当前实现
```cpp
class PluginManager : public IPluginManager {
    bool load_plugin(std::string_view path) override {
        std::lock_guard<std::mutex> lock(mutex_);  // ✅ 有锁保护
        // 加载动态库
        // 创建插件实例
        plugins_.emplace(name, ...);
    }

    IPlugin* get_plugin(std::string_view name) override {
        std::lock_guard<std::mutex> lock(mutex_);  // ⚠️ 返回原始指针
        return it->second.plugin.get();
    }

private:
    std::map<std::string, PluginEntry> plugins_;
    std::mutex mutex_;
};
```

### 线程安全性评估
❌ **严重的线程安全问题**
1. **悬空指针**: `get_plugin` 返回原始指针后释放锁，其他线程可能 `unload_plugin`，导致悬空指针
2. **生命周期管理**: 无法保证返回的 `IPlugin*` 在使用期间有效

### 改进建议
```cpp
class PluginManager : public IPluginManager {
public:
    // 方案 1: 返回 shared_ptr
    std::shared_ptr<IPlugin> get_plugin(std::string_view name) override {
        std::shared_lock lock(mutex_);  // 读锁
        auto it = plugins_.find(std::string(name));
        if (it != plugins_.end()) {
            return it->second.plugin;  // 增加引用计数
        }
        return nullptr;
    }

    void unload_plugin(std::string_view name) override {
        std::unique_lock lock(mutex_);  // 写锁
        auto it = plugins_.find(std::string(name));
        if (it != plugins_.end()) {
            plugins_.erase(it);  // 只有当所有 shared_ptr 释放后才真正销毁
        }
    }

private:
    struct PluginEntry {
        std::shared_ptr<IPlugin> plugin;  // 使用 shared_ptr
        std::unique_ptr<PAL::IDynamicLibrary> library;
        bool initialized;
    };

    std::map<std::string, PluginEntry> plugins_;
    std::shared_mutex mutex_;  // 读写锁
};
```

**优势**:
- 保证插件在使用期间不会被卸载
- 多个线程可以同时 `get_plugin`

---

## 5. KernelContext 系统

### 当前实现
```cpp
class KernelContext {
public:
    static KernelContext& instance() {
        static KernelContext instance;  // ✅ C++11 保证线程安全
        return instance;
    }

    ILogger* logger() { return logger_.get(); }  // ❌ 无锁，返回原始指针
    IEventBus* event_bus() { return event_bus_.get(); }
    // ...

private:
    bool initialized_ = false;  // ❌ 不是原子的
    std::unique_ptr<ILogger> logger_;
    std::unique_ptr<IEventBus> event_bus_;
    // ...
};
```

### 线程安全性评估
⚠️ **有线程安全问题**
1. **初始化竞态**: `initialized_` 不是原子的，多线程调用 `initialize()` 可能重复初始化
2. **访问竞态**: 服务访问方法（如 `logger()`）不检查 `initialized_`，可能返回空指针
3. **生命周期**: 返回原始指针，无法保证在使用期间服务不被 `shutdown()`

### 改进建议
```cpp
class KernelContext {
public:
    static KernelContext& instance() {
        static KernelContext instance;
        return instance;
    }

    bool initialize() {
        std::call_once(init_flag_, [this] {
            // 初始化逻辑
            logger_ = create_logger();
            // ...
            initialized_.store(true, std::memory_order_release);
        });
        return initialized_.load(std::memory_order_acquire);
    }

    std::shared_ptr<ILogger> logger() {
        if (!initialized_.load(std::memory_order_acquire)) {
            return nullptr;
        }
        std::shared_lock lock(mutex_);
        return logger_;
    }

    void shutdown() {
        std::unique_lock lock(mutex_);
        if (initialized_.exchange(false, std::memory_order_acq_rel)) {
            // 关闭逻辑
            logger_.reset();
            // ...
        }
    }

private:
    std::once_flag init_flag_;
    std::atomic<bool> initialized_{false};
    std::shared_mutex mutex_;
    std::shared_ptr<ILogger> logger_;
    std::shared_ptr<IEventBus> event_bus_;
    // ...
};
```

---

## 6. 总体建议

### 6.1 立即修复（高优先级）

1. **PluginManager**: 修改 `get_plugin` 返回 `shared_ptr` 而非原始指针
2. **KernelContext**: 使用 `std::once_flag` 保证初始化的线程安全
3. **KernelContext**: 服务访问方法返回 `shared_ptr` 并检查 `initialized_`

### 6.2 性能优化（中优先级）

1. **Logger**: 实现异步日志，使用无锁队列
2. **EventBus**: 使用分段锁或读写锁提高并发度
3. **VFS**: 使用 `shared_mutex` 允许并发读取

### 6.3 架构改进（长期）

1. **引入 ServiceLocator 模式**: 统一管理所有服务的生命周期和线程安全
2. **无锁数据结构**: 为高频访问的数据结构（如事件订阅）使用无锁实现
3. **线程本地存储**: 为日志等频繁使用的服务提供 TLS 缓存

---

## 7. 测试建议

### 7.1 单元测试
```cpp
// 测试多线程日志
TEST(Logger, MultithreadedLogging) {
    auto logger = create_logger();
    std::vector<std::thread> threads;
    
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&logger, i] {
            for (int j = 0; j < 1000; ++j) {
                logger->info("Thread " + std::to_string(i) + " message " + std::to_string(j));
            }
        });
    }
    
    for (auto& t : threads) t.join();
    // 验证日志文件完整性
}
```

### 7.2 压力测试
```cpp
// 使用 Google Benchmark 测试并发性能
BENCHMARK(EventBus_Publish)->ThreadRange(1, 16)->UseRealTime();
```

### 7.3 竞态检测
```bash
# 使用 Thread Sanitizer 编译
cmake -DCMAKE_CXX_FLAGS="-fsanitize=thread" ..
cmake --build .
./CoronaFramework
```

---

## 8. 代码审查清单

在实现新系统时，确保：

- [ ] 所有共享状态都有适当的同步机制（mutex, atomic, lock-free）
- [ ] 避免在持锁时执行耗时操作（I/O, 复杂计算）
- [ ] 避免死锁（锁顺序一致，或使用 `std::scoped_lock`）
- [ ] 返回的指针/引用生命周期明确（优先使用 `shared_ptr`）
- [ ] 使用 `const` 方法配合 `mutable` mutex 保护不变式
- [ ] 优先使用高层同步原语（`shared_mutex`, `atomic`, `call_once`）
- [ ] 为线程安全的类添加文档注释说明
- [ ] 编写多线程单元测试
- [ ] 使用 Thread Sanitizer 验证

---

## 9. 下一步行动

1. **创建线程安全改进分支**: `feature/thread-safety-improvements`
2. **按优先级修复问题**:
   - 第一批：KernelContext, PluginManager（修复悬空指针）
   - 第二批：EventBus, VFS（性能优化）
   - 第三批：Logger（异步实现）
3. **添加多线程测试套件**
4. **更新文档**: 标注所有公共 API 的线程安全性
5. **代码审查**: 由团队成员交叉审查修改
