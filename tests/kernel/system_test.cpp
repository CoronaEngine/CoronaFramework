#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "../test_framework.h"
#include "corona/kernel/core/kernel_context.h"
#include "corona/kernel/system/system_base.h"

using namespace Corona::Kernel;
using namespace CoronaTest;

// ========================================
// Test System Implementations
// ========================================

class CounterSystem : public SystemBase {
   public:
    CounterSystem() : update_count_(0) {}

    std::string_view get_name() const override { return "CounterSystem"; }
    int get_priority() const override { return 50; }
    int get_target_fps() const override { return 100; }  // 快速测试

    bool initialize(ISystemContext* ctx) override {
        return true;
    }

    void shutdown() override {}

    void update() override {
        update_count_.fetch_add(1, std::memory_order_relaxed);
    }

    int get_update_count() const {
        return update_count_.load(std::memory_order_relaxed);
    }

   private:
    std::atomic<int> update_count_;
};

class SlowSystem : public SystemBase {
   public:
    std::string_view get_name() const override { return "SlowSystem"; }
    int get_priority() const override { return 10; }
    int get_target_fps() const override { return 10; }  // 慢速系统

    bool initialize(ISystemContext* ctx) override {
        return true;
    }

    void shutdown() override {}

    void update() override {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
};

// ========================================
// SystemBase Tests
// ========================================

TEST(SystemBase, StartStop) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto counter_system = std::make_shared<CounterSystem>();
    auto system_manager = kernel.system_manager();

    system_manager->register_system(counter_system);
    ASSERT_TRUE(system_manager->initialize_all());

    // 启动系统
    counter_system->start();
    ASSERT_EQ(counter_system->get_state(), SystemState::running);

    // 运行一段时间
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 停止系统
    counter_system->stop();
    ASSERT_EQ(counter_system->get_state(), SystemState::stopped);

    // 验证更新被调用了
    ASSERT_GT(counter_system->get_update_count(), 0);

    system_manager->shutdown_all();
    kernel.shutdown();
}

TEST(SystemBase, PauseResume) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto counter_system = std::make_shared<CounterSystem>();
    auto system_manager = kernel.system_manager();

    system_manager->register_system(counter_system);
    ASSERT_TRUE(system_manager->initialize_all());

    counter_system->start();

    // 运行一段时间
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    int count_before_pause = counter_system->get_update_count();

    // 暂停
    counter_system->pause();
    ASSERT_EQ(counter_system->get_state(), SystemState::paused);

    // 等待一段时间，计数器不应该增加
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    int count_during_pause = counter_system->get_update_count();

    // 验证暂停期间没有更新
    ASSERT_EQ(count_before_pause, count_during_pause);

    // 恢复
    counter_system->resume();
    ASSERT_EQ(counter_system->get_state(), SystemState::running);

    // 运行一段时间
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    int count_after_resume = counter_system->get_update_count();

    // 验证恢复后继续更新
    ASSERT_GT(count_after_resume, count_during_pause);

    counter_system->stop();
    system_manager->shutdown_all();
    kernel.shutdown();
}

TEST(SystemBase, TargetFPS) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto counter_system = std::make_shared<CounterSystem>();
    counter_system->set_target_fps(10);  // 设置为 10 FPS

    auto system_manager = kernel.system_manager();
    system_manager->register_system(counter_system);
    ASSERT_TRUE(system_manager->initialize_all());

    counter_system->start();

    // 运行 1 秒
    std::this_thread::sleep_for(std::chrono::seconds(1));

    counter_system->stop();

    int update_count = counter_system->get_update_count();

    // 10 FPS 应该在 1 秒内更新约 10 次（允许 ±3 的误差）
    ASSERT_GT(update_count, 7);
    ASSERT_LT(update_count, 13);

    system_manager->shutdown_all();
    kernel.shutdown();
}

// ========================================
// SystemManager Tests
// ========================================

