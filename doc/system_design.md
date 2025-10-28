# 系统 (System) 设计与多线程架构

## 1. 简介

系统 (System) 是引擎中执行具体功能的独立单元，如渲染系统、物理系统、动画系统等。为了充分利用现代多核处理器，每个系统运行在独立的线程上，由主线程统一管理其生命周期。

### 1.1 设计目标
*   **并行执行**: 各系统独立运行，最大化 CPU 利用率
*   **生命周期管理**: 主线程控制所有系统线程的启动、暂停、恢复和停止
*   **线程安全**: 系统间通信必须是线程安全的
*   **同步机制**: 提供灵活的同步点，确保帧间数据一致性

---

## 2. 系统接口设计

### 2.1 `ISystem` 核心接口

```cpp
#pragma once
#include <string_view>
#include <atomic>
#include <thread>

namespace Corona::Kernel {

    enum class SystemState {
        idle,      // 未启动
        running,   // 正在运行
        paused,    // 已暂停
        stopping,  // 正在停止
        stopped    // 已停止
    };

    // 系统上下文（提供对全局数据和服务的访问）
    class ISystemContext {
    public:
        virtual ~ISystemContext() = default;

        // 访问内核服务
        virtual ILogger* logger() = 0;
        virtual IEventBus* event_bus() = 0;
        virtual IVfs* vfs() = 0;
        virtual IPluginManager* plugin_manager() = 0;

        // 访问其他系统（可选，需谨慎使用）
        virtual ISystem* get_system(std::string_view name) = 0;

        // 获取帧时间信息
        virtual float get_delta_time() const = 0;
        virtual uint64_t get_frame_number() const = 0;
    };

    // 系统接口
    class ISystem {
    public:
        virtual ~ISystem() = default;

        // 获取系统名称
        virtual std::string_view get_name() const = 0;

        // 获取系统优先级（用于初始化顺序）
        virtual int get_priority() const = 0;

        // 系统初始化（在主线程调用，传入系统上下文）
        virtual bool initialize(ISystemContext* context) = 0;

        // 系统关闭（在主线程调用）
        virtual void shutdown() = 0;

        // 系统更新（在独立线程循环调用，无参数）
        virtual void update() = 0;

        // 帧同步点（可选，用于需要与主线程同步的系统）
        virtual void sync() {}

        // 获取系统目标帧率（0 表示不限制）
        virtual int get_target_fps() const = 0;

        // 获取当前状态
        virtual SystemState get_state() const = 0;

        // 线程控制
        virtual void start() = 0;    // 启动系统线程
        virtual void pause() = 0;    // 暂停系统
        virtual void resume() = 0;   // 恢复系统
        virtual void stop() = 0;     // 停止系统线程
    };

}  // namespace Corona::Kernel
```

### 2.2 `SystemBase` 基类实现

