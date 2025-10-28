#include "../test_framework.h"
#include "corona/kernel/event/i_event_stream.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <string>
#include <iostream>

using namespace Corona::Kernel;
using namespace std::chrono_literals;

// Test event types
struct SimpleEvent {
    int value = 0;
};

struct ComplexEvent {
    int id;
    std::string data;
};

// ========================================
// Basic Functionality Tests
// ========================================

TEST(EventStream, BasicPublishSubscribe) {
    EventStream<SimpleEvent> stream;
    auto sub = stream.subscribe();

    ASSERT_TRUE(sub.is_valid());
    ASSERT_EQ(stream.subscriber_count(), 1u);

    stream.publish(SimpleEvent{42});

    auto event = sub.try_pop();
    ASSERT_TRUE(event.has_value());
    ASSERT_EQ(event->value, 42);
}

TEST(EventStream, MultipleSubscribers) {
    EventStream<SimpleEvent> stream;
    auto sub1 = stream.subscribe();
    auto sub2 = stream.subscribe();
    auto sub3 = stream.subscribe();

    ASSERT_EQ(stream.subscriber_count(), 3u);

    stream.publish(SimpleEvent{100});

    auto e1 = sub1.try_pop();
    auto e2 = sub2.try_pop();
    auto e3 = sub3.try_pop();

    ASSERT_TRUE(e1.has_value());
    ASSERT_EQ(e1->value, 100);
    
    ASSERT_TRUE(e2.has_value());
    ASSERT_EQ(e2->value, 100);
    
    ASSERT_TRUE(e3.has_value());
    ASSERT_EQ(e3->value, 100);
}

TEST(EventStream, RAIISubscription) {
    EventStream<SimpleEvent> stream;

    {
        auto sub = stream.subscribe();
        ASSERT_EQ(stream.subscriber_count(), 1u);
    }  // sub goes out of scope

    // Give a moment for cleanup
    std::this_thread::sleep_for(1ms);
    
    // Note: subscriber count may still be 1 until next publish
    // This is by design for performance reasons
}

TEST(EventStream, TryPopEmpty) {
    EventStream<SimpleEvent> stream;
    auto sub = stream.subscribe();

    auto event = sub.try_pop();
    ASSERT_FALSE(event.has_value());
}

TEST(EventStream, MultipleEvents) {
    EventStream<SimpleEvent> stream;
    auto sub = stream.subscribe();

    for (int i = 0; i < 10; ++i) {
        stream.publish(SimpleEvent{i});
    }

    for (int i = 0; i < 10; ++i) {
        auto event = sub.try_pop();
        ASSERT_TRUE(event.has_value());
        ASSERT_EQ(event->value, i);
    }

    auto empty_event = sub.try_pop();
    ASSERT_FALSE(empty_event.has_value());
}

// ========================================
// Blocking Tests
// ========================================

TEST(EventStream, WaitForTimeout) {
    EventStream<SimpleEvent> stream;
    auto sub = stream.subscribe();

    auto start = std::chrono::steady_clock::now();
    auto event = sub.wait_for(50ms);
    auto elapsed = std::chrono::steady_clock::now() - start;

    ASSERT_FALSE(event.has_value());
    ASSERT_TRUE(elapsed >= 50ms);
    ASSERT_TRUE(elapsed <= 150ms);  // Allow some margin for scheduling
}

TEST(EventStream, WaitForSuccess) {
    EventStream<SimpleEvent> stream;
    auto sub = stream.subscribe();

    std::thread publisher([&] {
        std::this_thread::sleep_for(20ms);
        stream.publish(SimpleEvent{777});
    });

    auto event = sub.wait_for(1000ms);
    ASSERT_TRUE(event.has_value());
    ASSERT_EQ(event->value, 777);

    publisher.join();
}

TEST(EventStream, WaitIndefinitely) {
    EventStream<SimpleEvent> stream;
    auto sub = stream.subscribe();

    std::atomic<bool> received{false};

    std::thread consumer([&] {
        auto event = sub.wait();
        if (event.has_value() && event->value == 999) {
            received = true;
        }
    });

    std::this_thread::sleep_for(20ms);
    stream.publish(SimpleEvent{999});

    consumer.join();
    ASSERT_TRUE(received.load());
}