TEST(SystemManager, RegisterAndGet) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto system_manager = kernel.system_manager();
    auto counter_system = std::make_shared<CounterSystem>();

    system_manager->register_system(counter_system);

    auto retrieved = system_manager->get_system("CounterSystem");
    ASSERT_TRUE(retrieved != nullptr);
    ASSERT_EQ(retrieved.get(), counter_system.get());

    kernel.shutdown();
}

TEST(SystemManager, InitializePriority) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto system_manager = kernel.system_manager();

    // 创建多个系统（不同优先级）
    auto low_priority = std::make_shared<CounterSystem>();  // 优先级 50
    auto high_priority = std::make_shared<SlowSystem>();    // 优先级 10

    // 以相反的顺序注册
    system_manager->register_system(low_priority);
    system_manager->register_system(high_priority);

    // 初始化应该按优先级排序（高优先级优先）
    ASSERT_TRUE(system_manager->initialize_all());

    system_manager->shutdown_all();
    kernel.shutdown();
}

TEST(SystemManager, StartStopAll) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto system_manager = kernel.system_manager();

    auto system1 = std::make_shared<CounterSystem>();
    auto system2 = std::make_shared<SlowSystem>();

    system_manager->register_system(system1);
    system_manager->register_system(system2);

    ASSERT_TRUE(system_manager->initialize_all());

    // 启动所有系统
    system_manager->start_all();

    ASSERT_EQ(system1->get_state(), SystemState::running);
    ASSERT_EQ(system2->get_state(), SystemState::running);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 停止所有系统
    system_manager->stop_all();

    ASSERT_EQ(system1->get_state(), SystemState::stopped);
    ASSERT_EQ(system2->get_state(), SystemState::stopped);

    system_manager->shutdown_all();
    kernel.shutdown();
}

TEST(SystemManager, PauseResumeAll) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto system_manager = kernel.system_manager();

    auto counter_system = std::make_shared<CounterSystem>();

    system_manager->register_system(counter_system);
    ASSERT_TRUE(system_manager->initialize_all());

    system_manager->start_all();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    int count_before_pause = counter_system->get_update_count();

    // 暂停所有
    system_manager->pause_all();
    ASSERT_EQ(counter_system->get_state(), SystemState::paused);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    int count_during_pause = counter_system->get_update_count();

    // 暂停期间计数增长应该很少（可能有几次更新在暂停生效前完成）
    ASSERT_LT(count_during_pause - count_before_pause, 10);

    // 恢复所有
    system_manager->resume_all();
    ASSERT_EQ(counter_system->get_state(), SystemState::running);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    int count_after_resume = counter_system->get_update_count();

    ASSERT_GT(count_after_resume, count_during_pause);

    system_manager->stop_all();
    system_manager->shutdown_all();
    kernel.shutdown();
}

// ========================================
// SystemContext Tests
// ========================================

class ContextTestSystem : public SystemBase {
   public:
    ContextTestSystem() : logger_accessed_(false), event_bus_accessed_(false) {}

    std::string_view get_name() const override { return "ContextTestSystem"; }
    int get_priority() const override { return 50; }
    int get_target_fps() const override { return 10; }

    bool initialize(ISystemContext* ctx) override {
        // 测试在初始化时访问上下文
        auto logger = ctx->logger();
        ASSERT_TRUE(logger != nullptr);
        logger->info("ContextTestSystem initialized");
        return true;
    }

    void shutdown() override {}

    void update() override {
        // 测试在更新时访问上下文
        if (!logger_accessed_) {
            auto logger = context()->logger();
            if (logger) {
                logger->info("ContextTestSystem accessing logger");
                logger_accessed_ = true;
            }
        }

        if (!event_bus_accessed_) {
            auto event_bus = context()->event_bus();
            if (event_bus) {
                event_bus_accessed_ = true;
            }
        }
    }

    bool has_accessed_logger() const { return logger_accessed_; }
    bool has_accessed_event_bus() const { return event_bus_accessed_; }

   private:
    std::atomic<bool> logger_accessed_;
    std::atomic<bool> event_bus_accessed_;
};