```cpp
#pragma once
#include "i_system.h"
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

namespace Corona::Kernel {

    class SystemBase : public ISystem {
    public:
        SystemBase() : state_(SystemState::idle), 
                       should_run_(false),
                       is_paused_(false),
                       context_(nullptr),
                       target_fps_(60),  // 默认 60 FPS
                       frame_number_(0) {}
        
        virtual ~SystemBase() {
            if (thread_.joinable()) {
                stop();
            }
        }

        SystemState get_state() const override {
            return state_.load(std::memory_order_acquire);
        }

        int get_target_fps() const override {
            return target_fps_;
        }

        void set_target_fps(int fps) {
            target_fps_ = fps;
        }

        void start() override {
            if (state_ != SystemState::idle && state_ != SystemState::stopped) {
                return;
            }

            should_run_.store(true, std::memory_order_release);
            is_paused_.store(false, std::memory_order_release);
            state_.store(SystemState::running, std::memory_order_release);
            
            thread_ = std::thread(&SystemBase::thread_loop, this);
        }

        void pause() override {
            if (state_ != SystemState::running) {
                return;
            }

            is_paused_.store(true, std::memory_order_release);
            state_.store(SystemState::paused, std::memory_order_release);
        }

        void resume() override {
            if (state_ != SystemState::paused) {
                return;
            }

            {
                std::lock_guard<std::mutex> lock(pause_mutex_);
                is_paused_.store(false, std::memory_order_release);
                state_.store(SystemState::running, std::memory_order_release);
            }
            pause_cv_.notify_one();
        }

        void stop() override {
            if (state_ == SystemState::stopped || state_ == SystemState::idle) {
                return;
            }

            state_.store(SystemState::stopping, std::memory_order_release);
            should_run_.store(false, std::memory_order_release);
            
            // 唤醒可能在等待的线程
            {
                std::lock_guard<std::mutex> lock(pause_mutex_);
                is_paused_.store(false, std::memory_order_release);
            }
            pause_cv_.notify_one();

            if (thread_.joinable()) {
                thread_.join();
            }

            state_.store(SystemState::stopped, std::memory_order_release);
        }

    protected:
        // 获取系统上下文
        ISystemContext* context() { return context_; }
        const ISystemContext* context() const { return context_; }

        // 获取当前帧号
        uint64_t frame_number() const { return frame_number_; }

        // 获取上一帧的 delta time（秒）
        float delta_time() const { return last_delta_time_; }

        // 子类可以覆盖此方法来自定义线程循环
        virtual void thread_loop() {
            auto last_time = std::chrono::high_resolution_clock::now();
            
            // 计算帧时间间隔
            std::chrono::microseconds frame_duration(0);
            if (target_fps_ > 0) {
                frame_duration = std::chrono::microseconds(1000000 / target_fps_);
            }

            while (should_run_.load(std::memory_order_acquire)) {
                auto frame_start = std::chrono::high_resolution_clock::now();

                // 检查暂停状态
                if (is_paused_.load(std::memory_order_acquire)) {
                    std::unique_lock<std::mutex> lock(pause_mutex_);
                    pause_cv_.wait(lock, [this] { 
                        return !is_paused_.load(std::memory_order_acquire) || 
                               !should_run_.load(std::memory_order_acquire); 
                    });
                    last_time = std::chrono::high_resolution_clock::now();
                    continue;
                }

                // 计算 delta_time
                auto current_time = std::chrono::high_resolution_clock::now();
                last_delta_time_ = std::chrono::duration<float>(current_time - last_time).count();
                last_time = current_time;

                // 调用子类的更新逻辑（无参数）
                update();

                frame_number_++;

                // 帧率限制
                if (target_fps_ > 0) {
                    auto frame_end = std::chrono::high_resolution_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(frame_end - frame_start);
                    
                    if (elapsed < frame_duration) {
                        std::this_thread::sleep_for(frame_duration - elapsed);
                    }
                }
            }
        }

    private:
        friend class SystemManager;  // 允许 SystemManager 设置 context
        
        void set_context(ISystemContext* context) {
            context_ = context;
        }

        std::atomic<SystemState> state_;
        std::atomic<bool> should_run_;
        std::atomic<bool> is_paused_;
        std::thread thread_;
        std::mutex pause_mutex_;
        std::condition_variable pause_cv_;
        
        ISystemContext* context_;
        int target_fps_;
        uint64_t frame_number_;
        float last_delta_time_;
    };

}  // namespace Corona::Kernel
```

### 2.3 `ISystemManager` 系统管理器接口

```cpp
#pragma once
#include "i_system.h"
#include <memory>
#include <vector>
#include <string_view>

namespace Corona::Kernel {

    class ISystemManager {
    public:
        virtual ~ISystemManager() = default;

        // 注册系统
        virtual void register_system(std::shared_ptr<ISystem> system) = 0;

        // 获取系统
        virtual std::shared_ptr<ISystem> get_system(std::string_view name) = 0;

        // 初始化所有系统（按优先级）
        virtual bool initialize_all() = 0;

        // 启动所有系统线程
        virtual void start_all() = 0;

        // 暂停所有系统
        virtual void pause_all() = 0;

        // 恢复所有系统
        virtual void resume_all() = 0;

        // 停止所有系统
        virtual void stop_all() = 0;

        // 关闭所有系统
        virtual void shutdown_all() = 0;

        // 同步点：等待所有系统完成当前帧
        virtual void sync_all() = 0;

        // 获取所有系统
        virtual std::vector<std::shared_ptr<ISystem>> get_all_systems() = 0;
    };

}  // namespace Corona::Kernel
```

---

## 3. 典型系统示例

### 3.1 渲染系统

```cpp
class RenderSystem : public SystemBase {
public:
    std::string_view get_name() const override { return "RenderSystem"; }
    int get_priority() const override { return 100; }  // 高优先级
    int get_target_fps() const override { return 60; }  // 60 FPS

    bool initialize(ISystemContext* context) override {
        // 初始化渲染上下文、设备等
        // 可以通过 context 访问日志等服务
        context->logger()->info("RenderSystem initializing...");
        return true;
    }

    void shutdown() override {
        // 清理渲染资源
        context()->logger()->info("RenderSystem shutting down...");
    }

    void update() override {
        // 可以访问系统上下文
        auto logger = context()->logger();
        auto event_bus = context()->event_bus();
        
        // 执行渲染命令队列
        // 提交绘制调用
        // 交换缓冲区
        
        // 发布渲染完成事件
        event_bus->publish(RenderFrameCompleteEvent{frame_number()});
    }

    void sync() override {
        // 等待 GPU 完成渲染
    }
};
```

