#include "../test_framework.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "corona/kernel/event/i_event_bus.h"

using namespace Corona::Kernel;
using namespace CoronaTest;

// Test event types
struct SimpleEvent {
    int value = 0;
};

struct StringEvent {
    std::string message;
};

struct ComplexEvent {
    int id;
    std::string name;
    double timestamp;
};

struct EmptyEvent {};

// Factory function declaration (from event_bus.cpp)
namespace Corona::Kernel {
std::unique_ptr<IEventBus> create_event_bus();
}

// ========================================
// Basic Functionality Tests
// ========================================

TEST(EventBus, CreateEventBus) {
    auto bus = create_event_bus();
    ASSERT_TRUE(bus != nullptr);
}

TEST(EventBus, BasicSubscribePublish) {
    auto bus = create_event_bus();
    
    int received_value = 0;
    auto id = bus->subscribe<SimpleEvent>([&](const SimpleEvent& event) {
        received_value = event.value;
    });
    
    // EventId 0 is valid (starts from 0)
    // Just check it's a valid subscription
    
    bus->publish(SimpleEvent{42});
    ASSERT_EQ(received_value, 42);
}

TEST(EventBus, MultipleSubscribers) {
    auto bus = create_event_bus();
    
    int count = 0;
    std::vector<int> received_values;
    
    auto id1 = bus->subscribe<SimpleEvent>([&](const SimpleEvent& event) {
        received_values.push_back(event.value);
        count++;
    });
    
    auto id2 = bus->subscribe<SimpleEvent>([&](const SimpleEvent& event) {
        received_values.push_back(event.value * 2);
        count++;
    });
    
    auto id3 = bus->subscribe<SimpleEvent>([&](const SimpleEvent& event) {
        received_values.push_back(event.value * 3);
        count++;
    });
    
    bus->publish(SimpleEvent{10});
    
    ASSERT_EQ(count, 3);
    ASSERT_EQ(received_values.size(), 3u);
    // Handlers are called in subscription order
    ASSERT_EQ(received_values[0], 10);
    ASSERT_EQ(received_values[1], 20);
    ASSERT_EQ(received_values[2], 30);
}

TEST(EventBus, UnsubscribeHandler) {
    auto bus = create_event_bus();
    
    int call_count = 0;
    
    auto id = bus->subscribe<SimpleEvent>([&](const SimpleEvent&) {
        call_count++;
    });
    
    bus->publish(SimpleEvent{1});
    ASSERT_EQ(call_count, 1);
    
    bus->unsubscribe(id);
    
    bus->publish(SimpleEvent{2});
    ASSERT_EQ(call_count, 1);  // Should not increase
}

TEST(EventBus, UnsubscribeNonExistentId) {
    auto bus = create_event_bus();
    
    // Should not crash
    bus->unsubscribe(999999);
}

TEST(EventBus, PublishWithoutSubscribers) {
    auto bus = create_event_bus();
    
    // Should not crash when no one is listening
    bus->publish(SimpleEvent{100});
}

// ========================================
// Multiple Event Types Tests
// ========================================

TEST(EventBus, DifferentEventTypes) {
    auto bus = create_event_bus();
    
    int simple_value = 0;
    std::string string_value;
    
    bus->subscribe<SimpleEvent>([&](const SimpleEvent& event) {
        simple_value = event.value;
    });
    
    bus->subscribe<StringEvent>([&](const StringEvent& event) {
        string_value = event.message;
    });
    
    bus->publish(SimpleEvent{123});
    bus->publish(StringEvent{"Hello EventBus"});
    
    ASSERT_EQ(simple_value, 123);
    ASSERT_EQ(string_value, "Hello EventBus");
}

TEST(EventBus, ComplexEventType) {
    auto bus = create_event_bus();
    
    ComplexEvent received;
    bool handler_called = false;
    
    bus->subscribe<ComplexEvent>([&](const ComplexEvent& event) {
        received = event;
        handler_called = true;
    });
    
    ComplexEvent sent{42, "TestEvent", 123.456};
    bus->publish(sent);
    
    ASSERT_TRUE(handler_called);
    ASSERT_EQ(received.id, 42);
    ASSERT_EQ(received.name, "TestEvent");
    ASSERT_EQ(received.timestamp, 123.456);
}

TEST(EventBus, EmptyEvent) {
    auto bus = create_event_bus();
    
    bool handler_called = false;
    
    bus->subscribe<EmptyEvent>([&](const EmptyEvent&) {
        handler_called = true;
    });
    
    bus->publish(EmptyEvent{});
    
    ASSERT_TRUE(handler_called);
}

// ========================================
// Subscription Management Tests
// ========================================

TEST(EventBus, MultipleSubscriptionsReturnDifferentIds) {
    auto bus = create_event_bus();
    
    auto id1 = bus->subscribe<SimpleEvent>([](const SimpleEvent&) {});
    auto id2 = bus->subscribe<SimpleEvent>([](const SimpleEvent&) {});
    auto id3 = bus->subscribe<SimpleEvent>([](const SimpleEvent&) {});
    
    ASSERT_TRUE(id1 != id2);
    ASSERT_TRUE(id2 != id3);
    ASSERT_TRUE(id1 != id3);
}