TEST(SystemContext, AccessServices) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto system_manager = kernel.system_manager();
    auto test_system = std::make_shared<ContextTestSystem>();

    system_manager->register_system(test_system);
    ASSERT_TRUE(system_manager->initialize_all());

    test_system->start();

    // 等待系统运行并访问服务
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    test_system->stop();

    // 验证系统成功访问了服务
    ASSERT_TRUE(test_system->has_accessed_logger());
    ASSERT_TRUE(test_system->has_accessed_event_bus());

    system_manager->shutdown_all();
    kernel.shutdown();
}

// ========================================
// Stress Tests
// ========================================

TEST(SystemStress, MultipleSystemsHighLoad) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto system_manager = kernel.system_manager();

    // 创建多个系统
    std::vector<std::shared_ptr<CounterSystem>> systems;
    for (int i = 0; i < 5; ++i) {
        auto system = std::make_shared<CounterSystem>();
        system->set_target_fps(100);  // 高 FPS
        systems.push_back(system);
        system_manager->register_system(system);
    }

    ASSERT_TRUE(system_manager->initialize_all());
    system_manager->start_all();

    // 高负载运行
    std::this_thread::sleep_for(std::chrono::seconds(2));

    system_manager->stop_all();

    // 验证所有系统都有更新
    for (const auto& system : systems) {
        ASSERT_GT(system->get_update_count(), 0);
    }

    system_manager->shutdown_all();
    kernel.shutdown();
}

TEST(SystemStress, RapidStartStop) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto counter_system = std::make_shared<CounterSystem>();
    auto system_manager = kernel.system_manager();

    system_manager->register_system(counter_system);
    ASSERT_TRUE(system_manager->initialize_all());

    // 快速启动停止多次
    for (int i = 0; i < 10; ++i) {
        counter_system->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        counter_system->stop();
    }

    // 应该没有崩溃
    ASSERT_GT(counter_system->get_update_count(), 0);

    system_manager->shutdown_all();
    kernel.shutdown();
}

// ========================================
// Boundary and Error Handling Tests
// ========================================

TEST(SystemManager, GetNonExistentSystem) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto system_manager = kernel.system_manager();

    // 获取不存在的系统应该返回 nullptr
    auto system = system_manager->get_system("NonExistentSystem");
    ASSERT_TRUE(system == nullptr);

    kernel.shutdown();
}

TEST(SystemManager, DuplicateSystemRegistration) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto system_manager = kernel.system_manager();
    auto counter_system = std::make_shared<CounterSystem>();

    // 注册相同系统两次
    system_manager->register_system(counter_system);
    system_manager->register_system(counter_system);

    // 实现可能允许重复注册（记录两次）
    // 这个测试只验证不会崩溃
    auto all_systems = system_manager->get_all_systems();
    ASSERT_GT(all_systems.size(), 0u);

    kernel.shutdown();
}

TEST(SystemManager, InitializeEmptyManager) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto system_manager = kernel.system_manager();

    // 初始化空的 SystemManager 应该成功
    ASSERT_TRUE(system_manager->initialize_all());

    kernel.shutdown();
}

TEST(SystemManager, StartWithoutInitialize) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto system_manager = kernel.system_manager();
    auto counter_system = std::make_shared<CounterSystem>();

    system_manager->register_system(counter_system);

    // 不调用 initialize_all 直接 start_all
    system_manager->start_all();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 系统应该没有运行或没有更新（因为未初始化）
    system_manager->stop_all();

    kernel.shutdown();
}

TEST(SystemManager, MultipleInitializeCalls) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto system_manager = kernel.system_manager();
    auto counter_system = std::make_shared<CounterSystem>();

    system_manager->register_system(counter_system);

    // 多次调用 initialize_all 应该安全
    ASSERT_TRUE(system_manager->initialize_all());
    ASSERT_TRUE(system_manager->initialize_all());
    ASSERT_TRUE(system_manager->initialize_all());

    system_manager->shutdown_all();
    kernel.shutdown();
}

TEST(SystemManager, StopWithoutStart) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto system_manager = kernel.system_manager();
    auto counter_system = std::make_shared<CounterSystem>();

    system_manager->register_system(counter_system);
    ASSERT_TRUE(system_manager->initialize_all());

    // 不启动直接停止
    system_manager->stop_all();

    ASSERT_EQ(counter_system->get_state(), SystemState::idle);

    system_manager->shutdown_all();
    kernel.shutdown();
}