### 3.2 物理系统

```cpp
class PhysicsSystem : public SystemBase {
public:
    std::string_view get_name() const override { return "PhysicsSystem"; }
    int get_priority() const override { return 50; }
    int get_target_fps() const override { return 60; }  // 固定 60 FPS 物理模拟

    bool initialize(ISystemContext* context) override {
        // 初始化物理世界
        context->logger()->info("PhysicsSystem initializing...");
        
        // 订阅碰撞事件
        context->event_bus()->subscribe<CollisionEvent>([this](const CollisionEvent& e) {
            handle_collision(e);
        });
        
        return true;
    }

    void shutdown() override {
        // 清理物理资源
    }

    void update() override {
        float dt = delta_time();  // 获取上一帧的时间
        
        // 物理模拟步进
        physics_world_->step(dt);
        
        // 碰撞检测
        detect_collisions();
        
        // 发布物理事件到事件总线
        context()->event_bus()->publish(PhysicsUpdateEvent{frame_number()});
    }

    void sync() override {
        // 将物理结果同步到主线程
    }

private:
    void handle_collision(const CollisionEvent& e) {
        // 处理碰撞
    }
};
```

### 3.3 动画系统

```cpp
class AnimationSystem : public SystemBase {
public:
    std::string_view get_name() const override { return "AnimationSystem"; }
    int get_priority() const override { return 30; }
    int get_target_fps() const override { return 30; }  // 30 FPS 足够动画更新

    bool initialize(ISystemContext* context) override {
        // 初始化动画资源
        context->logger()->info("AnimationSystem initializing...");
        
        // 可以从 VFS 加载动画数据
        auto vfs = context->vfs();
        // load_animations_from_vfs(vfs);
        
        return true;
    }

    void shutdown() override {
        // 清理动画资源
    }

    void update() override {
        float dt = delta_time();
        
        // 更新动画状态
        update_animations(dt);
        
        // 计算骨骼变换
        compute_bone_transforms();
        
        // 混合动画
        blend_animations();
        
        // 发送动画更新事件
        context()->event_bus()->publish(AnimationUpdateEvent{frame_number()});
    }

private:
    void update_animations(float dt) { /* ... */ }
    void compute_bone_transforms() { /* ... */ }
    void blend_animations() { /* ... */ }
};
```

---

## 4. 主线程控制流程

```cpp
// 引擎主循环
void Engine::run() {
    auto system_manager = kernel_->system_manager();

    // 1. 初始化所有系统
    if (!system_manager->initialize_all()) {
        return;
    }

    // 2. 启动所有系统线程
    system_manager->start_all();

    // 3. 主循环
    while (is_running_) {
        // 处理输入事件（在主线程）
        process_input();

        // 等待所有系统完成当前帧
        system_manager->sync_all();

        // 处理帧间任务
        process_frame_tasks();

        // 可选：限制主线程帧率
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // 4. 停止所有系统
    system_manager->stop_all();

    // 5. 关闭所有系统
    system_manager->shutdown_all();
}
```

---

## 5. 线程安全通信机制

### 5.1 命令队列模式

系统之间不直接调用对方的方法，而是通过命令队列进行异步通信：

```cpp
class RenderCommand {
public:
    virtual ~RenderCommand() = default;
    virtual void execute() = 0;
};

class RenderSystem : public SystemBase {
private:
    // 线程安全的命令队列
    concurrent_queue<std::unique_ptr<RenderCommand>> command_queue_;

public:
    void submit_command(std::unique_ptr<RenderCommand> cmd) {
        command_queue_.push(std::move(cmd));
    }

    void update(float delta_time) override {
        // 执行所有命令
        std::unique_ptr<RenderCommand> cmd;
        while (command_queue_.try_pop(cmd)) {
            cmd->execute();
        }
    }
};
```

### 5.2 事件总线（已有实现）

使用现有的 `EventBus` 进行跨线程事件通信，需确保：
*   事件数据是不可变的或深拷贝
*   订阅者回调是线程安全的
*   事件处理可能在不同线程

### 5.3 共享状态保护

对于必须共享的状态，使用读写锁或原子操作：