// ========================================
// Backpressure Tests
// ========================================

TEST(EventStream, BackpressureDropOldest) {
    EventStreamOptions opts;
    opts.max_queue_size = 3;
    opts.policy = BackpressurePolicy::DropOldest;

    EventStream<SimpleEvent> stream;
    auto sub = stream.subscribe(opts);

    // Fill queue beyond capacity
    for (int i = 0; i < 5; ++i) {
        stream.publish(SimpleEvent{i});
    }

    // Should have last 3 events (2, 3, 4)
    auto e1 = sub.try_pop();
    auto e2 = sub.try_pop();
    auto e3 = sub.try_pop();

    ASSERT_TRUE(e1.has_value());
    ASSERT_TRUE(e2.has_value());
    ASSERT_TRUE(e3.has_value());

    ASSERT_EQ(e1->value, 2);
    ASSERT_EQ(e2->value, 3);
    ASSERT_EQ(e3->value, 4);

    auto empty_event = sub.try_pop();
    ASSERT_FALSE(empty_event.has_value());
}

TEST(EventStream, BackpressureDropNewest) {
    EventStreamOptions opts;
    opts.max_queue_size = 3;
    opts.policy = BackpressurePolicy::DropNewest;

    EventStream<SimpleEvent> stream;
    auto sub = stream.subscribe(opts);

    // Fill queue beyond capacity
    for (int i = 0; i < 5; ++i) {
        stream.publish(SimpleEvent{i});
    }

    // Should have first 3 events (0, 1, 2)
    auto e1 = sub.try_pop();
    auto e2 = sub.try_pop();
    auto e3 = sub.try_pop();

    ASSERT_TRUE(e1.has_value());
    ASSERT_TRUE(e2.has_value());
    ASSERT_TRUE(e3.has_value());

    ASSERT_EQ(e1->value, 0);
    ASSERT_EQ(e2->value, 1);
    ASSERT_EQ(e3->value, 2);

    auto empty_event = sub.try_pop();
    ASSERT_FALSE(empty_event.has_value());
}

TEST(EventStream, BackpressureBlock) {
    EventStreamOptions opts;
    opts.max_queue_size = 2;
    opts.policy = BackpressurePolicy::Block;

    EventStream<SimpleEvent> stream;
    auto sub = stream.subscribe(opts);

    std::atomic<int> published_count{0};
    std::atomic<bool> started_blocking{false};

    // Publisher thread
    std::thread publisher([&] {
        for (int i = 0; i < 5; ++i) {
            if (i == 2) {
                started_blocking = true;  // Mark that we're about to block
            }
            stream.publish(SimpleEvent{i});
            published_count++;
        }
    });

    // Let publisher fill the queue
    std::this_thread::sleep_for(50ms);

    // Publisher should be blocked after filling queue
    ASSERT_TRUE(started_blocking.load());
    int count_when_blocked = published_count.load();
    ASSERT_TRUE(count_when_blocked <= 3);  // Should have blocked around the 3rd event

    // Consume events to unblock
    for (int i = 0; i < 5; ++i) {
        auto event = sub.wait_for(1000ms);
        ASSERT_TRUE(event.has_value());
        ASSERT_EQ(event->value, i);
    }

    publisher.join();
    ASSERT_EQ(published_count.load(), 5);
}

// ========================================
// Thread Safety Tests
// ========================================

TEST(EventStream, ConcurrentPublish) {
    EventStream<SimpleEvent> stream;
    auto sub = stream.subscribe({.max_queue_size = 1000});  // Increase queue size

    constexpr int kThreads = 8;
    constexpr int kEventsPerThread = 100;

    std::vector<std::thread> publishers;
    for (int t = 0; t < kThreads; ++t) {
        publishers.emplace_back([&, t] {
            for (int i = 0; i < kEventsPerThread; ++i) {
                stream.publish(SimpleEvent{t * 1000 + i});
            }
        });
    }

    for (auto& thread : publishers) {
        thread.join();
    }

    // Should receive all events
    int count = 0;
    while (auto event = sub.try_pop()) {
        count++;
    }

    ASSERT_EQ(count, kThreads * kEventsPerThread);
}

