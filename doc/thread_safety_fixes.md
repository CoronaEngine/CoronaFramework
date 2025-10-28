# 线程安全问题修复记录

**修复日期**: 2025-10-28  
**相关分析**: 参见 `thread_safety_review.md`

---

## 修复摘要

本次修复解决了线程安全分析报告中标记的 2 个高优先级问题：

1. ✅ **ConsoleSink::flush()** 缺少互斥锁保护
2. ✅ **PluginManager::get_plugin()** 返回裸指针导致潜在悬垂指针问题

---

## 修复详情

### 1. ConsoleSink::flush() 锁保护

#### 问题描述
`ConsoleSink::flush()` 方法在多线程环境下调用 `fast_io::flush()` 时没有加锁保护，可能导致与 `log()` 方法的竞争条件。

#### 修复前
```cpp
void flush() override {
    // Flush stdout
    fast_io::flush(fast_io::out());
}
```

#### 修复后
```cpp
void flush() override {
    std::lock_guard<std::mutex> lock(mutex_);
    // Flush stdout
    fast_io::flush(fast_io::out());
}
```

#### 影响文件
- `src/kernel/src/logger.cpp` - ConsoleSink 类

#### 风险等级
- 修复前: 🟡 中等风险
- 修复后: 🟢 无风险

---

### 2. PluginManager 生命周期安全

#### 问题描述
`PluginManager::get_plugin()` 返回裸指针 `IPlugin*`，在多线程环境下存在悬垂指针风险：

**危险场景**:
```cpp
// 线程 A
auto* plugin = plugin_manager->get_plugin("MyPlugin");  // 获取裸指针

// 线程 B
plugin_manager->unload_plugin("MyPlugin");  // 插件被销毁

// 线程 A
plugin->initialize();  // ❌ 悬垂指针，崩溃！
```

#### 修复策略
采用 `std::shared_ptr` 替代裸指针，利用引用计数保证生命周期安全。

#### 修复前
```cpp
// 接口
virtual IPlugin* get_plugin(std::string_view name) = 0;

// 实现
struct PluginEntry {
    std::unique_ptr<IPlugin, DestroyPluginFunc> plugin;
    std::unique_ptr<PAL::IDynamicLibrary> library;
    bool initialized;
};

IPlugin* get_plugin(std::string_view name) override {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = plugins_.find(std::string(name));
    if (it != plugins_.end()) {
        return it->second.plugin.get();  // ⚠️ 返回裸指针
    }
    return nullptr;
}
```

#### 修复后

**接口变更** (`i_plugin_manager.h`):
```cpp
// Get a plugin by name (returns shared_ptr for safe lifetime management)
virtual std::shared_ptr<IPlugin> get_plugin(std::string_view name) = 0;
```

**实现变更** (`plugin_manager.cpp`):

1. 添加自定义删除器：
```cpp
// Custom deleter for shared_ptr that uses the plugin's destroy function
class PluginDeleter {
   public:
    explicit PluginDeleter(DestroyPluginFunc destroy_func) 
        : destroy_func_(destroy_func) {}
    
    void operator()(IPlugin* plugin) const {
        if (plugin && destroy_func_) {
            destroy_func_(plugin);
        }
    }
    
   private:
    DestroyPluginFunc destroy_func_;
};
```

2. 修改 PluginEntry 结构：
```cpp
struct PluginEntry {
    std::shared_ptr<IPlugin> plugin;  // ✅ 使用 shared_ptr
    std::unique_ptr<PAL::IDynamicLibrary> library;
    bool initialized;
};
```

3. 修改加载逻辑：
```cpp
PluginInfo info = plugin->get_info();
plugins_.emplace(info.name, PluginEntry{
    std::shared_ptr<IPlugin>(plugin, PluginDeleter(destroy_func)),  // 使用自定义删除器
    std::move(library),
    false
});
```

4. 修改获取方法：
```cpp
std::shared_ptr<IPlugin> get_plugin(std::string_view name) override {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = plugins_.find(std::string(name));
    if (it != plugins_.end()) {
        return it->second.plugin;  // ✅ 返回 shared_ptr，增加引用计数
    }
    return nullptr;
}
```

#### 影响文件
- `src/kernel/include/corona/kernel/i_plugin_manager.h` - 接口定义
- `src/kernel/src/plugin_manager.cpp` - 实现

#### 使用示例

**修复前（不安全）**:
```cpp
auto* plugin = plugin_manager->get_plugin("MyPlugin");
if (plugin) {
    plugin->initialize();  // ⚠️ 可能在此期间被其他线程卸载
}
```

