#include <chrono>
#include <memory>
#include <vector>

#include "../test_framework.h"
#include "corona/kernel/core/i_logger.h"
#include "corona/kernel/core/kernel_context.h"
#include "corona/kernel/event/i_event_bus.h"
#include "corona/kernel/system/i_system_manager.h"
#include "corona/kernel/system/system_base.h"

using namespace Corona::Kernel;
using namespace CoronaTest;

// ========================================
// Benchmark Helper
// ========================================

class BenchmarkTimer {
   public:
    void start() {
        start_time_ = std::chrono::high_resolution_clock::now();
    }

    double elapsed_ms() const {
        auto end_time = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end_time - start_time_).count();
    }

   private:
    std::chrono::high_resolution_clock::time_point start_time_;
};

// ========================================
// Test Systems
// ========================================

class BenchmarkSystem : public SystemBase {
   public:
    explicit BenchmarkSystem(int priority)
        : priority_(priority), name_("BenchmarkSystem_" + std::to_string(priority)) {
    }

    std::string_view get_name() const override {
        return name_;
    }

    int get_priority() const override { return priority_; }
    int get_target_fps() const override { return 1000; }

    bool initialize(ISystemContext* ctx) override {
        return true;
    }

    void shutdown() override {}
    void update() override {}

   private:
    int priority_;
    std::string name_;
};

// ========================================
// EventBus Benchmarks
// ========================================

struct BenchmarkEvent {
    int value;
};

TEST(Benchmark, EventBusPublishSubscribe) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto* event_bus = kernel.event_bus();
    ASSERT_TRUE(event_bus != nullptr);

    // 订阅事件
    int event_count = 0;
    event_bus->subscribe<BenchmarkEvent>([&](const BenchmarkEvent& evt) {
        event_count++;
    });

    BenchmarkTimer timer;
    const int iterations = 10000;

    // 性能测试:发布事件
    timer.start();
    for (int i = 0; i < iterations; ++i) {
        event_bus->publish(BenchmarkEvent{i});
    }
    double elapsed = timer.elapsed_ms();

    // 验证所有事件都被处理
    ASSERT_EQ(event_count, iterations);

    // 输出性能指标
    CFW_LOG_INFO("EventBus: Published {} events in {} ms", iterations, elapsed);
    CFW_LOG_INFO("EventBus: Average {} ms per event", elapsed / iterations);
    CFW_LOG_INFO("EventBus: Throughput {} events/sec", iterations / elapsed * 1000);

    // 期望:至少每个事件 < 0.1ms (10,000 events/sec)
    ASSERT_LT(elapsed / iterations, 0.1);

    kernel.shutdown();
}

TEST(Benchmark, EventBusMultipleSubscribers) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto* event_bus = kernel.event_bus();
    ASSERT_TRUE(event_bus != nullptr);

    // 创建多个订阅者
    const int subscriber_count = 100;
    std::vector<int> event_counts(subscriber_count, 0);

    for (int i = 0; i < subscriber_count; ++i) {
        event_bus->subscribe<BenchmarkEvent>([&, i](const BenchmarkEvent& evt) {
            event_counts[i]++;
        });
    }

    BenchmarkTimer timer;
    const int iterations = 1000;

    // 性能测试:多订阅者场景
    timer.start();
    for (int i = 0; i < iterations; ++i) {
        event_bus->publish(BenchmarkEvent{i});
    }
    double elapsed = timer.elapsed_ms();

    // 验证所有订阅者都收到事件
    for (int count : event_counts) {
        ASSERT_EQ(count, iterations);
    }

    CFW_LOG_INFO("EventBus (100 subscribers): Published {} events in {} ms", iterations, elapsed);
    CFW_LOG_INFO("EventBus: Average {} ms per event", elapsed / iterations);

    // 期望:即使有100个订阅者,每个事件也应 < 1ms
    ASSERT_LT(elapsed / iterations, 1.0);

    kernel.shutdown();
}

// ========================================
// SystemManager Benchmarks
// ========================================