```cpp
class TransformSystem {
private:
    std::shared_mutex mutex_;
    std::unordered_map<EntityId, Transform> transforms_;

public:
    Transform get_transform(EntityId id) const {
        std::shared_lock lock(mutex_);
        return transforms_.at(id);
    }

    void set_transform(EntityId id, const Transform& transform) {
        std::unique_lock lock(mutex_);
        transforms_[id] = transform;
    }
};
```

---

## 6. 同步策略

### 6.1 栅栏同步 (Barrier)

在帧边界使用栅栏确保所有系统完成当前帧：

```cpp
class SystemManager {
private:
    std::barrier<> frame_barrier_;

public:
    void sync_all() override {
        // 通知所有系统到达同步点
        for (auto& system : systems_) {
            system->sync();
        }
        
        // 等待所有系统线程到达栅栏
        frame_barrier_.arrive_and_wait();
    }
};
```

### 6.2 流水线模式

某些系统可以形成流水线，前一个系统的输出是下一个的输入：

```
动画系统 -> 物理系统 -> 渲染系统
```

使用双缓冲或三缓冲来避免等待：

```cpp
class AnimationSystem {
private:
    std::array<AnimationData, 2> buffers_;
    std::atomic<int> write_index_{0};

public:
    void update(float delta_time) override {
        int write = write_index_.load(std::memory_order_acquire);
        // 写入当前缓冲区
        update_animations(buffers_[write]);
        
        // 交换缓冲区
        write_index_.store(1 - write, std::memory_order_release);
    }

    const AnimationData& get_read_buffer() const {
        int write = write_index_.load(std::memory_order_acquire);
        return buffers_[1 - write];  // 返回另一个缓冲区
    }
};
```

---

## 7. 性能考虑

### 7.1 线程亲和性

为关键系统设置 CPU 亲和性，避免线程迁移：

```cpp
void SystemBase::set_thread_affinity(int core_id) {
    // Windows
    #ifdef _WIN32
    SetThreadAffinityMask(thread_.native_handle(), 1ULL << core_id);
    #endif
    
    // Linux
    #ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(thread_.native_handle(), sizeof(cpu_set_t), &cpuset);
    #endif
}
```

### 7.2 固定时间步长物理

物理系统通常需要固定时间步长来保证稳定性：

```cpp
class PhysicsSystem : public SystemBase {
private:
    static constexpr float FIXED_TIMESTEP = 1.0f / 60.0f;  // 固定 60 FPS
    float accumulator_ = 0.0f;

public:
    int get_target_fps() const override { return 0; }  // 不限制，由内部控制

    void update() override {
        float dt = delta_time();
        accumulator_ += dt;
        
        // 固定时间步长更新
        while (accumulator_ >= FIXED_TIMESTEP) {
            physics_step(FIXED_TIMESTEP);
            accumulator_ -= FIXED_TIMESTEP;
        }
        
        // 可选：插值渲染状态
        float alpha = accumulator_ / FIXED_TIMESTEP;
        interpolate_states(alpha);
    }

private:
    void physics_step(float dt) { /* 物理模拟 */ }
    void interpolate_states(float alpha) { /* 状态插值 */ }
};
```

### 7.3 系统间帧率协调

不同系统可以有不同的更新频率：

```cpp
// 渲染系统：60 FPS，快速响应
class RenderSystem : public SystemBase {
    int get_target_fps() const override { return 60; }
};

// 物理系统：60 FPS，稳定模拟
class PhysicsSystem : public SystemBase {
    int get_target_fps() const override { return 60; }
};

// 动画系统：30 FPS，节省资源
class AnimationSystem : public SystemBase {
    int get_target_fps() const override { return 30; }
};

// AI 系统：20 FPS，降低负载
class AISystem : public SystemBase {
    int get_target_fps() const override { return 20; }
};

// 音频系统：不限制，事件驱动
class AudioSystem : public SystemBase {
    int get_target_fps() const override { return 0; }  // 不限制
    
    void update() override {
        // 快速处理音频命令队列
        process_audio_commands();
        
        // 短暂休眠避免空转
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
};
```

---

## 8. 系统上下文实现示例

### 8.1 SystemContext 类

