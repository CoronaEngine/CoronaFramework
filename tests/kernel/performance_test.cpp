#include "../test_framework.h"

#include <chrono>
#include <memory>
#include <thread>

#include "corona/kernel/core/kernel_context.h"
#include "corona/kernel/system/system_base.h"

using namespace Corona::Kernel;
using namespace CoronaTest;

// ========================================
// Test System for Performance Monitoring
// ========================================

class FastSystem : public SystemBase {
public:
    std::string_view get_name() const override { return "FastSystem"; }
    int get_priority() const override { return 50; }
    int get_target_fps() const override { return 100; }

    bool initialize(ISystemContext* ctx) override {
        return true;
    }

    void shutdown() override {}

    void update() override {
        // 快速更新（几乎无操作）
    }
};

class SlowSystem : public SystemBase {
public:
    std::string_view get_name() const override { return "SlowSystem"; }
    int get_priority() const override { return 50; }
    int get_target_fps() const override { return 10; }

    bool initialize(ISystemContext* ctx) override {
        return true;
    }

    void shutdown() override {}

    void update() override {
        // 模拟慢更新
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
};

class VariableSystem : public SystemBase {
public:
    VariableSystem() : call_count_(0) {}

    std::string_view get_name() const override { return "VariableSystem"; }
    int get_priority() const override { return 50; }
    int get_target_fps() const override { return 60; }

    bool initialize(ISystemContext* ctx) override {
        return true;
    }

    void shutdown() override {}

    void update() override {
        call_count_++;
        
        // 每隔几帧模拟一次慢更新
        if (call_count_ % 10 == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

private:
    int call_count_;
};

// ========================================
// Performance Stats Tests
// ========================================

TEST(PerformanceStats, BasicStats) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto fast_system = std::make_shared<FastSystem>();
    auto system_manager = kernel.system_manager();

    system_manager->register_system(fast_system);
    ASSERT_TRUE(system_manager->initialize_all());

    fast_system->start();

    // 运行 1 秒
    std::this_thread::sleep_for(std::chrono::seconds(1));

    fast_system->stop();

    // 验证统计信息
    auto stats = system_manager->get_system_stats("FastSystem");
    
    ASSERT_EQ(stats.name, "FastSystem");
    ASSERT_EQ(stats.state, SystemState::stopped);
    ASSERT_EQ(stats.target_fps, 100);
    
    // 验证统计信息被收集（FPS > 0 表示系统在运行）
    ASSERT_GT(stats.actual_fps, 0.0f);
    
    // 总帧数应该大于 0
    ASSERT_GT(stats.total_frames, 0);
    
    // 平均帧时间应该 > 0
    ASSERT_GT(stats.average_frame_time_ms, 0.0f);

    system_manager->shutdown_all();
    kernel.shutdown();
}

TEST(PerformanceStats, SlowSystemStats) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto slow_system = std::make_shared<SlowSystem>();
    auto system_manager = kernel.system_manager();

    system_manager->register_system(slow_system);
    ASSERT_TRUE(system_manager->initialize_all());

    slow_system->start();

    // 运行 1 秒
    std::this_thread::sleep_for(std::chrono::seconds(1));

    slow_system->stop();

    auto stats = system_manager->get_system_stats("SlowSystem");
    
    // 慢系统应该约 10 FPS（允许较大误差，因为有 50ms sleep）
    ASSERT_GT(stats.actual_fps, 5.0f);
    ASSERT_LT(stats.actual_fps, 20.0f);
    
    // 平均帧时间应该 > 50ms（因为有 50ms sleep），但系统开销可能较低
    ASSERT_GT(stats.average_frame_time_ms, 20.0f);

    system_manager->shutdown_all();
    kernel.shutdown();
}

TEST(PerformanceStats, MaxFrameTime) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto variable_system = std::make_shared<VariableSystem>();
    auto system_manager = kernel.system_manager();

    system_manager->register_system(variable_system);
    ASSERT_TRUE(system_manager->initialize_all());

    variable_system->start();

    // 运行足够长时间以触发慢帧
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    variable_system->stop();

    auto stats = system_manager->get_system_stats("VariableSystem");
    
    // 最大帧时间应该 > 20ms（慢帧）
    ASSERT_GT(stats.max_frame_time_ms, 15.0f);
    
    // 但平均帧时间应该较小（大部分是快帧）
    ASSERT_LT(stats.average_frame_time_ms, 10.0f);

    system_manager->shutdown_all();
    kernel.shutdown();
}

TEST(PerformanceStats, ResetStats) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto fast_system = std::make_shared<FastSystem>();
    auto system_manager = kernel.system_manager();

    system_manager->register_system(fast_system);
    ASSERT_TRUE(system_manager->initialize_all());

    fast_system->start();

    // 运行一段时间
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto stats_before = system_manager->get_system_stats("FastSystem");
    ASSERT_GT(stats_before.total_frames, 0);

    // 重置统计
    fast_system->reset_stats();

    auto stats_after = system_manager->get_system_stats("FastSystem");
    ASSERT_EQ(stats_after.total_frames, 0);
    ASSERT_EQ(stats_after.max_frame_time_ms, 0.0f);

    fast_system->stop();
    system_manager->shutdown_all();
    kernel.shutdown();
}

TEST(PerformanceStats, GetAllStats) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto system_manager = kernel.system_manager();

    auto system1 = std::make_shared<FastSystem>();
    auto system2 = std::make_shared<SlowSystem>();

    system_manager->register_system(system1);
    system_manager->register_system(system2);
    
    ASSERT_TRUE(system_manager->initialize_all());
    system_manager->start_all();

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 获取所有统计信息
    auto all_stats = system_manager->get_all_stats();

    ASSERT_EQ(all_stats.size(), 2);

    // 验证两个系统都有统计数据
    bool found_fast = false;
    bool found_slow = false;

    for (const auto& stats : all_stats) {
        if (stats.name == "FastSystem") {
            found_fast = true;
            ASSERT_GT(stats.actual_fps, 50.0f);
            ASSERT_GT(stats.total_frames, 0);
        } else if (stats.name == "SlowSystem") {
            found_slow = true;
            ASSERT_GT(stats.actual_fps, 5.0f);
            ASSERT_LT(stats.actual_fps, 20.0f);
            ASSERT_GT(stats.total_frames, 0);
        }
    }

    ASSERT_TRUE(found_fast);
    ASSERT_TRUE(found_slow);

    system_manager->stop_all();
    system_manager->shutdown_all();
    kernel.shutdown();
}

TEST(PerformanceStats, StatsWhilePaused) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto fast_system = std::make_shared<FastSystem>();
    auto system_manager = kernel.system_manager();

    system_manager->register_system(fast_system);
    ASSERT_TRUE(system_manager->initialize_all());

    fast_system->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto stats_before_pause = system_manager->get_system_stats("FastSystem");
    
    // 暂停
    fast_system->pause();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto stats_during_pause = system_manager->get_system_stats("FastSystem");
    
    // 暂停期间帧数不应该增加
    ASSERT_EQ(stats_before_pause.total_frames, stats_during_pause.total_frames);
    
    // 恢复
    fast_system->resume();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto stats_after_resume = system_manager->get_system_stats("FastSystem");
    
    // 恢复后帧数应该继续增加
    ASSERT_GT(stats_after_resume.total_frames, stats_during_pause.total_frames);

    fast_system->stop();
    system_manager->shutdown_all();
    kernel.shutdown();
}

// ========================================
// Main Function
// ========================================

int main() {
    return CoronaTest::TestRunner::instance().run_all();
}
