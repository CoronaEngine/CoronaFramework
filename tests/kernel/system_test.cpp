#include "../test_framework.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "corona/kernel/kernel_context.h"
#include "corona/kernel/system_base.h"

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
    auto high_priority = std::make_shared<SlowSystem>();     // 优先级 10

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

    ASSERT_EQ(count_before_pause, count_during_pause);

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
// Main Function
// ========================================

int main() {
    return CoronaTest::TestRunner::instance().run_all();
}