TEST(EventBus, UnsubscribeOneOfMany) {
    auto bus = create_event_bus();
    
    int count1 = 0, count2 = 0, count3 = 0;
    
    auto id1 = bus->subscribe<SimpleEvent>([&](const SimpleEvent&) { count1++; });
    auto id2 = bus->subscribe<SimpleEvent>([&](const SimpleEvent&) { count2++; });
    auto id3 = bus->subscribe<SimpleEvent>([&](const SimpleEvent&) { count3++; });
    
    bus->publish(SimpleEvent{1});
    ASSERT_EQ(count1, 1);
    ASSERT_EQ(count2, 1);
    ASSERT_EQ(count3, 1);
    
    bus->unsubscribe(id2);  // Remove middle subscriber
    
    bus->publish(SimpleEvent{2});
    ASSERT_EQ(count1, 2);
    ASSERT_EQ(count2, 1);  // Should not increase
    ASSERT_EQ(count3, 2);
}

TEST(EventBus, SubscribeAfterUnsubscribe) {
    auto bus = create_event_bus();
    
    int call_count = 0;
    
    auto id1 = bus->subscribe<SimpleEvent>([&](const SimpleEvent&) { call_count++; });
    bus->publish(SimpleEvent{1});
    ASSERT_EQ(call_count, 1);
    
    bus->unsubscribe(id1);
    bus->publish(SimpleEvent{2});
    ASSERT_EQ(call_count, 1);
    
    // Subscribe again with new handler
    auto id2 = bus->subscribe<SimpleEvent>([&](const SimpleEvent&) { call_count += 10; });
    bus->publish(SimpleEvent{3});
    ASSERT_EQ(call_count, 11);
}

// ========================================
// Lambda and Closure Tests
// ========================================

TEST(EventBus, LambdaCaptureByValue) {
    auto bus = create_event_bus();
    
    int multiplier = 5;
    int result = 0;
    
    bus->subscribe<SimpleEvent>([multiplier, &result](const SimpleEvent& event) {
        result = event.value * multiplier;
    });
    
    bus->publish(SimpleEvent{10});
    ASSERT_EQ(result, 50);
}

TEST(EventBus, LambdaCaptureByReference) {
    auto bus = create_event_bus();
    
    int accumulator = 0;
    
    bus->subscribe<SimpleEvent>([&accumulator](const SimpleEvent& event) {
        accumulator += event.value;
    });
    
    bus->publish(SimpleEvent{10});
    bus->publish(SimpleEvent{20});
    bus->publish(SimpleEvent{30});
    
    ASSERT_EQ(accumulator, 60);
}

TEST(EventBus, MutableLambda) {
    auto bus = create_event_bus();
    
    std::vector<int> received_values;
    
    bus->subscribe<SimpleEvent>([&received_values](const SimpleEvent& event) mutable {
        received_values.push_back(event.value);
    });
    
    bus->publish(SimpleEvent{1});
    bus->publish(SimpleEvent{2});
    bus->publish(SimpleEvent{3});
    
    ASSERT_EQ(received_values.size(), 3u);
    ASSERT_EQ(received_values[0], 1);
    ASSERT_EQ(received_values[1], 2);
    ASSERT_EQ(received_values[2], 3);
}

// ========================================
// Event Order Tests
// ========================================

TEST(EventBus, MultiplePublishOrder) {
    auto bus = create_event_bus();
    
    std::vector<int> received_order;
    
    bus->subscribe<SimpleEvent>([&](const SimpleEvent& event) {
        received_order.push_back(event.value);
    });
    
    for (int i = 0; i < 10; ++i) {
        bus->publish(SimpleEvent{i});
    }
    
    ASSERT_EQ(received_order.size(), 10u);
    for (int i = 0; i < 10; ++i) {
        ASSERT_EQ(received_order[i], i);
    }
}

TEST(EventBus, SubscriberCallOrder) {
    auto bus = create_event_bus();
    
    std::vector<int> call_order;
    
    // Subscribers should be called in subscription order
    bus->subscribe<SimpleEvent>([&](const SimpleEvent&) {
        call_order.push_back(1);
    });
    
    bus->subscribe<SimpleEvent>([&](const SimpleEvent&) {
        call_order.push_back(2);
    });
    
    bus->subscribe<SimpleEvent>([&](const SimpleEvent&) {
        call_order.push_back(3);
    });
    
    bus->publish(SimpleEvent{0});
    
    ASSERT_EQ(call_order.size(), 3u);
    ASSERT_EQ(call_order[0], 1);
    ASSERT_EQ(call_order[1], 2);
    ASSERT_EQ(call_order[2], 3);
}