TEST(EventStream, ConcurrentSubscribe) {
    EventStream<SimpleEvent> stream;

    constexpr int kThreads = 8;
    std::vector<EventSubscription<SimpleEvent>> subscriptions;
    subscriptions.reserve(kThreads);

    std::mutex sub_mutex;
    std::vector<std::thread> subscribers;

    for (int t = 0; t < kThreads; ++t) {
        subscribers.emplace_back([&] {
            auto sub = stream.subscribe();
            std::lock_guard lock(sub_mutex);
            subscriptions.push_back(std::move(sub));
        });
    }

    for (auto& thread : subscribers) {
        thread.join();
    }

    ASSERT_EQ(stream.subscriber_count(), static_cast<size_t>(kThreads));
}

TEST(EventStream, PublishWhileConsuming) {
    EventStream<SimpleEvent> stream;
    auto sub = stream.subscribe({.max_queue_size = 2000});  // Increase queue size

    std::atomic<bool> stop{false};
    std::atomic<int> consumed{0};

    // Consumer thread
    std::thread consumer([&] {
        while (!stop.load()) {
            if (auto event = sub.try_pop()) {
                consumed++;
            }
            std::this_thread::yield();
        }
        
        // Consume any remaining events
        while (auto event = sub.try_pop()) {
            consumed++;
        }
    });

    // Publisher thread
    std::thread publisher([&] {
        for (int i = 0; i < 1000; ++i) {
            stream.publish(SimpleEvent{i});
            if (i % 100 == 0) {
                std::this_thread::sleep_for(1ms);
            }
        }
    });

    publisher.join();
    stop = true;
    consumer.join();

    ASSERT_EQ(consumed.load(), 1000);
}

// ========================================
// Complex Event Tests
// ========================================

TEST(EventStream, ComplexEventType) {
    EventStream<ComplexEvent> stream;
    auto sub = stream.subscribe();

    stream.publish(ComplexEvent{1, "Hello"});
    stream.publish(ComplexEvent{2, "World"});

    auto e1 = sub.try_pop();
    auto e2 = sub.try_pop();

    ASSERT_TRUE(e1.has_value());
    ASSERT_EQ(e1->id, 1);
    ASSERT_EQ(e1->data, "Hello");

    ASSERT_TRUE(e2.has_value());
    ASSERT_EQ(e2->id, 2);
    ASSERT_EQ(e2->data, "World");
}

TEST(EventStream, MoveSemantics) {
    EventStream<ComplexEvent> stream;
    auto sub = stream.subscribe();

    ComplexEvent event{42, "Move me"};
    stream.publish(std::move(event));

    auto received = sub.try_pop();
    ASSERT_TRUE(received.has_value());
    ASSERT_EQ(received->id, 42);
    ASSERT_EQ(received->data, "Move me");
}

// ========================================
// Close Subscription Tests
// ========================================

TEST(EventStream, CloseSubscription) {
    EventStream<SimpleEvent> stream;
    auto sub = stream.subscribe();

    stream.publish(SimpleEvent{1});
    sub.close();

    // After close, try_pop should return nullopt
    auto event = sub.try_pop();
    ASSERT_FALSE(event.has_value());
    ASSERT_FALSE(sub.is_valid());
}

TEST(EventStream, WaitAfterClose) {
    EventStream<SimpleEvent> stream;
    auto sub = stream.subscribe();

    std::thread closer([&] {
        std::this_thread::sleep_for(50ms);
        sub.close();
    });

    // wait() should return nullopt after close
    auto event = sub.wait();
    ASSERT_FALSE(event.has_value());

    closer.join();
}

// ========================================
// Performance Tests
// ========================================

TEST(EventStream, HighThroughput) {
    EventStream<SimpleEvent> stream;
    auto sub = stream.subscribe(EventStreamOptions{.max_queue_size = 100000});

    constexpr int kEventCount = 100000;

    auto start = std::chrono::steady_clock::now();

    // Publish
    for (int i = 0; i < kEventCount; ++i) {
        stream.publish(SimpleEvent{i});
    }

    // Consume
    int consumed = 0;
    while (auto event = sub.try_pop()) {
        consumed++;
    }

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    ASSERT_EQ(consumed, kEventCount);
    ASSERT_LT(ms, 2000);  // Should complete in under 2 seconds

    std::cout << "Published and consumed " << kEventCount << " events in " << ms << "ms\n";
    std::cout << "Throughput: " << (ms > 0 ? (kEventCount * 1000 / ms) : kEventCount) << " events/sec\n";
}

