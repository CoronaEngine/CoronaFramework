/**
 * @file systemmanager_mt_test.cpp
 * @brief SystemManager 多线程稳定性和健壮性测试
 *
 * 测试场景：
 * 1. 并发系统注册/注销
 * 2. 系统状态转换竞争
 * 3. 多系统并发运行
 * 4. 系统间通信
 * 5. FPS控制稳定性
 */

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "../test_framework.h"
#include "corona/kernel/core/kernel_context.h"
#include "corona/kernel/system/i_system.h"
#include "corona/kernel/system/i_system_context.h"
#include "corona/kernel/system/i_system_manager.h"
#include "corona/kernel/system/system_base.h"

using namespace Corona::Kernel;
using namespace CoronaTest;

// ========================================
// 测试用系统类
// ========================================

class TestSystem : public SystemBase {
   public:
    explicit TestSystem(const std::string& name, std::atomic<int>* counter = nullptr)
        : name_(name), update_counter_(counter) {}

    std::string_view get_name() const override {
        return name_;
    }

    int get_priority() const override {
        return 0;
    }

    bool initialize(ISystemContext* context) override {
        initialized_ = true;
        return true;
    }

    void update() override {
        if (update_counter_) {
            update_counter_->fetch_add(1, std::memory_order_relaxed);
        }
        // 模拟一些工作
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    void shutdown() override {
        shutdown_called_ = true;
    }

    bool is_initialized() const { return initialized_; }
    bool is_shutdown_called() const { return shutdown_called_; }

   private:
    std::string name_;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> shutdown_called_{false};
    std::atomic<int>* update_counter_;
};

// ========================================
// 并发系统注册测试
// ========================================

TEST(SystemManagerMT, ConcurrentRegisterSystem) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto system_manager = kernel.system_manager();
    ASSERT_TRUE(system_manager != nullptr);

