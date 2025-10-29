/**
 * @file eventbus_mt_test.cpp
 * @brief EventBus 多线程稳定性和健壮性测试
 *
 * 测试场景：
 * 1. 并发订阅/取消订阅
 * 2. 事件风暴（高频发布）
 * 3. 订阅者异常处理
 * 4. 内存泄漏和资源管理
 * 5. 订阅者生命周期管理
 * 6. 发布-订阅竞态条件
 */

#include <atomic>
#include <chrono>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

#include "../test_framework.h"
#include "corona/kernel/core/kernel_context.h"
#include "corona/kernel/event/i_event_bus.h"

using namespace Corona::Kernel;
using namespace CoronaTest;

// 测试事件类型
struct StressTestEvent {
    int id;
    std::thread::id thread_id;
    std::chrono::steady_clock::time_point timestamp;
};

struct HeavyPayloadEvent {
    std::vector<int> data;
    std::string message;
};

struct ExceptionTriggerEvent {
    bool should_throw;
};

// ========================================
// 并发订阅/取消订阅测试
// ========================================

TEST(EventBusMT, ConcurrentSubscribeUnsubscribe) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto event_bus = kernel.event_bus();
    ASSERT_TRUE(event_bus != nullptr);

    std::atomic<int> subscribe_count{0};
    std::atomic<int> unsubscribe_count{0};
    std::atomic<bool> stop{false};

    constexpr int num_threads = 8;
    std::vector<std::thread> threads;

    // 多个线程同时订阅和取消订阅
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([event_bus, &subscribe_count, &unsubscribe_count, &stop]() {
            std::vector<EventId> handles;
            while (!stop) {
                // 订阅
                auto handle = event_bus->subscribe<StressTestEvent>([](const StressTestEvent&) {
                    // 空处理器
                });
                handles.push_back(handle);
                subscribe_count.fetch_add(1, std::memory_order_relaxed);

                // 随机延迟
                std::this_thread::sleep_for(std::chrono::microseconds(100));

                // 取消订阅
                if (!handles.empty()) {
                    event_bus->unsubscribe(handles.back());
                    handles.pop_back();
                    unsubscribe_count.fetch_add(1, std::memory_order_relaxed);
                }
            }

            // 清理剩余句柄
            for (auto& handle : handles) {
                event_bus->unsubscribe(handle);
            }
        });
    }

    // 运行2秒
    std::this_thread::sleep_for(std::chrono::seconds(2));
    stop = true;

    for (auto& t : threads) {
        t.join();
    }

    // 验证订阅和取消订阅次数
    ASSERT_GT(subscribe_count.load(), 0);
    ASSERT_GT(unsubscribe_count.load(), 0);

    kernel.shutdown();
}

// ========================================
// 事件风暴测试（高频发布）
// ========================================