// ========================================
// Boundary and Error Handling Tests
// ========================================

TEST(EventStream, ZeroMaxQueueSize) {
    EventStreamOptions opts;
    opts.max_queue_size = 0;
    opts.policy = BackpressurePolicy::DropNewest;

    EventStream<SimpleEvent> stream;
    auto sub = stream.subscribe(opts);

    // Implementation automatically adjusts 0 to 1 for safety
    // Should not crash with zero queue size
    stream.publish(SimpleEvent{100});

    auto event = sub.try_pop();
    ASSERT_TRUE(event.has_value());  // Zero gets adjusted to 1
    ASSERT_EQ(event->value, 100);
}

TEST(EventStream, VerySmallQueueSize) {
    EventStreamOptions opts;
    opts.max_queue_size = 1;
    opts.policy = BackpressurePolicy::DropOldest;

    EventStream<SimpleEvent> stream;
    auto sub = stream.subscribe(opts);

    stream.publish(SimpleEvent{1});
    stream.publish(SimpleEvent{2});
    stream.publish(SimpleEvent{3});

    // Should only have the latest event
    auto event = sub.try_pop();
    ASSERT_TRUE(event.has_value());
    ASSERT_EQ(event->value, 3);

    auto empty_event = sub.try_pop();
    ASSERT_FALSE(empty_event.has_value());
}

TEST(EventStream, VeryLargeQueueSize) {
    EventStreamOptions opts;
    opts.max_queue_size = 1000000;  // 1 million

    EventStream<SimpleEvent> stream;
    auto sub = stream.subscribe(opts);

    // Should not crash with very large queue size
    for (int i = 0; i < 100; ++i) {
        stream.publish(SimpleEvent{i});
    }

    int count = 0;
    while (auto event = sub.try_pop()) {
        count++;
    }

    ASSERT_EQ(count, 100);
}

TEST(EventStream, PublishToNoSubscribers) {
    EventStream<SimpleEvent> stream;

    // Should not crash when publishing to stream with no subscribers
    stream.publish(SimpleEvent{999});

    ASSERT_EQ(stream.subscriber_count(), 0u);
}

TEST(EventStream, MultipleCloseCalls) {
    EventStream<SimpleEvent> stream;
    auto sub = stream.subscribe();

    // First close
    sub.close();
    ASSERT_FALSE(sub.is_valid());

    // Second close should not crash
    sub.close();
    ASSERT_FALSE(sub.is_valid());

    // Third close
    sub.close();
    ASSERT_FALSE(sub.is_valid());
}

TEST(EventStream, WaitForZeroTimeout) {
    EventStream<SimpleEvent> stream;
    auto sub = stream.subscribe();

    auto start = std::chrono::steady_clock::now();
    auto event = sub.wait_for(0ms);
    auto elapsed = std::chrono::steady_clock::now() - start;

    ASSERT_FALSE(event.has_value());
    ASSERT_LT(elapsed, 50ms);  // Should return immediately
}

TEST(EventStream, WaitForNegativeTimeout) {
    EventStream<SimpleEvent> stream;
    auto sub = stream.subscribe();

    // Negative timeout should behave like zero timeout
    auto start = std::chrono::steady_clock::now();
    auto event = sub.wait_for(std::chrono::milliseconds(-100));
    auto elapsed = std::chrono::steady_clock::now() - start;

    ASSERT_FALSE(event.has_value());
    ASSERT_LT(elapsed, 50ms);  // Should return immediately
}

TEST(EventStream, RapidSubscribeUnsubscribe) {
    EventStream<SimpleEvent> stream;

    // Create and destroy many subscriptions rapidly
    for (int i = 0; i < 100; ++i) {
        auto sub = stream.subscribe();
        ASSERT_TRUE(sub.is_valid());
    }  // sub destroyed

    std::this_thread::sleep_for(10ms);
    
    // Should still work after many subscribe/unsubscribe cycles
    auto sub = stream.subscribe();
    stream.publish(SimpleEvent{777});

    auto event = sub.try_pop();
    ASSERT_TRUE(event.has_value());
    ASSERT_EQ(event->value, 777);
}

