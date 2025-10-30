#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "../test_framework.h"
#include "corona/kernel/utils/storage.h"

using Corona::Kernel::Utils::StaticBuffer;

TEST(StaticBufferTests, AllocateAccessDeallocate) {
    StaticBuffer<int, 8> buffer;

    std::vector<std::size_t> indices;
    for (int i = 0; i < 8; ++i) {
        auto index = buffer.allocate([i](int& slot) { slot = i * 10; });
        ASSERT_TRUE(index.has_value());
        indices.push_back(*index);
    }

    ASSERT_FALSE(buffer.allocate([](int&) {}));

    for (int i = 0; i < 8; ++i) {
        const std::size_t idx = indices[i];
        ASSERT_TRUE(buffer.access(idx, [i](const int& value) {
            ASSERT_EQ(value, i * 10);
        }));
        ASSERT_TRUE(buffer.access_mut(idx, [i](int& value) {
            value = i * 10 + 1;
        }));
    }

    for (int i = 0; i < 8; ++i) {
        const std::size_t idx = indices[i];
        ASSERT_TRUE(buffer.access(idx, [i](const int& value) {
            ASSERT_EQ(value, i * 10 + 1);
        }));
        buffer.deallocate(idx);
    }

    auto recycled = buffer.allocate([](int& slot) { slot = 42; });
    ASSERT_TRUE(recycled.has_value());
    ASSERT_TRUE(buffer.access(*recycled, [](const int& value) { ASSERT_EQ(value, 42); }));
}

TEST(StaticBufferTests, MultiThreadConcurrentAccess) {
    static constexpr std::size_t capacity = 64;
    StaticBuffer<int, capacity> buffer;

    std::atomic<bool> start{false};
    std::atomic<std::size_t> produced{0};
    std::atomic<std::size_t> consumed{0};

    const auto producer = [&]() {
        std::vector<std::size_t> local_indices;
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        for (int iteration = 0; iteration < 1'000; ++iteration) {
            auto index = buffer.allocate([&](int& slot) { slot = iteration; });
            if (index.has_value()) {
                local_indices.push_back(*index);
                produced.fetch_add(1, std::memory_order_release);
            } else {
                std::this_thread::yield();
            }

            while (!local_indices.empty()) {
                const std::size_t idx = local_indices.back();
                bool read_ok = buffer.access(idx, [&](const int& value) {
                    ASSERT_GE(value, 0);
                });
                if (read_ok) {
                    buffer.deallocate(idx);
                    local_indices.pop_back();
                    consumed.fetch_add(1, std::memory_order_release);
                } else {
                    std::this_thread::yield();
                }
            }
        }
    };

    const auto reader = [&]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        std::size_t observed = 0;
        while (observed < 20'000) {
            for (std::size_t idx = 0; idx < capacity; ++idx) {
                buffer.access(idx, [&](const int& value) {
                    (void)value;
                    ++observed;
                });
            }
            std::this_thread::yield();
        }
    };

    std::thread producer_thread1(producer);
    std::thread producer_thread2(producer);
    std::thread reader_thread(reader);

    start.store(true, std::memory_order_release);

    producer_thread1.join();
    producer_thread2.join();
    reader_thread.join();

    ASSERT_EQ(produced.load(std::memory_order_acquire), consumed.load(std::memory_order_acquire));
}

TEST(StaticBufferTests, ForEachTraversal) {
    static constexpr std::size_t capacity = 16;
    StaticBuffer<int, capacity> buffer;

    std::vector<std::size_t> indices;
    indices.reserve(capacity / 2);
    std::vector<int> initial_values;
    initial_values.reserve(capacity / 2);

    int expected_sum = 0;
    for (int i = 0; i < 8; ++i) {
        const int value = i + 1;
        auto index = buffer.allocate([value](int& slot) { slot = value; });
        ASSERT_TRUE(index.has_value());
        indices.push_back(*index);
        initial_values.push_back(value);
        expected_sum += value;
    }

    int sum = 0;
    int visits = 0;
    buffer.for_each_const([&](const int& value) {
        sum += value;
        ++visits;
    });

    ASSERT_EQ(visits, static_cast<int>(indices.size()));
    ASSERT_EQ(sum, expected_sum);

    buffer.for_each([](int& value) { value *= 2; });

    for (std::size_t i = 0; i < indices.size(); ++i) {
        const std::size_t idx = indices[i];
        const int expected = initial_values[i] * 2;
        ASSERT_TRUE(buffer.access(idx, [expected](const int& value) { ASSERT_EQ(value, expected); }));
    }

    std::atomic<bool> lock_ready{false};
    std::atomic<bool> release_lock{false};

    const std::size_t guarded_index = indices.front();
    std::thread locker([&]() {
        buffer.access_mut(guarded_index, [&](int& value) {
            lock_ready.store(true, std::memory_order_release);
            while (!release_lock.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            value += 5;
        });
    });

    while (!lock_ready.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    std::thread releaser([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        release_lock.store(true, std::memory_order_release);
    });

    int traversal_count = 0;
    buffer.for_each_const([&](const int& value) {
        (void)value;
        ++traversal_count;
    });

    release_lock.store(true, std::memory_order_release);
    releaser.join();
    locker.join();

    ASSERT_EQ(traversal_count, static_cast<int>(indices.size()));
    ASSERT_TRUE(buffer.access(guarded_index, [&](const int& value) {
        ASSERT_EQ(value, initial_values.front() * 2 + 5);
    }));
}

TEST(StaticBufferTests, MixedConcurrentOperations) {
    static constexpr std::size_t capacity = 128;
    StaticBuffer<int, capacity> buffer;

    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<std::size_t> allocations{0};
    std::atomic<std::size_t> reads{0};
    std::atomic<std::size_t> writes{0};

    std::vector<std::thread> threads;

    auto producer = [&]() {
        std::vector<std::size_t> owned;
        owned.reserve(capacity / 2);

        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!stop.load(std::memory_order_relaxed)) {
            if (auto index = buffer.allocate([](int& slot) { slot = 1; })) {
                owned.push_back(*index);
                allocations.fetch_add(1, std::memory_order_release);
            } else {
                std::this_thread::yield();
            }

            if (!owned.empty()) {
                const std::size_t idx = owned.back();
                bool wrote = buffer.access_mut(idx, [&](int& slot) {
                    slot += 1;
                    writes.fetch_add(1, std::memory_order_relaxed);
                });
                if (wrote) {
                    buffer.deallocate(idx);
                    owned.pop_back();
                } else {
                    std::this_thread::yield();
                }
            }
        }

        for (std::size_t idx : owned) {
            buffer.deallocate(idx);
        }
    };

    auto reader = [&]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!stop.load(std::memory_order_relaxed)) {
            for (std::size_t idx = 0; idx < capacity; ++idx) {
                buffer.access(idx, [&](const int& value) {
                    ASSERT_GE(value, 1);
                    reads.fetch_add(1, std::memory_order_relaxed);
                });
            }
            std::this_thread::yield();
        }
    };

    threads.emplace_back(producer);
    threads.emplace_back(producer);
    threads.emplace_back(reader);
    threads.emplace_back(reader);

    start.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stop.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    ASSERT_GT(allocations.load(std::memory_order_acquire), 0U);
    ASSERT_GT(reads.load(std::memory_order_acquire), 0U);
    ASSERT_GT(writes.load(std::memory_order_acquire), 0U);
}

int main() {
    return CoronaTest::TestRunner::instance().run_all();
}