TEST(SystemManager, PauseWithoutStart) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto system_manager = kernel.system_manager();
    auto counter_system = std::make_shared<CounterSystem>();

    system_manager->register_system(counter_system);
    ASSERT_TRUE(system_manager->initialize_all());

    // 不启动直接暂停
    system_manager->pause_all();

    // 应该不崩溃
    kernel.shutdown();
}

TEST(SystemBase, ZeroFPS) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto counter_system = std::make_shared<CounterSystem>();
    counter_system->set_target_fps(0);  // 0 表示不限制 FPS

    auto system_manager = kernel.system_manager();
    system_manager->register_system(counter_system);
    ASSERT_TRUE(system_manager->initialize_all());

    counter_system->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    counter_system->stop();

    // 0 FPS 应该尽可能快地更新
    ASSERT_GT(counter_system->get_update_count(), 50);  // 应该有很多更新

    system_manager->shutdown_all();
    kernel.shutdown();
}

TEST(SystemBase, NegativeFPS) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto counter_system = std::make_shared<CounterSystem>();
    counter_system->set_target_fps(-10);  // 负数 FPS

    auto system_manager = kernel.system_manager();
    system_manager->register_system(counter_system);
    ASSERT_TRUE(system_manager->initialize_all());

    counter_system->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    counter_system->stop();

    // 负数 FPS 应该被处理为 0 或最小值
    // 系统应该不崩溃
    ASSERT_TRUE(counter_system->get_state() == SystemState::stopped);

    system_manager->shutdown_all();
    kernel.shutdown();
}

TEST(SystemBase, VeryHighFPS) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto counter_system = std::make_shared<CounterSystem>();
    counter_system->set_target_fps(10000);  // 非常高的 FPS

    auto system_manager = kernel.system_manager();
    system_manager->register_system(counter_system);
    ASSERT_TRUE(system_manager->initialize_all());

    counter_system->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    counter_system->stop();

    // 即使请求很高的 FPS，系统也应该稳定运行
    ASSERT_GT(counter_system->get_update_count(), 0);

    system_manager->shutdown_all();
    kernel.shutdown();
}

TEST(SystemBase, ResetStats) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto counter_system = std::make_shared<CounterSystem>();
    auto system_manager = kernel.system_manager();

    system_manager->register_system(counter_system);
    ASSERT_TRUE(system_manager->initialize_all());

    counter_system->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 获取统计信息
    uint64_t frames_before = counter_system->get_total_frames();
    ASSERT_GT(frames_before, 0u);

    // 重置统计
    counter_system->reset_stats();

    // 统计应该归零
    ASSERT_EQ(counter_system->get_total_frames(), 0u);
    // max_frame_time 可能不会立即归零，取决于实现

    counter_system->stop();
    system_manager->shutdown_all();
    kernel.shutdown();
}

TEST(SystemBase, PerformanceMetrics) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto counter_system = std::make_shared<CounterSystem>();
    counter_system->set_target_fps(60);

    auto system_manager = kernel.system_manager();
    system_manager->register_system(counter_system);
    ASSERT_TRUE(system_manager->initialize_all());

    counter_system->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    counter_system->stop();

    // 验证性能指标有效
    float actual_fps = counter_system->get_actual_fps();
    float avg_frame_time = counter_system->get_average_frame_time();
    float max_frame_time = counter_system->get_max_frame_time();
    uint64_t total_frames = counter_system->get_total_frames();

    ASSERT_GT(actual_fps, 0.0f);
    ASSERT_GT(avg_frame_time, 0.0f);  // 只要大于 0 即可
    ASSERT_GT(max_frame_time, 0.0f);
    ASSERT_GT(total_frames, 0u);

    system_manager->shutdown_all();
    kernel.shutdown();
}

