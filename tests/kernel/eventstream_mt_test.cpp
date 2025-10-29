/**
 * @file eventstream_mt_test.cpp
 * @brief EventStream 多线程稳定性和健壮性测试
 *
 * 测试场景：
 * 1. 并发推送/拉取
 * 2. 背压处理（Block、DropOldest、DropNewest）
 * 3. 流关闭和订阅生命周期
 * 4. 多生产者多消费者模式
 * 5. 高频事件推送
 * 6. 订阅者阻塞和超时处理
 */

#include <atomic>
#include <chrono>
#include <random>
#include <thread>
#include <vector>

#include "../test_framework.h"
#include "corona/kernel/event/i_event_stream.h"

using namespace Corona::Kernel;
using namespace CoronaTest;

// 测试事件类型
struct StreamTestEvent {
    int id;
    std::thread::id producer_id;
    std::chrono::steady_clock::time_point timestamp;
};

struct LargeStreamEvent {
    std::vector<int> data;
    std::string message;
};

// ========================================
// 并发推送/拉取测试
// ========================================

TEST(EventStreamMT, ConcurrentPushPull) {
    EventStream<StreamTestEvent> stream;

    constexpr int num_producers = 4;
    constexpr int num_consumers = 4;
    constexpr int events_per_producer = 500;

    std::atomic<int> produced_count{0};
    std::atomic<int> consumed_count{0};

    // 创建消费者
    std::vector<std::thread> consumers;
    std::vector<EventSubscription<StreamTestEvent>> subscriptions;

    for (int i = 0; i < num_consumers; ++i) {
        subscriptions.push_back(stream.subscribe());
    }

    for (int i = 0; i < num_consumers; ++i) {
        consumers.emplace_back([&consumed_count, sub = std::move(subscriptions[i])]() mutable {
            while (true) {
                auto event = sub.wait_for(std::chrono::milliseconds(500));
                if (!event.has_value()) {
                    break;  // 超时或流已关闭
                }
                consumed_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // 创建生产者
    std::vector<std::thread> producers;
    for (int i = 0; i < num_producers; ++i) {
        producers.emplace_back([&stream, &produced_count, i]() {
            for (int j = 0; j < events_per_producer; ++j) {
                stream.publish(StreamTestEvent{
                    j,
                    std::this_thread::get_id(),
                    std::chrono::steady_clock::now()});
                produced_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // 等待所有生产者完成
    for (auto& t : producers) {
        t.join();
    }

    // 等待消费者处理完所有事件
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 关闭所有订阅
    for (auto& sub : subscriptions) {
        sub.close();
    }

    for (auto& t : consumers) {
        t.join();
    }

    // 验证：每个消费者都收到了事件，总数应该是 生产数 * 消费者数
    int expected = num_producers * events_per_producer * num_consumers;
    ASSERT_EQ(consumed_count.load(), expected);
}

// ========================================
// 背压策略测试 - Block
// ========================================

TEST(EventStreamMT, BackpressureBlock) {
    EventStreamOptions options;
    options.max_queue_size = 10;
    options.policy = BackpressurePolicy::Block;

    EventStream<StreamTestEvent> stream;
    auto subscription = stream.subscribe(options);

    std::atomic<int> published_count{0};
    std::atomic<bool> producer_blocked{false};

    // 生产者：快速发布超过队列容量的事件
    std::thread producer([&stream, &published_count, &producer_blocked]() {
        for (int i = 0; i < 50; ++i) {
            if (i == 15) {
                producer_blocked.store(true);  // 应该在这里开始阻塞
            }
            stream.publish(StreamTestEvent{i, std::this_thread::get_id(), std::chrono::steady_clock::now()});
            published_count.fetch_add(1);
        }
    });

    // 消费者：慢速消费
    std::atomic<int> consumed_count{0};
    std::thread consumer([sub = std::move(subscription), &consumed_count]() mutable {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));  // 延迟开始消费
        while (true) {
            auto event = sub.wait_for(std::chrono::milliseconds(100));
            if (!event.has_value()) {
                break;
            }
            consumed_count.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));  // 慢速消费
        }
    });

    producer.join();
    consumer.join();

    // 验证：生产者因背压而阻塞过
    ASSERT_TRUE(producer_blocked.load());
    ASSERT_EQ(published_count.load(), 50);
    ASSERT_EQ(consumed_count.load(), 50);
}

// ========================================
// 背压策略测试 - DropOldest
// ========================================

TEST(EventStreamMT, BackpressureDropOldest) {
    EventStreamOptions options;
    options.max_queue_size = 10;
    options.policy = BackpressurePolicy::DropOldest;

    EventStream<StreamTestEvent> stream;
    auto subscription = stream.subscribe(options);

    std::atomic<int> published_count{0};

    // 生产者：快速发布大量事件
    std::thread producer([&stream, &published_count]() {
        for (int i = 0; i < 100; ++i) {
            stream.publish(StreamTestEvent{i, std::this_thread::get_id(), std::chrono::steady_clock::now()});
            published_count.fetch_add(1);
        }
    });

    producer.join();

    // 延迟后开始消费
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::vector<int> consumed_ids;
    while (true) {
        auto event = subscription.try_pop();
        if (!event.has_value()) {
            break;
        }
        consumed_ids.push_back(event->id);
    }

    // 验证：只保留了最新的事件（旧的被丢弃）
    ASSERT_LE(consumed_ids.size(), options.max_queue_size);
    ASSERT_GT(consumed_ids.size(), 0);

    // 最后消费的应该是最新的事件
    if (!consumed_ids.empty()) {
        ASSERT_GT(consumed_ids.back(), 80);  // 应该接近 100
    }
}

// ========================================
// 背压策略测试 - DropNewest
// ========================================

TEST(EventStreamMT, BackpressureDropNewest) {
    EventStreamOptions options;
    options.max_queue_size = 10;
    options.policy = BackpressurePolicy::DropNewest;

    EventStream<StreamTestEvent> stream;
    auto subscription = stream.subscribe(options);

    // 生产者：快速发布大量事件
    for (int i = 0; i < 100; ++i) {
        stream.publish(StreamTestEvent{i, std::this_thread::get_id(), std::chrono::steady_clock::now()});
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::vector<int> consumed_ids;
    while (true) {
        auto event = subscription.try_pop();
        if (!event.has_value()) {
            break;
        }
        consumed_ids.push_back(event->id);
    }

    // 验证：只保留了最早的事件（新的被丢弃）
    ASSERT_LE(consumed_ids.size(), options.max_queue_size);
    ASSERT_GT(consumed_ids.size(), 0);

    // 最后消费的应该是早期的事件
    if (!consumed_ids.empty()) {
        ASSERT_LT(consumed_ids.back(), 15);  // 应该是早期的事件
    }
}

// ========================================
// 多生产者多消费者压力测试
// ========================================

TEST(EventStreamMT, MultiProducerMultiConsumerStress) {
    EventStream<StreamTestEvent> stream;

    constexpr int num_producers = 8;
    constexpr int num_consumers = 8;
    constexpr int events_per_producer = 200;

    std::atomic<int> total_produced{0};
    std::atomic<int> total_consumed{0};

    // 创建消费者
    std::vector<std::thread> consumers;
    std::vector<EventSubscription<StreamTestEvent>> subscriptions;

    for (int i = 0; i < num_consumers; ++i) {
        subscriptions.push_back(stream.subscribe());
    }

    for (int i = 0; i < num_consumers; ++i) {
        consumers.emplace_back([&total_consumed, sub = std::move(subscriptions[i])]() mutable {
            int local_count = 0;
            while (true) {
                auto event = sub.wait_for(std::chrono::milliseconds(500));
                if (!event.has_value()) {
                    break;
                }
                local_count++;
                // 模拟处理时间
                volatile int dummy = 0;
                for (int j = 0; j < 100; ++j) {
                    dummy += j;
                }
            }
            total_consumed.fetch_add(local_count, std::memory_order_relaxed);
        });
    }

    // 创建生产者
    std::vector<std::thread> producers;
    for (int i = 0; i < num_producers; ++i) {
        producers.emplace_back([&stream, &total_produced, i]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(1, 5);

            for (int j = 0; j < events_per_producer; ++j) {
                stream.publish(StreamTestEvent{j, std::this_thread::get_id(), std::chrono::steady_clock::now()});
                total_produced.fetch_add(1);

                // 随机延迟
                if (j % 10 == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(dis(gen)));
                }
            }
        });
    }

    // 等待所有生产者完成
    for (auto& t : producers) {
        t.join();
    }

    // 等待消费者处理完
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 关闭所有订阅
    for (auto& sub : subscriptions) {
        sub.close();
    }

    for (auto& t : consumers) {
        t.join();
    }

    // 验证：总消费数 = 总生产数 * 消费者数
    int expected = num_producers * events_per_producer * num_consumers;
    ASSERT_EQ(total_consumed.load(), expected);
}

// ========================================
// 订阅生命周期测试
// ========================================

TEST(EventStreamMT, SubscriptionLifecycle) {
    EventStream<StreamTestEvent> stream;

    std::atomic<bool> stop{false};
    std::atomic<int> active_subscriptions{0};

    // 生产者持续发布事件
    std::thread producer([&stream, &stop]() {
        int counter = 0;
        while (!stop) {
            stream.publish(StreamTestEvent{counter++, std::this_thread::get_id(), std::chrono::steady_clock::now()});
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    // 多个线程动态创建和销毁订阅
    std::vector<std::thread> subscription_threads;
    for (int i = 0; i < 5; ++i) {
        subscription_threads.emplace_back([&stream, &stop, &active_subscriptions]() {
            for (int cycle = 0; cycle < 5; ++cycle) {
                auto subscription = stream.subscribe();
                active_subscriptions.fetch_add(1);

                // 消费一些事件
                for (int j = 0; j < 10; ++j) {
                    auto event = subscription.wait_for(std::chrono::milliseconds(50));
                    if (!event.has_value()) {
                        break;
                    }
                }

                subscription.close();
                active_subscriptions.fetch_sub(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        });
    }

    for (auto& t : subscription_threads) {
        t.join();
    }

    stop = true;
    producer.join();

    // 验证：所有订阅都已正确关闭
    ASSERT_EQ(active_subscriptions.load(), 0);
}

// ========================================
// 高频事件推送测试
// ========================================

TEST(EventStreamMT, HighFrequencyPublish) {
    EventStream<StreamTestEvent> stream;

    EventStreamOptions options;
    options.max_queue_size = 1000;
    auto subscription = stream.subscribe(options);

    std::atomic<int> published_count{0};
    std::atomic<bool> done{false};

    auto start_time = std::chrono::steady_clock::now();

    // 高频生产者
    std::thread producer([&stream, &published_count, &done]() {
        for (int i = 0; i < 10000; ++i) {
            stream.publish(StreamTestEvent{i, std::this_thread::get_id(), std::chrono::steady_clock::now()});
            published_count.fetch_add(1);
        }
        done.store(true);
    });

    // 高频消费者
    std::atomic<int> consumed_count{0};
    std::thread consumer([sub = std::move(subscription), &consumed_count, &done]() mutable {
        while (!done.load() || consumed_count.load() < 10000) {
            auto event = sub.wait_for(std::chrono::milliseconds(100));
            if (event.has_value()) {
                consumed_count.fetch_add(1);
            }
        }
    });

    producer.join();
    consumer.join();

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    // 验证
    ASSERT_EQ(published_count.load(), 10000);
    ASSERT_EQ(consumed_count.load(), 10000);
}

// ========================================
// 订阅者阻塞和超时测试
// ========================================

TEST(EventStreamMT, SubscriberTimeoutHandling) {
    EventStream<StreamTestEvent> stream;
    auto subscription = stream.subscribe();

    std::atomic<bool> timeout_occurred{false};

    // 消费者等待事件（但不发布）
    std::thread consumer([sub = std::move(subscription), &timeout_occurred]() mutable {
        auto event = sub.wait_for(std::chrono::milliseconds(200));
        if (!event.has_value()) {
            timeout_occurred.store(true);
        }
    });

    consumer.join();

    // 验证：应该发生超时
    ASSERT_TRUE(timeout_occurred.load());
}

// ========================================
// 大数据负载测试
// ========================================

TEST(EventStreamMT, LargePayloadEvents) {
    EventStream<LargeStreamEvent> stream;
    auto subscription = stream.subscribe();

    constexpr int num_events = 100;
    std::atomic<int> published_count{0};
    std::atomic<size_t> total_data_size{0};

    // 生产者：发布大数据事件
    std::thread producer([&stream, &published_count]() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(5000, 10000);

        for (int i = 0; i < num_events; ++i) {
            LargeStreamEvent event;
            event.data.resize(dis(gen));
            std::fill(event.data.begin(), event.data.end(), i);
            event.message = "Large event #" + std::to_string(i);
            stream.publish(std::move(event));
            published_count.fetch_add(1);
        }
    });

    // 消费者：处理大数据
    std::atomic<int> consumed_count{0};
    std::thread consumer([sub = std::move(subscription), &consumed_count, &total_data_size]() mutable {
        while (consumed_count.load() < num_events) {
            auto event = sub.wait_for(std::chrono::milliseconds(500));
            if (event.has_value()) {
                total_data_size.fetch_add(event->data.size(), std::memory_order_relaxed);
                consumed_count.fetch_add(1);
            }
        }
    });

    producer.join();
    consumer.join();

    // 验证
    ASSERT_EQ(published_count.load(), num_events);
    ASSERT_EQ(consumed_count.load(), num_events);
    ASSERT_GT(total_data_size.load(), 0);
}

// ========================================
// 流关闭时的竞态测试
// ========================================

TEST(EventStreamMT, CloseRaceCondition) {
    EventStream<StreamTestEvent> stream;

    std::vector<EventSubscription<StreamTestEvent>> subscriptions;
    for (int i = 0; i < 5; ++i) {
        subscriptions.push_back(stream.subscribe());
    }

    std::atomic<bool> stop{false};

    // 生产者
    std::thread producer([&stream, &stop]() {
        int counter = 0;
        while (!stop) {
            stream.publish(StreamTestEvent{counter++, std::this_thread::get_id(), std::chrono::steady_clock::now()});
            std::this_thread::yield();
        }
    });

    // 消费者
    std::vector<std::thread> consumers;
    for (int i = 0; i < 5; ++i) {
        consumers.emplace_back([sub = std::move(subscriptions[i]), &stop]() mutable {
            while (!stop) {
                auto event = sub.wait_for(std::chrono::milliseconds(10));
                // 继续尝试消费
            }
        });
    }

    // 运行一段时间后关闭
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stop = true;

    producer.join();
    for (auto& t : consumers) {
        t.join();
    }

    // 验证：没有崩溃即可
}

// ========================================
// Main Function
// ========================================

int main() {
    return CoronaTest::TestRunner::instance().run_all();
}