TEST(EventBusMT, EventStorm) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto event_bus = kernel.event_bus();

    std::atomic<int> published_count{0};
    std::atomic<int> received_count{0};

    // 订阅者
    event_bus->subscribe<StressTestEvent>([&received_count](const StressTestEvent& event) {
        received_count.fetch_add(1, std::memory_order_relaxed);
        // 模拟少量处理时间
        volatile int dummy = 0;
        for (int i = 0; i < 10; ++i) {
            dummy += i;
        }
    });

    constexpr int num_publishers = 10;
    constexpr int events_per_publisher = 1000;
    std::vector<std::thread> publishers;

    auto start_time = std::chrono::steady_clock::now();

    // 多个发布者同时发布大量事件
    for (int i = 0; i < num_publishers; ++i) {
        publishers.emplace_back([event_bus, i, &published_count]() {
            for (int j = 0; j < events_per_publisher; ++j) {
                event_bus->publish(StressTestEvent{
                    j,
                    std::this_thread::get_id(),
                    std::chrono::steady_clock::now()});
                published_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& t : publishers) {
        t.join();
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    // 等待事件处理完成
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 验证所有事件都被发布和接收
    ASSERT_EQ(published_count.load(), num_publishers * events_per_publisher);
    ASSERT_EQ(received_count.load(), num_publishers * events_per_publisher);

    kernel.shutdown();
}

// ========================================
// 订阅者异常处理测试
// ========================================

TEST(EventBusMT, SubscriberExceptionHandling) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto event_bus = kernel.event_bus();

    std::atomic<int> normal_handler_count{0};
    std::atomic<int> exception_count{0};

    // 正常的订阅者
    event_bus->subscribe<ExceptionTriggerEvent>([&normal_handler_count](const ExceptionTriggerEvent&) {
        normal_handler_count.fetch_add(1);
    });

    // 会抛出异常的订阅者
    event_bus->subscribe<ExceptionTriggerEvent>([&exception_count](const ExceptionTriggerEvent& event) {
        if (event.should_throw) {
            exception_count.fetch_add(1);
            throw std::runtime_error("Test exception from subscriber");
        }
    });

    // 另一个正常的订阅者（确保异常不影响其他订阅者）
    std::atomic<int> after_exception_count{0};
    event_bus->subscribe<ExceptionTriggerEvent>([&after_exception_count](const ExceptionTriggerEvent&) {
        after_exception_count.fetch_add(1);
    });

    // 发布多个事件，部分触发异常
    std::vector<std::thread> publishers;
    for (int i = 0; i < 5; ++i) {
        publishers.emplace_back([event_bus, i]() {
            for (int j = 0; j < 100; ++j) {
                event_bus->publish(ExceptionTriggerEvent{j % 3 == 0});  // 每3个事件触发一次异常
            }
        });
    }

    for (auto& t : publishers) {
        t.join();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 验证异常不影响其他订阅者
    ASSERT_EQ(normal_handler_count.load(), 500);  // 5线程 * 100事件
    ASSERT_EQ(after_exception_count.load(), 500);
    ASSERT_GT(exception_count.load(), 0);  // 至少有一些异常被抛出

    kernel.shutdown();
}

// ========================================
// 大负载事件处理测试
// ========================================

TEST(EventBusMT, HeavyPayloadEvents) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto event_bus = kernel.event_bus();

    std::atomic<int> event_count{0};
    std::atomic<size_t> total_data_size{0};

    // 订阅者处理大数据
    event_bus->subscribe<HeavyPayloadEvent>([&event_count, &total_data_size](const HeavyPayloadEvent& event) {
        event_count.fetch_add(1);
        total_data_size.fetch_add(event.data.size(), std::memory_order_relaxed);
    });

    constexpr int num_threads = 4;
    constexpr int events_per_thread = 50;
    std::vector<std::thread> threads;

    // 发布大数据事件
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([event_bus, i]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(1000, 5000);

            for (int j = 0; j < events_per_thread; ++j) {
                HeavyPayloadEvent event;
                event.data.resize(dis(gen));
                std::fill(event.data.begin(), event.data.end(), i * 100 + j);
                event.message = "Heavy event from thread " + std::to_string(i) + " #" + std::to_string(j);
                event_bus->publish(std::move(event));  // 使用移动语义
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    ASSERT_EQ(event_count.load(), num_threads * events_per_thread);
    ASSERT_GT(total_data_size.load(), 0);

    kernel.shutdown();
}

// ========================================
// 订阅者生命周期管理测试
// ========================================

TEST(EventBusMT, SubscriberLifecycleManagement) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto event_bus = kernel.event_bus();

    std::atomic<bool> stop{false};
    std::atomic<int> active_subscribers{0};

    // 发布者线程持续发布事件
    std::thread publisher([event_bus, &stop]() {
        int counter = 0;
        while (!stop) {
            event_bus->publish(StressTestEvent{counter++, std::this_thread::get_id(), std::chrono::steady_clock::now()});
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    // 多个线程动态订阅和取消订阅
    std::vector<std::thread> subscriber_threads;
    for (int i = 0; i < 5; ++i) {
        subscriber_threads.emplace_back([event_bus, &stop, &active_subscribers]() {
            for (int cycle = 0; cycle < 5; ++cycle) {
                auto handle = event_bus->subscribe<StressTestEvent>([&active_subscribers](const StressTestEvent&) {
                    // 处理事件
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                });

                active_subscribers.fetch_add(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                event_bus->unsubscribe(handle);
                active_subscribers.fetch_sub(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });
    }

    for (auto& t : subscriber_threads) {
        t.join();
    }

    stop = true;
    publisher.join();

    // 最终应该没有活跃的订阅者
    ASSERT_EQ(active_subscribers.load(), 0);

    kernel.shutdown();
}

// ========================================
// 发布-订阅竞态条件测试
// ========================================

TEST(EventBusMT, PublishSubscribeRaceCondition) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto event_bus = kernel.event_bus();

    std::atomic<int> event_count{0};
    std::atomic<bool> stop{false};

    // 线程1: 持续发布
    std::thread publisher([event_bus, &stop]() {
        int counter = 0;
        while (!stop) {
            event_bus->publish(StressTestEvent{counter++, std::this_thread::get_id(), std::chrono::steady_clock::now()});
            std::this_thread::yield();
        }
    });

    // 线程2: 持续订阅
    std::thread subscriber([event_bus, &stop, &event_count]() {
        std::vector<EventId> handles;
        while (!stop) {
            auto handle = event_bus->subscribe<StressTestEvent>([&event_count](const StressTestEvent&) {
                event_count.fetch_add(1);
            });
            handles.push_back(handle);
            std::this_thread::yield();

            if (handles.size() > 10) {
                // 保持句柄数量在合理范围
                event_bus->unsubscribe(handles.front());
                handles.erase(handles.begin());
            }
        }

        // 清理
        for (auto& handle : handles) {
            event_bus->unsubscribe(handle);
        }
    });

    // 线程3: 持续取消订阅
    std::thread unsubscriber([event_bus, &stop]() {
        while (!stop) {
            auto handle = event_bus->subscribe<StressTestEvent>([](const StressTestEvent&) {});
            std::this_thread::yield();
            event_bus->unsubscribe(handle);
            std::this_thread::yield();
        }
    });

    // 运行2秒
    std::this_thread::sleep_for(std::chrono::seconds(2));
    stop = true;

    publisher.join();
    subscriber.join();
    unsubscriber.join();

    // 验证没有崩溃
    ASSERT_GT(event_count.load(), 0);

    kernel.shutdown();
}

// ========================================
// 多类型事件并发测试
// ========================================

struct EventType1 {
    int value;
};
struct EventType2 {
    double value;
};
struct EventType3 {
    std::string value;
};

TEST(EventBusMT, MultipleEventTypesConcurrent) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto event_bus = kernel.event_bus();

    std::atomic<int> count1{0}, count2{0}, count3{0};

    // 为不同类型的事件订阅
    event_bus->subscribe<EventType1>([&count1](const EventType1&) { count1.fetch_add(1); });
    event_bus->subscribe<EventType2>([&count2](const EventType2&) { count2.fetch_add(1); });
    event_bus->subscribe<EventType3>([&count3](const EventType3&) { count3.fetch_add(1); });

    std::vector<std::thread> threads;

    // 线程1: 发布 EventType1
    threads.emplace_back([event_bus]() {
        for (int i = 0; i < 1000; ++i) {
            event_bus->publish(EventType1{i});
        }
    });

    // 线程2: 发布 EventType2
    threads.emplace_back([event_bus]() {
        for (int i = 0; i < 1000; ++i) {
            event_bus->publish(EventType2{static_cast<double>(i) * 1.5});
        }
    });

    // 线程3: 发布 EventType3
    threads.emplace_back([event_bus]() {
        for (int i = 0; i < 1000; ++i) {
            event_bus->publish(EventType3{"Event" + std::to_string(i)});
        }
    });

    for (auto& t : threads) {
        t.join();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 验证每种类型的事件都被正确处理
    ASSERT_EQ(count1.load(), 1000);
    ASSERT_EQ(count2.load(), 1000);
    ASSERT_EQ(count3.load(), 1000);

    kernel.shutdown();
}

// ========================================
// 内存压力测试
// ========================================

TEST(EventBusMT, MemoryPressureTest) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto event_bus = kernel.event_bus();

    std::atomic<int> subscription_cycles{0};

    constexpr int num_threads = 4;
    constexpr int cycles_per_thread = 100;

    std::vector<std::thread> threads;

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([event_bus, &subscription_cycles]() {
            for (int cycle = 0; cycle < cycles_per_thread; ++cycle) {
                // 创建大量订阅
                std::vector<EventId> handles;
                for (int j = 0; j < 50; ++j) {
                    auto handle = event_bus->subscribe<StressTestEvent>([](const StressTestEvent&) {
                        // 简单处理
                    });
                    handles.push_back(handle);
                }

                // 发布一些事件
                for (int j = 0; j < 10; ++j) {
                    event_bus->publish(StressTestEvent{j, std::this_thread::get_id(), std::chrono::steady_clock::now()});
                }

                // 取消所有订阅
                for (auto& handle : handles) {
                    event_bus->unsubscribe(handle);
                }

                subscription_cycles.fetch_add(1);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(subscription_cycles.load(), num_threads * cycles_per_thread);

    kernel.shutdown();
}

// ========================================
// Main Function
// ========================================

int main() {
    return CoronaTest::TestRunner::instance().run_all();
}