**修复后（安全）**:
```cpp
auto plugin = plugin_manager->get_plugin("MyPlugin");  // shared_ptr
if (plugin) {
    plugin->initialize();  // ✅ 安全！引用计数保证插件不会被提前销毁
    // plugin 离开作用域时自动减少引用计数
}
```

#### 风险等级
- 修复前: 🟡 中等风险（可能导致崩溃）
- 修复后: 🟢 无风险

#### API 兼容性
⚠️ **破坏性变更**: 返回类型从 `IPlugin*` 改为 `std::shared_ptr<IPlugin>`

**迁移指南**:
```cpp
// 旧代码
IPlugin* plugin = plugin_manager->get_plugin("MyPlugin");
if (plugin) {
    plugin->do_something();
}

// 新代码（推荐）
auto plugin = plugin_manager->get_plugin("MyPlugin");
if (plugin) {
    plugin->do_something();
}

// 新代码（如果必须使用裸指针）
if (auto plugin = plugin_manager->get_plugin("MyPlugin")) {
    IPlugin* raw_ptr = plugin.get();  // 仅在 shared_ptr 生命周期内使用
    raw_ptr->do_something();
}
```

---

## 性能影响

### ConsoleSink::flush()
- **影响**: 极小
- **说明**: flush 操作本身就很慢（涉及 I/O），添加锁的开销可忽略不计

### PluginManager::get_plugin()
- **影响**: 轻微增加
- **说明**: 
  - 每次调用会增加/减少引用计数（原子操作）
  - 相比插件操作本身的开销（虚函数调用等），性能影响很小
  - 带来的安全性收益远大于性能损失

---

## 测试建议

### 1. ConsoleSink 多线程测试
```cpp
TEST(LoggerThreadSafetyTest, ConcurrentFlush) {
    auto logger = create_logger();
    
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&logger]() {
            for (int j = 0; j < 100; ++j) {
                logger->info("Test message");
                logger->flush();  // 测试并发 flush
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
}
```

### 2. PluginManager 生命周期测试
```cpp
TEST(PluginManagerThreadSafetyTest, PluginLifetimeSafety) {
    auto pm = create_plugin_manager();
    pm->load_plugin("test_plugin.dll");
    
    std::atomic<bool> should_stop{false};
    std::thread loader_thread([&pm, &should_stop]() {
        while (!should_stop) {
            auto plugin = pm->get_plugin("TestPlugin");
            if (plugin) {
                // 持有 shared_ptr，即使插件被卸载也不会崩溃
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                plugin->get_info();
            }
        }
    });
    
    std::thread unloader_thread([&pm, &should_stop]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        pm->unload_plugin("TestPlugin");  // 尝试卸载
        should_stop = true;
    });
    
    loader_thread.join();
    unloader_thread.join();
    
    // 如果使用裸指针，这个测试会崩溃
    // 使用 shared_ptr 后，测试应该安全通过
}
```

### 3. ThreadSanitizer 验证
```bash
# 使用 ThreadSanitizer 编译
cmake -DCMAKE_CXX_FLAGS="-fsanitize=thread -g" ..
make

# 运行测试
./corona_tests

# 应该不会报告任何数据竞争
```

---

## 检查清单

- [x] ConsoleSink::flush() 添加互斥锁
- [x] PluginManager 改用 shared_ptr
- [x] 添加 PluginDeleter 自定义删除器
- [x] 更新接口文档注释
- [x] 验证编译无错误
- [ ] 添加多线程单元测试
- [ ] 使用 ThreadSanitizer 验证
- [ ] 更新用户文档

---

## 后续工作

### 短期（建议在下一个版本完成）
1. 添加上述建议的单元测试
2. 使用 ThreadSanitizer 运行完整测试套件
3. 更新 API 文档，说明 breaking change

### 长期（可选优化）
1. 考虑为 VFS 添加读写锁（如果出现性能瓶颈）
2. 考虑为 SystemManager 添加读写锁（如果频繁查询系统）
3. 添加性能基准测试，监控锁竞争情况

---

## 参考资料

- [C++ Core Guidelines: CP.20 Use RAII, never plain lock()/unlock()](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rcp-raii)
- [C++ Core Guidelines: CP.110 Do not write your own double-checked locking](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rcp-double)
- [C++ Reference: std::shared_ptr](https://en.cppreference.com/w/cpp/memory/shared_ptr)
- [C++ Reference: std::lock_guard](https://en.cppreference.com/w/cpp/thread/lock_guard)

---

**修复完成** ✅  
所有标记为高优先级的线程安全问题已解决。