```cpp
class SystemContext : public ISystemContext {
public:
    SystemContext(KernelContext* kernel, ISystemManager* system_manager)
        : kernel_(kernel), system_manager_(system_manager) {}

    ILogger* logger() override {
        return kernel_->logger();
    }

    IEventBus* event_bus() override {
        return kernel_->event_bus();
    }

    IVfs* vfs() override {
        return kernel_->vfs();
    }

    IPluginManager* plugin_manager() override {
        return kernel_->plugin_manager();
    }

    ISystem* get_system(std::string_view name) override {
        auto sys = system_manager_->get_system(name);
        return sys ? sys.get() : nullptr;
    }

    float get_delta_time() const override {
        return main_thread_delta_time_;
    }

    uint64_t get_frame_number() const override {
        return main_thread_frame_number_;
    }

    // 由主线程更新
    void update_frame_info(float delta_time, uint64_t frame_number) {
        main_thread_delta_time_ = delta_time;
        main_thread_frame_number_ = frame_number;
    }

private:
    KernelContext* kernel_;
    ISystemManager* system_manager_;
    float main_thread_delta_time_ = 0.0f;
    uint64_t main_thread_frame_number_ = 0;
};
```

### 8.2 SystemManager 初始化系统时传入上下文

```cpp
class SystemManager : public ISystemManager {
private:
    std::unique_ptr<SystemContext> context_;
    std::vector<std::shared_ptr<ISystem>> systems_;

public:
    SystemManager(KernelContext* kernel) 
        : context_(std::make_unique<SystemContext>(kernel, this)) {}

    bool initialize_all() override {
        // 按优先级排序
        std::sort(systems_.begin(), systems_.end(),
                  [](const auto& a, const auto& b) {
                      return a->get_priority() > b->get_priority();
                  });

        // 初始化所有系统，传入上下文
        for (auto& system : systems_) {
            auto* base = dynamic_cast<SystemBase*>(system.get());
            if (base) {
                base->set_context(context_.get());
            }

            if (!system->initialize(context_.get())) {
                return false;
            }
        }
        return true;
    }

    // ... 其他方法
};
```

---

## 9. 调试与监控

### 9.1 系统状态监控

```cpp
struct SystemStats {
    std::string name;
    SystemState state;
    int target_fps;
    float avg_frame_time;
    float max_frame_time;
    uint64_t update_count;
    uint64_t frame_number;
};

class ISystemManager {
public:
    virtual std::vector<SystemStats> get_statistics() = 0;
};
```

### 9.2 死锁检测

在开发模式下启用死锁检测：

```cpp
#ifdef DEBUG_MODE
class LockOrderChecker {
public:
    void lock(const char* lock_name, int order);
    void unlock(const char* lock_name);
};
#endif
```

### 9.3 性能分析

集成性能分析工具（如 Tracy Profiler）标记系统帧：

```cpp
void SystemBase::update() {
    PROFILE_ZONE(get_name());  // Tracy 宏
    // ... 更新逻辑
}
```

### 9.4 系统通信追踪

追踪系统间的事件流：

```cpp
class EventBus {
public:
    void publish(const Event& event) {
        #ifdef DEBUG_MODE
        auto* logger = context_->logger();
        logger->trace("Event published: " + event.type + 
                     " from thread: " + std::this_thread::get_id());
        #endif
        
        // ... 发布逻辑
    }
};
```

---

## 10. 最佳实践

1. **最小化锁的持有时间**: 只在关键区域加锁
2. **避免死锁**: 始终按相同顺序获取多个锁
3. **使用 RAII**: 用 `std::lock_guard` 管理锁
4. **数据局部性**: 系统应尽量操作本地数据，通过 `context()` 访问全局服务
5. **事件优先**: 优先使用事件总线通信而非直接调用其他系统
6. **合理设置帧率**: 根据系统特性设置合适的 `target_fps`
   - 渲染系统：60-144 FPS（响应性）
   - 物理系统：60 FPS（稳定性）
   - 动画系统：30 FPS（平衡）
   - AI 系统：10-20 FPS（可接受延迟）
7. **文档化线程模型**: 清晰标注哪些方法是线程安全的
8. **压力测试**: 使用 Thread Sanitizer 检测竞态条件
9. **优雅降级**: 在单核机器上自动切换到单线程模式
10. **避免系统间直接依赖**: 使用 `context()->get_system()` 应谨慎，优先通过事件通信

---

## 11. 下一步实现

1. 创建 `i_system.h` 和 `i_system_context.h`
2. 创建 `system_base.h` 实现基类
3. 实现 `SystemContext` 类
4. 实现 `SystemManager` 类
5. 修改 `KernelContext` 添加 `system_manager()` 接口
6. 为现有 Kernel 服务添加线程安全保护
7. 创建示例系统（如 `DummySystem`）进行测试
8. 编写多线程单元测试
9. 添加性能监控和统计功能