TEST(Benchmark, SystemManagerSorting) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto* system_manager = kernel.system_manager();

    // 创建多个系统(不同优先级)
    const int system_count = 100;

    BenchmarkTimer timer;

    // 性能测试:注册大量系统(不应该触发排序)
    timer.start();
    for (int i = 0; i < system_count; ++i) {
        auto sys = std::make_shared<BenchmarkSystem>(i % 10);
        system_manager->register_system(sys);
    }
    double register_time = timer.elapsed_ms();

    // 性能测试:初始化(会触发排序)
    timer.start();
    ASSERT_TRUE(system_manager->initialize_all());
    double init_time = timer.elapsed_ms();

    CFW_LOG_INFO("SystemManager ({} systems):", system_count);
    CFW_LOG_INFO("  Register time: {} ms", register_time);
    CFW_LOG_INFO("  Initialize time (includes sorting): {} ms", init_time);
    CFW_LOG_INFO("  Average register: {} ms/system", register_time / system_count);

    // 期望:注册操作应该非常快(不触发排序)
    ASSERT_LT(register_time / system_count, 0.1);  // 每个系统注册 < 0.1ms

    // 期望:初始化包含排序,但也应该在合理时间内完成
    ASSERT_LT(init_time, 100.0);  // 100个系统初始化 < 100ms

    system_manager->shutdown_all();
    kernel.shutdown();
}

TEST(Benchmark, SystemManagerRegistration) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto* system_manager = kernel.system_manager();

    BenchmarkTimer timer;
    const int system_count = 1000;

    // 性能测试:注册大量系统
    timer.start();
    for (int i = 0; i < system_count; ++i) {
        auto sys = std::make_shared<BenchmarkSystem>(i % 10);
        system_manager->register_system(sys);
    }
    double elapsed = timer.elapsed_ms();

    CFW_LOG_INFO("SystemManager: Registered {} systems in {} ms", system_count, elapsed);
    CFW_LOG_INFO("SystemManager: Average {} ms per system", elapsed / system_count);

    // 期望:每个系统注册 < 0.01ms
    ASSERT_LT(elapsed / system_count, 0.01);

    kernel.shutdown();
}

TEST(Benchmark, SystemManagerLookup) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto* system_manager = kernel.system_manager();

    // 注册一些系统
    const int system_count = 100;
    for (int i = 0; i < system_count; ++i) {
        auto sys = std::make_shared<BenchmarkSystem>(i);
        system_manager->register_system(sys);
    }

    BenchmarkTimer timer;
    const int lookup_count = 10000;

    // 性能测试:系统查找
    timer.start();
    for (int i = 0; i < lookup_count; ++i) {
        std::string name = "BenchmarkSystem_" + std::to_string(i % system_count);
        auto sys = system_manager->get_system(name);
        ASSERT_TRUE(sys != nullptr);
    }
    double elapsed = timer.elapsed_ms();

    CFW_LOG_INFO("SystemManager: {} lookups in {} ms", lookup_count, elapsed);
    CFW_LOG_INFO("SystemManager: Average {} ms per lookup", elapsed / lookup_count);

    // 期望:每次查找 < 0.01ms (使用 map O(log n))
    ASSERT_LT(elapsed / lookup_count, 0.01);

    kernel.shutdown();
}

// ========================================
// Memory Allocation Benchmarks
// ========================================

TEST(Benchmark, EventAllocation) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    BenchmarkTimer timer;
    const int iterations = 100000;

    // 性能测试:事件对象分配
    timer.start();
    for (int i = 0; i < iterations; ++i) {
        BenchmarkEvent evt{i};
        (void)evt;  // 避免优化掉
    }
    double elapsed = timer.elapsed_ms();

    CFW_LOG_INFO("Memory: Allocated {} events in {} ms", iterations, elapsed);
    CFW_LOG_INFO("Memory: Average {} ms per allocation", elapsed / iterations);

    // 期望:栈分配非常快 < 0.0001ms
    ASSERT_LT(elapsed / iterations, 0.0001);

    kernel.shutdown();
}

// ========================================
// Main Function
// ========================================

int main() {
    return CoronaTest::TestRunner::instance().run_all();
}