TEST(EventStream, LargeEventData) {
    struct LargeEvent {
        std::vector<int> data;
    };

    EventStream<LargeEvent> stream;
    auto sub = stream.subscribe();

    // Create a large event (10KB)
    LargeEvent large_event;
    large_event.data.resize(2560, 42);  // 10KB of integers

    stream.publish(large_event);

    auto event = sub.try_pop();
    ASSERT_TRUE(event.has_value());
    ASSERT_EQ(event->data.size(), 2560u);
    ASSERT_EQ(event->data[0], 42);
    ASSERT_EQ(event->data[2559], 42);
}

TEST(EventStream, MixedBackpressurePolicies) {
    EventStream<SimpleEvent> stream;

    // Create subscriptions with different policies
    auto sub1 = stream.subscribe({.max_queue_size = 3, .policy = BackpressurePolicy::DropOldest});
    auto sub2 = stream.subscribe({.max_queue_size = 3, .policy = BackpressurePolicy::DropNewest});

    // Publish 5 events
    for (int i = 0; i < 5; ++i) {
        stream.publish(SimpleEvent{i});
    }

    // sub1 (DropOldest) should have events 2, 3, 4
    auto e1_1 = sub1.try_pop();
    auto e1_2 = sub1.try_pop();
    auto e1_3 = sub1.try_pop();

    ASSERT_TRUE(e1_1.has_value());
    ASSERT_TRUE(e1_2.has_value());
    ASSERT_TRUE(e1_3.has_value());
    ASSERT_EQ(e1_1->value, 2);
    ASSERT_EQ(e1_2->value, 3);
    ASSERT_EQ(e1_3->value, 4);

    // sub2 (DropNewest) should have events 0, 1, 2
    auto e2_1 = sub2.try_pop();
    auto e2_2 = sub2.try_pop();
    auto e2_3 = sub2.try_pop();

    ASSERT_TRUE(e2_1.has_value());
    ASSERT_TRUE(e2_2.has_value());
    ASSERT_TRUE(e2_3.has_value());
    ASSERT_EQ(e2_1->value, 0);
    ASSERT_EQ(e2_2->value, 1);
    ASSERT_EQ(e2_3->value, 2);
}

TEST(EventStream, PublishAfterAllSubscribersClose) {
    EventStream<SimpleEvent> stream;

    {
        auto sub1 = stream.subscribe();
        auto sub2 = stream.subscribe();
        
        stream.publish(SimpleEvent{100});
        
        sub1.close();
        sub2.close();
    }

    std::this_thread::sleep_for(10ms);

    // Publishing after all subscribers close should not crash
    stream.publish(SimpleEvent{200});
    
    // New subscriber should not receive old events
    auto new_sub = stream.subscribe();
    auto event = new_sub.try_pop();
    ASSERT_FALSE(event.has_value());
}

TEST(EventStream, ConcurrentCloseAndPublish) {
    EventStream<SimpleEvent> stream;
    auto sub = stream.subscribe({.max_queue_size = 1000});

    std::atomic<bool> stop{false};

    // Publisher thread
    std::thread publisher([&] {
        for (int i = 0; i < 1000 && !stop.load(); ++i) {
            stream.publish(SimpleEvent{i});
            if (i % 50 == 0) {
                std::this_thread::sleep_for(1ms);
            }
        }
    });

    std::this_thread::sleep_for(100ms);

    // Close subscription while publishing
    sub.close();
    stop = true;

    publisher.join();

    // Should not crash
    ASSERT_FALSE(sub.is_valid());
}

TEST(EventStream, WaitInterruptedByClose) {
    EventStream<SimpleEvent> stream;
    auto sub = stream.subscribe();

    std::atomic<bool> wait_returned{false};

    // Thread waiting indefinitely
    std::thread waiter([&] {
        auto event = sub.wait();
        wait_returned = true;
        ASSERT_FALSE(event.has_value());
    });

    std::this_thread::sleep_for(50ms);

    // Close should interrupt the wait
    sub.close();

    waiter.join();
    ASSERT_TRUE(wait_returned.load());
}

int main() {
    return CoronaTest::TestRunner::instance().run_all();
}
