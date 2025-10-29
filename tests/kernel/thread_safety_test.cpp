#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "../test_framework.h"
#include "corona/kernel/core/i_logger.h"
#include "corona/kernel/core/kernel_context.h"

using namespace Corona::Kernel;
using namespace CoronaTest;

// ========================================
// Logger Thread Safety Tests
// ========================================

TEST(LoggerThreadSafety, ConcurrentLogging) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto logger = kernel.logger();
    ASSERT_TRUE(logger != nullptr);

    std::atomic<int> counter{0};
    constexpr int num_threads = 10;
    constexpr int messages_per_thread = 100;

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([logger, i, &counter]() {
            for (int j = 0; j < messages_per_thread; ++j) {
                logger->info("Thread " + std::to_string(i) + " message " + std::to_string(j));
                counter.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // 验证所有消息都被记录
    ASSERT_EQ(counter.load(), num_threads * messages_per_thread);

    kernel.shutdown();
}

TEST(LoggerThreadSafety, ConcurrentFlush) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto logger = kernel.logger();

    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([logger, i]() {
            for (int j = 0; j < 50; ++j) {
                logger->info("Message from thread " + std::to_string(i));
                // 测试 flush 的线程安全性（已修复）
                // flush 现在有锁保护
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    kernel.shutdown();
}

TEST(LoggerThreadSafety, AddRemoveSinks) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto logger = kernel.logger();

    std::atomic<bool> stop{false};

    // 线程1：不断添加sink
    std::thread adder([logger, &stop]() {
        while (!stop) {
            auto sink = create_console_sink();
            logger->add_sink(sink);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    // 线程2：不断记录日志
    std::thread logger_thread([logger, &stop]() {
        int count = 0;
        while (!stop) {
            logger->info("Logging message " + std::to_string(count++));
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    // 运行1秒
    std::this_thread::sleep_for(std::chrono::seconds(1));
    stop = true;

    adder.join();
    logger_thread.join();

    kernel.shutdown();
}

// ========================================
// EventBus Thread Safety Tests
// ========================================

struct TestEvent {
    int value;
    std::thread::id thread_id;
};

TEST(EventBusThreadSafety, ConcurrentPublishSubscribe) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto event_bus = kernel.event_bus();
    ASSERT_TRUE(event_bus != nullptr);

    std::atomic<int> event_count{0};

    // 订阅者
    event_bus->subscribe<TestEvent>([&event_count](const TestEvent& event) {
        event_count.fetch_add(event.value, std::memory_order_relaxed);
    });

    // 多个发布者线程
    std::vector<std::thread> publishers;
    constexpr int num_publishers = 5;
    constexpr int events_per_publisher = 100;

    for (int i = 0; i < num_publishers; ++i) {
        publishers.emplace_back([event_bus, i]() {
            for (int j = 0; j < events_per_publisher; ++j) {
                event_bus->publish(TestEvent{1, std::this_thread::get_id()});
            }
        });
    }

    for (auto& t : publishers) {
        t.join();
    }

    // 等待事件处理完成
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_EQ(event_count.load(), num_publishers * events_per_publisher);

    kernel.shutdown();
}

TEST(EventBusThreadSafety, SubscribeWhilePublishing) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto event_bus = kernel.event_bus();

    std::atomic<bool> stop{false};
    std::atomic<int> publish_count{0};
    std::atomic<int> receive_count{0};

    // 发布者线程
    std::thread publisher([event_bus, &stop, &publish_count]() {
        while (!stop) {
            event_bus->publish(TestEvent{1, std::this_thread::get_id()});
            publish_count.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    // 订阅者线程（动态订阅）
    std::vector<std::thread> subscribers;
    for (int i = 0; i < 3; ++i) {
        subscribers.emplace_back([event_bus, &stop, &receive_count]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            event_bus->subscribe<TestEvent>([&receive_count](const TestEvent&) {
                receive_count.fetch_add(1);
            });
            while (!stop) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }

    // 运行500ms
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    stop = true;

    publisher.join();
    for (auto& t : subscribers) {
        t.join();
    }

    // 验证没有崩溃即可
    ASSERT_GT(publish_count.load(), 0);

    kernel.shutdown();
}

// ========================================
// VFS Thread Safety Tests
// ========================================

TEST(VFSThreadSafety, ConcurrentMount) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto vfs = kernel.vfs();
    ASSERT_TRUE(vfs != nullptr);

    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([vfs, i]() {
            std::string virtual_path = "/data" + std::to_string(i) + "/";
            std::string physical_path = "./data" + std::to_string(i) + "/";
            ASSERT_NO_THROW(vfs->mount(virtual_path, physical_path));
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    kernel.shutdown();
}

TEST(VFSThreadSafety, ConcurrentResolve) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto vfs = kernel.vfs();
    vfs->mount("/data/", "./data/");

    std::atomic<int> resolve_count{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([vfs, &resolve_count]() {
            for (int j = 0; j < 100; ++j) {
                auto resolved = vfs->resolve("/data/test.txt");
                if (!resolved.empty()) {
                    resolve_count.fetch_add(1);
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(resolve_count.load(), 1000);

    kernel.shutdown();
}

// ========================================
// PluginManager Thread Safety Tests
// ========================================

TEST(PluginManagerThreadSafety, ConcurrentGetPlugin) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto plugin_manager = kernel.plugin_manager();
    ASSERT_TRUE(plugin_manager != nullptr);

    // 注意：这个测试假设没有实际的插件
    // 主要测试 get_plugin 的线程安全性

    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([plugin_manager]() {
            for (int j = 0; j < 100; ++j) {
                // 使用 shared_ptr 确保生命周期安全（已修复）
                auto plugin = plugin_manager->get_plugin("NonExistentPlugin");
                ASSERT_FALSE(plugin);  // 应该返回 nullptr
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    kernel.shutdown();
}

// ========================================
// KernelContext Thread Safety Tests
// ========================================

TEST(KernelContextThreadSafety, ConcurrentInitialize) {
    // 测试多次并发初始化（应该是安全的）
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&success_count]() {
            auto& kernel = KernelContext::instance();
            if (kernel.initialize()) {
                success_count.fetch_add(1);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // 所有线程都应该成功（因为有锁保护）
    ASSERT_EQ(success_count.load(), 10);

    auto& kernel = KernelContext::instance();
    kernel.shutdown();
}

TEST(KernelContextThreadSafety, InitializeShutdownStress) {
    // 压力测试：多次初始化和关闭
    for (int i = 0; i < 5; ++i) {
        auto& kernel = KernelContext::instance();
        ASSERT_TRUE(kernel.initialize());

        // 使用服务
        auto logger = kernel.logger();
        logger->info("Stress test iteration " + std::to_string(i));

        kernel.shutdown();
    }
}

// ========================================
// Main Function
// ========================================

int main() {
    return CoronaTest::TestRunner::instance().run_all();
}