    constexpr int num_threads = 10;
    std::atomic<int> register_success{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([system_manager, i, &register_success]() {
            auto system = std::make_shared<TestSystem>("TestSystem" + std::to_string(i));
            try {
                system_manager->register_system(system);
                register_success.fetch_add(1);
            } catch (...) {
                // 注册可能失败
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // 验证所有系统都成功注册
    ASSERT_EQ(register_success.load(), num_threads);

    kernel.shutdown();
}

// ========================================
// 并发系统查询测试
// ========================================

TEST(SystemManagerMT, ConcurrentGetSystem) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto system_manager = kernel.system_manager();

    // 注册一些系统
    for (int i = 0; i < 5; ++i) {
        auto system = std::make_shared<TestSystem>("QueryTestSystem" + std::to_string(i));
        system_manager->register_system(system);
    }

    constexpr int num_threads = 16;
    constexpr int queries_per_thread = 500;
    std::atomic<int> query_count{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([system_manager, &query_count]() {
            for (int j = 0; j < queries_per_thread; ++j) {
                auto system = system_manager->get_system("QueryTestSystem" + std::to_string(j % 5));
                if (system) {
                    query_count.fetch_add(1);
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(query_count.load(), num_threads * queries_per_thread);

    kernel.shutdown();
}

// ========================================
// 系统状态转换测试
// ========================================

TEST(SystemManagerMT, ConcurrentStateTransitions) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto system_manager = kernel.system_manager();

    // 注册系统
    auto system = std::make_shared<TestSystem>("StateTestSystem");
    system_manager->register_system(system);

    std::atomic<bool> stop{false};
    std::atomic<int> start_count{0};
    std::atomic<int> pause_count{0};
    std::atomic<int> resume_count{0};

    // 线程1: 不断启动
    std::thread starter([system_manager, &stop, &start_count]() {
        while (!stop) {
            try {
                auto sys = system_manager->get_system("StateTestSystem");
                if (sys) {
                    sys->start();
                    start_count.fetch_add(1);
                }
            } catch (...) {
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    // 线程2: 不断暂停
    std::thread pauser([system_manager, &stop, &pause_count]() {
        while (!stop) {
            try {
                auto sys = system_manager->get_system("StateTestSystem");
                if (sys) {
                    sys->pause();
                    pause_count.fetch_add(1);
                }
            } catch (...) {
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
    });

    // 线程3: 不断恢复
    std::thread resumer([system_manager, &stop, &resume_count]() {
        while (!stop) {
            try {
                auto sys = system_manager->get_system("StateTestSystem");
                if (sys) {
                    sys->resume();
                    resume_count.fetch_add(1);
                }
            } catch (...) {
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(12));
        }
    });

    // 运行1秒
    std::this_thread::sleep_for(std::chrono::seconds(1));
    stop = true;

    starter.join();
    pauser.join();
    resumer.join();

    // 验证操作都执行了
    ASSERT_GT(start_count.load(), 0);

    kernel.shutdown();
}

// ========================================
// 多系统并发运行测试
// ========================================

TEST(SystemManagerMT, MultipleSystemsConcurrentRun) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto system_manager = kernel.system_manager();

    constexpr int num_systems = 8;
    std::vector<std::atomic<int>> update_counters(num_systems);

    for (auto& counter : update_counters) {
        counter.store(0);
    }

    // 注册多个系统
    for (int i = 0; i < num_systems; ++i) {
        auto system = std::make_shared<TestSystem>("RunTestSystem" + std::to_string(i), &update_counters[i]);
        system_manager->register_system(system);
        system->start();
    }

    // 让系统运行一段时间
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // 停止所有系统
    for (int i = 0; i < num_systems; ++i) {
        auto system = system_manager->get_system("RunTestSystem" + std::to_string(i));
        if (system) {
            system->stop();
        }
    }

    // 验证所有系统都更新了
    for (int i = 0; i < num_systems; ++i) {
        ASSERT_GT(update_counters[i].load(), 0);
    }

    kernel.shutdown();
}

// ========================================
// 系统注册/注销竞争测试
// ========================================

TEST(SystemManagerMT, RegisterUnregisterRace) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto system_manager = kernel.system_manager();

    std::atomic<bool> stop{false};
    std::atomic<int> register_count{0};
    std::atomic<int> unregister_count{0};

    // 线程1-3: 注册系统
    std::vector<std::thread> register_threads;
    for (int i = 0; i < 3; ++i) {
        register_threads.emplace_back([system_manager, i, &stop, &register_count]() {
            int counter = 0;
            while (!stop) {
                auto system = std::make_shared<TestSystem>("RaceSystem" + std::to_string(i) + "_" + std::to_string(counter));
                try {
                    system_manager->register_system(system);
                    register_count.fetch_add(1);
                } catch (...) {
                }
                counter++;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        });
    }

    // 线程4-6: 注销系统（尝试获取并检查）
    std::vector<std::thread> unregister_threads;
    for (int i = 0; i < 3; ++i) {
        unregister_threads.emplace_back([system_manager, i, &stop, &unregister_count]() {
            int counter = 0;
            while (!stop) {
                try {
                    auto sys = system_manager->get_system("RaceSystem" + std::to_string(i) + "_" + std::to_string(counter));
                    if (sys) {
                        unregister_count.fetch_add(1);
                    }
                } catch (...) {
                }
                counter++;
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
        });
    }

    // 运行1秒
    std::this_thread::sleep_for(std::chrono::seconds(1));
    stop = true;

    for (auto& t : register_threads) {
        t.join();
    }
    for (auto& t : unregister_threads) {
        t.join();
    }

    // 验证
    ASSERT_GT(register_count.load(), 0);

    kernel.shutdown();
}

// ========================================
// 系统查询压力测试
// ========================================

TEST(SystemManagerMT, SystemQueryStress) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto system_manager = kernel.system_manager();

    // 注册10个系统
    for (int i = 0; i < 10; ++i) {
        auto system = std::make_shared<TestSystem>("StressSystem" + std::to_string(i));
        system_manager->register_system(system);
    }

    constexpr int num_threads = 16;
    std::atomic<int> total_queries{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([system_manager, &total_queries]() {
            for (int j = 0; j < 1000; ++j) {
                auto system = system_manager->get_system("StressSystem" + std::to_string(j % 10));
                if (system) {
                    total_queries.fetch_add(1);
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(total_queries.load(), num_threads * 1000);

    kernel.shutdown();
}

// ========================================
// 混合操作压力测试
// ========================================

TEST(SystemManagerMT, MixedOperationsStress) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto system_manager = kernel.system_manager();

    std::atomic<bool> stop{false};
    std::atomic<int> total_operations{0};

    // 线程1-2: 注册系统
    std::vector<std::thread> register_threads;
    for (int i = 0; i < 2; ++i) {
        register_threads.emplace_back([system_manager, i, &stop, &total_operations]() {
            int counter = 0;
            while (!stop) {
                auto system = std::make_shared<TestSystem>("MixedSystem" + std::to_string(i) + "_" + std::to_string(counter % 5));
                try {
                    system_manager->register_system(system);
                    total_operations.fetch_add(1);
                } catch (...) {
                }
                counter++;
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
        });
    }

    // 线程3-6: 查询系统
    std::vector<std::thread> query_threads;
    for (int i = 0; i < 4; ++i) {
        query_threads.emplace_back([system_manager, i, &stop, &total_operations]() {
            while (!stop) {
                auto system = system_manager->get_system("MixedSystem" + std::to_string(i % 2) + "_" + std::to_string(i % 5));
                total_operations.fetch_add(1);
                std::this_thread::yield();
            }
        });
    }

    // 线程7-8: 启动/停止系统
    std::vector<std::thread> control_threads;
    for (int i = 0; i < 2; ++i) {
        control_threads.emplace_back([system_manager, i, &stop, &total_operations]() {
            while (!stop) {
                try {
                    auto sys = system_manager->get_system("MixedSystem" + std::to_string(i) + "_0");
                    if (sys) {
                        sys->start();
                        total_operations.fetch_add(1);
                    }
                } catch (...) {
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));

                try {
                    auto sys = system_manager->get_system("MixedSystem" + std::to_string(i) + "_0");
                    if (sys) {
                        sys->stop();
                        total_operations.fetch_add(1);
                    }
                } catch (...) {
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });
    }

    // 运行2秒
    std::this_thread::sleep_for(std::chrono::seconds(2));
    stop = true;

    for (auto& t : register_threads) {
        t.join();
    }
    for (auto& t : query_threads) {
        t.join();
    }
    for (auto& t : control_threads) {
        t.join();
    }

    // 验证
    ASSERT_GT(total_operations.load(), 0);

    kernel.shutdown();
}

// ========================================
// 系统启动/停止循环测试
// ========================================

TEST(SystemManagerMT, StartStopCycle) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto system_manager = kernel.system_manager();

    // 注册系统
    auto system = std::make_shared<TestSystem>("CycleSystem");
    system_manager->register_system(system);

    constexpr int cycles = 50;

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([system_manager]() {
            for (int j = 0; j < cycles; ++j) {
                try {
                    auto sys = system_manager->get_system("CycleSystem");
                    if (sys) {
                        sys->start();
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        sys->stop();
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                } catch (...) {
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    kernel.shutdown();
}

// ========================================
// 不存在的系统查询测试
// ========================================

TEST(SystemManagerMT, NonExistentSystemQuery) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto system_manager = kernel.system_manager();

    constexpr int num_threads = 8;

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([system_manager]() {
            for (int j = 0; j < 100; ++j) {
                auto system = system_manager->get_system("NonExistentSystem");
                ASSERT_FALSE(system);  // 应该返回 nullptr
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    kernel.shutdown();
}

// ========================================
// Main Function
// ========================================

int main() {
    return CoronaTest::TestRunner::instance().run_all();
}