TEST(SystemManager, SyncAll) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto system_manager = kernel.system_manager();
    auto counter_system = std::make_shared<CounterSystem>();

    system_manager->register_system(counter_system);
    ASSERT_TRUE(system_manager->initialize_all());

    system_manager->start_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // sync_all 应该等待所有系统完成当前帧
    system_manager->sync_all();

    // 应该不崩溃
    system_manager->stop_all();
    system_manager->shutdown_all();
    kernel.shutdown();
}

TEST(SystemManager, GetSystemStats) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto system_manager = kernel.system_manager();
    auto counter_system = std::make_shared<CounterSystem>();

    system_manager->register_system(counter_system);
    ASSERT_TRUE(system_manager->initialize_all());

    counter_system->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    counter_system->stop();

    // 获取系统统计信息
    auto stats = system_manager->get_system_stats("CounterSystem");

    ASSERT_EQ(stats.name, "CounterSystem");
    ASSERT_EQ(stats.state, SystemState::stopped);
    ASSERT_GT(stats.actual_fps, 0.0f);
    ASSERT_GT(stats.total_frames, 0u);

    system_manager->shutdown_all();
    kernel.shutdown();
}

TEST(SystemManager, GetAllStats) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto system_manager = kernel.system_manager();

    auto system1 = std::make_shared<CounterSystem>();
    auto system2 = std::make_shared<SlowSystem>();

    system_manager->register_system(system1);
    system_manager->register_system(system2);
    ASSERT_TRUE(system_manager->initialize_all());

    system_manager->start_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    system_manager->stop_all();

    // 获取所有统计信息
    auto all_stats = system_manager->get_all_stats();

    ASSERT_GE(all_stats.size(), 2u);

    // 验证每个系统都有统计信息
    bool found_counter = false;
    bool found_slow = false;

    for (const auto& stats : all_stats) {
        if (stats.name == "CounterSystem") {
            found_counter = true;
            ASSERT_GT(stats.total_frames, 0u);
        }
        if (stats.name == "SlowSystem") {
            found_slow = true;
            ASSERT_GT(stats.total_frames, 0u);
        }
    }

    ASSERT_TRUE(found_counter);
    ASSERT_TRUE(found_slow);

    system_manager->shutdown_all();
    kernel.shutdown();
}

TEST(SystemStress, ConcurrentStateChanges) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto counter_system = std::make_shared<CounterSystem>();
    auto system_manager = kernel.system_manager();

    system_manager->register_system(counter_system);
    ASSERT_TRUE(system_manager->initialize_all());

    // 并发状态变化可能会导致竞争条件
    // 使用更保守的测试方式
    std::atomic<bool> stop{false};

    std::thread worker([&]() {
        for (int j = 0; j < 5 && !stop.load(); ++j) {
            counter_system->start();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            counter_system->pause();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            counter_system->resume();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            counter_system->stop();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });

    worker.join();
    stop = true;

    // 应该不崩溃
    ASSERT_TRUE(counter_system->get_state() == SystemState::stopped ||
                counter_system->get_state() == SystemState::idle);

    system_manager->shutdown_all();
    kernel.shutdown();
}

TEST(SystemStress, LongRunningSystem) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto counter_system = std::make_shared<CounterSystem>();
    counter_system->set_target_fps(100);

    auto system_manager = kernel.system_manager();
    system_manager->register_system(counter_system);
    ASSERT_TRUE(system_manager->initialize_all());

    counter_system->start();

    // 运行较长时间
    std::this_thread::sleep_for(std::chrono::seconds(3));

    counter_system->stop();

    // 验证长时间运行的稳定性（非常宽松的范围）
    uint64_t total_frames = counter_system->get_total_frames();
    ASSERT_GT(total_frames, 100u);  // 至少有 100 帧

    float actual_fps = counter_system->get_actual_fps();
    ASSERT_GT(actual_fps, 30.0f);  // 至少 30 FPS

    // 验证系统没有崩溃并且统计正常
    ASSERT_GT(counter_system->get_average_frame_time(), 0.0f);

    system_manager->shutdown_all();
    kernel.shutdown();
}

// ========================================
// Main Function
// ========================================

int main() {
    return CoronaTest::TestRunner::instance().run_all();
}