// ========================================
// Edge Cases Tests
// ========================================

TEST(EventBus, RepeatedUnsubscribeSameId) {
    auto bus = create_event_bus();
    
    int call_count = 0;
    auto id = bus->subscribe<SimpleEvent>([&](const SimpleEvent&) { call_count++; });
    
    bus->unsubscribe(id);
    bus->unsubscribe(id);  // Should not crash
    bus->unsubscribe(id);  // Should not crash
    
    bus->publish(SimpleEvent{1});
    ASSERT_EQ(call_count, 0);
}

TEST(EventBus, UnsubscribeZeroId) {
    auto bus = create_event_bus();
    
    // Should not crash with ID 0
    bus->unsubscribe(0);
}

TEST(EventBus, LargeNumberOfSubscribers) {
    auto bus = create_event_bus();
    
    const int num_subscribers = 1000;
    std::atomic<int> call_count{0};
    
    std::vector<EventId> ids;
    for (int i = 0; i < num_subscribers; ++i) {
        ids.push_back(bus->subscribe<SimpleEvent>([&](const SimpleEvent&) {
            call_count++;
        }));
    }
    
    bus->publish(SimpleEvent{1});
    ASSERT_EQ(call_count.load(), num_subscribers);
}

TEST(EventBus, LargeNumberOfEventTypes) {
    auto bus = create_event_bus();
    
    // Register many different event types
    struct Event1 { int value; };
    struct Event2 { int value; };
    struct Event3 { int value; };
    // ... (using the same pattern)
    
    int count1 = 0, count2 = 0, count3 = 0;
    
    bus->subscribe<Event1>([&](const Event1&) { count1++; });
    bus->subscribe<Event2>([&](const Event2&) { count2++; });
    bus->subscribe<Event3>([&](const Event3&) { count3++; });
    
    bus->publish(Event1{1});
    bus->publish(Event2{2});
    bus->publish(Event3{3});
    
    ASSERT_EQ(count1, 1);
    ASSERT_EQ(count2, 1);
    ASSERT_EQ(count3, 1);
}

// ========================================
// Stress Tests
// ========================================

TEST(EventBus, HighFrequencyPublish) {
    auto bus = create_event_bus();
    
    std::atomic<int> count{0};
    
    bus->subscribe<SimpleEvent>([&](const SimpleEvent&) {
        count++;
    });
    
    const int num_events = 10000;
    for (int i = 0; i < num_events; ++i) {
        bus->publish(SimpleEvent{i});
    }
    
    ASSERT_EQ(count.load(), num_events);
}

TEST(EventBus, RepeatedSubscribeUnsubscribe) {
    auto bus = create_event_bus();
    
    for (int i = 0; i < 100; ++i) {
        auto id = bus->subscribe<SimpleEvent>([](const SimpleEvent&) {});
        bus->publish(SimpleEvent{i});
        bus->unsubscribe(id);
    }
    
    // Should not crash or leak memory
}

// ========================================
// Thread Safety Tests (Basic)
// ========================================

TEST(EventBus, ConcurrentPublish) {
    auto bus = create_event_bus();
    
    std::atomic<int> count{0};
    
    bus->subscribe<SimpleEvent>([&](const SimpleEvent&) {
        count++;
    });
    
    const int num_threads = 4;
    const int events_per_thread = 100;
    
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < events_per_thread; ++j) {
                bus->publish(SimpleEvent{j});
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    ASSERT_EQ(count.load(), num_threads * events_per_thread);
}

TEST(EventBus, ConcurrentSubscribe) {
    auto bus = create_event_bus();
    
    const int num_threads = 4;
    std::atomic<int> total_count{0};
    std::vector<EventId> ids;
    std::mutex ids_mutex;
    
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            auto id = bus->subscribe<SimpleEvent>([&](const SimpleEvent&) {
                total_count++;
            });
            
            std::lock_guard<std::mutex> lock(ids_mutex);
            ids.push_back(id);
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    bus->publish(SimpleEvent{1});
    ASSERT_EQ(total_count.load(), num_threads);
}

TEST(EventBus, ConcurrentSubscribeUnsubscribe) {
    auto bus = create_event_bus();
    
    std::atomic<bool> running{true};
    std::atomic<int> event_count{0};
    
    // Publisher thread
    std::thread publisher([&]() {
        while (running) {
            bus->publish(SimpleEvent{1});
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    });
    
    // Subscriber threads
    std::vector<std::thread> subscribers;
    for (int i = 0; i < 4; ++i) {
        subscribers.emplace_back([&]() {
            for (int j = 0; j < 10; ++j) {
                auto id = bus->subscribe<SimpleEvent>([&](const SimpleEvent&) {
                    event_count++;
                });
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                bus->unsubscribe(id);
            }
        });
    }
    
    for (auto& t : subscribers) {
        t.join();
    }
    
    running = false;
    publisher.join();
    
    // Should not crash
}

int main() {
    return TestRunner::instance().run_all();
}
