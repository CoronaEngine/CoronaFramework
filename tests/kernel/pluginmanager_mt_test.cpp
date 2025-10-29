/**
 * @file pluginmanager_mt_test.cpp
 * @brief PluginManager 多线程稳定性和健壮性测试
 *
 * 测试场景：
 * 1. 并发插件查询
 * 2. 插件接口调用竞争
 * 3. 插件生命周期管理
 * 4. 多线程插件迭代
 */

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "../test_framework.h"
#include "corona/kernel/core/i_plugin_manager.h"
#include "corona/kernel/core/kernel_context.h"

using namespace Corona::Kernel;
using namespace CoronaTest;

// ========================================
// 并发插件查询测试
// ========================================

TEST(PluginManagerMT, ConcurrentGetPlugin) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto plugin_manager = kernel.plugin_manager();
    ASSERT_TRUE(plugin_manager != nullptr);

    constexpr int num_threads = 16;
    constexpr int queries_per_thread = 500;
    std::atomic<int> query_count{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([plugin_manager, &query_count]() {
            for (int j = 0; j < queries_per_thread; ++j) {
                // 查询不存在的插件
                auto plugin = plugin_manager->get_plugin("NonExistentPlugin" + std::to_string(j % 10));
                ASSERT_FALSE(plugin);  // 应该返回 nullptr
                query_count.fetch_add(1, std::memory_order_relaxed);
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
// 并发插件迭代测试
// ========================================

TEST(PluginManagerMT, ConcurrentIteration) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto plugin_manager = kernel.plugin_manager();

    constexpr int num_threads = 8;
    std::atomic<int> iteration_count{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([plugin_manager, &iteration_count]() {
            for (int j = 0; j < 100; ++j) {
                // 获取所有插件
                auto plugins = plugin_manager->get_loaded_plugins();
                iteration_count.fetch_add(1, std::memory_order_relaxed);

                // 遍历插件列表
                for (const auto& plugin_name : plugins) {
                    // 只验证不崩溃
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(iteration_count.load(), num_threads * 100);

    kernel.shutdown();
}

// ========================================
// 混合查询测试
// ========================================

TEST(PluginManagerMT, MixedQueryOperations) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto plugin_manager = kernel.plugin_manager();

    std::atomic<bool> stop{false};
    std::atomic<int> get_plugin_ops{0};
    std::atomic<int> get_all_ops{0};

    // 线程1-4: 不断查询单个插件
    std::vector<std::thread> get_plugin_threads;
    for (int i = 0; i < 4; ++i) {
        get_plugin_threads.emplace_back([plugin_manager, &stop, &get_plugin_ops, i]() {
            while (!stop) {
                auto plugin = plugin_manager->get_plugin("Plugin" + std::to_string(i % 5));
                get_plugin_ops.fetch_add(1);
                std::this_thread::yield();
            }
        });
    }

    // 线程5-8: 不断获取所有插件
    std::vector<std::thread> get_all_threads;
    for (int i = 0; i < 4; ++i) {
        get_all_threads.emplace_back([plugin_manager, &stop, &get_all_ops]() {
            while (!stop) {
                auto plugins = plugin_manager->get_loaded_plugins();
                get_all_ops.fetch_add(1);
                std::this_thread::yield();
            }
        });
    }

    // 运行1秒
    std::this_thread::sleep_for(std::chrono::seconds(1));
    stop = true;

    for (auto& t : get_plugin_threads) {
        t.join();
    }
    for (auto& t : get_all_threads) {
        t.join();
    }

    // 验证
    ASSERT_GT(get_plugin_ops.load(), 0);
    ASSERT_GT(get_all_ops.load(), 0);

    kernel.shutdown();
}

// ========================================
// 高频查询压力测试
// ========================================

TEST(PluginManagerMT, HighFrequencyQueryStress) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto plugin_manager = kernel.plugin_manager();

    constexpr int num_threads = 16;
    std::atomic<int> total_queries{0};

    auto start_time = std::chrono::steady_clock::now();

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([plugin_manager, &total_queries, i]() {
            for (int j = 0; j < 1000; ++j) {
                // 快速查询
                auto plugin = plugin_manager->get_plugin("TestPlugin" + std::to_string(j % 20));
                total_queries.fetch_add(1);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    ASSERT_EQ(total_queries.load(), num_threads * 1000);

    kernel.shutdown();
}

// ========================================
// 插件名称冲突测试
// ========================================

TEST(PluginManagerMT, PluginNameConflicts) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto plugin_manager = kernel.plugin_manager();

    constexpr int num_threads = 10;

    // 多个线程查询相同的插件名
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([plugin_manager]() {
            for (int j = 0; j < 100; ++j) {
                auto plugin = plugin_manager->get_plugin("CommonPluginName");
                // 应该总是返回相同的结果（nullptr 或有效指针）
                std::this_thread::yield();
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    kernel.shutdown();
}

// ========================================
// 快速初始化/关闭循环测试
// ========================================

TEST(PluginManagerMT, RapidInitShutdownCycle) {
    constexpr int cycles = 10;

    for (int i = 0; i < cycles; ++i) {
        auto& kernel = KernelContext::instance();
        ASSERT_TRUE(kernel.initialize());

        auto plugin_manager = kernel.plugin_manager();
        ASSERT_TRUE(plugin_manager != nullptr);

        // 多线程查询插件
        std::vector<std::thread> threads;
        for (int j = 0; j < 4; ++j) {
            threads.emplace_back([plugin_manager, i, j]() {
                for (int k = 0; k < 50; ++k) {
                    auto plugin = plugin_manager->get_plugin("CyclePlugin" + std::to_string(k));
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        kernel.shutdown();
    }
}

// ========================================
// 插件查询爆发测试
// ========================================

TEST(PluginManagerMT, QueryBurst) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto plugin_manager = kernel.plugin_manager();

    std::atomic<int> burst_count{0};

    // 16个线程同时爆发查询
    std::vector<std::thread> threads;
    for (int i = 0; i < 16; ++i) {
        threads.emplace_back([plugin_manager, &burst_count, i]() {
            for (int j = 0; j < 500; ++j) {
                auto plugin = plugin_manager->get_plugin("BurstPlugin" + std::to_string(i));
                burst_count.fetch_add(1);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(burst_count.load(), 8000);

    kernel.shutdown();
}

// ========================================
// 长插件名称测试
// ========================================

TEST(PluginManagerMT, LongPluginNames) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto plugin_manager = kernel.plugin_manager();

    constexpr int num_threads = 8;

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([plugin_manager, i]() {
            // 构造长插件名
            std::string long_name = "VeryLongPluginName_";
            for (int j = 0; j < 50; ++j) {
                long_name += std::to_string(j);
            }
            long_name += "_Thread" + std::to_string(i);

            for (int j = 0; j < 100; ++j) {
                auto plugin = plugin_manager->get_plugin(long_name);
                ASSERT_FALSE(plugin);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    kernel.shutdown();
}

// ========================================
// 混合操作压力测试
// ========================================

TEST(PluginManagerMT, MixedOperationsStress) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto plugin_manager = kernel.plugin_manager();

    std::atomic<bool> stop{false};
    std::atomic<int> total_operations{0};

    // 线程1-4: 快速查询
    std::vector<std::thread> query_threads;
    for (int i = 0; i < 4; ++i) {
        query_threads.emplace_back([plugin_manager, &stop, &total_operations, i]() {
            int counter = 0;
            while (!stop) {
                auto plugin = plugin_manager->get_plugin("StressPlugin" + std::to_string(counter % 10));
                total_operations.fetch_add(1);
                counter++;
                std::this_thread::yield();
            }
        });
    }

    // 线程5-8: 获取所有插件
    std::vector<std::thread> get_all_threads;
    for (int i = 0; i < 4; ++i) {
        get_all_threads.emplace_back([plugin_manager, &stop, &total_operations]() {
            while (!stop) {
                auto plugins = plugin_manager->get_loaded_plugins();
                total_operations.fetch_add(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
    }

    // 运行2秒
    std::this_thread::sleep_for(std::chrono::seconds(2));
    stop = true;

    for (auto& t : query_threads) {
        t.join();
    }
    for (auto& t : get_all_threads) {
        t.join();
    }

    // 验证
    ASSERT_GT(total_operations.load(), 0);

    kernel.shutdown();
}

// ========================================
// 空字符串插件名测试
// ========================================

TEST(PluginManagerMT, EmptyPluginNameQuery) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto plugin_manager = kernel.plugin_manager();

    constexpr int num_threads = 8;

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([plugin_manager]() {
            for (int j = 0; j < 100; ++j) {
                auto plugin = plugin_manager->get_plugin("");
                ASSERT_FALSE(plugin);  // 空名称应该返回 nullptr
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
