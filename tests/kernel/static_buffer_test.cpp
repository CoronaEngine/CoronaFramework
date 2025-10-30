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
    std::atomic<bool> stop{false};
    std::atomic<std::size_t> observed{0};

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

        while (!stop.load(std::memory_order_acquire)) {
            for (std::size_t idx = 0; idx < capacity; ++idx) {
                buffer.access(idx, [&](const int& value) {
                    (void)value;
                    observed.fetch_add(1, std::memory_order_relaxed);
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
    stop.store(true, std::memory_order_release);
    reader_thread.join();

    ASSERT_EQ(produced.load(std::memory_order_acquire), consumed.load(std::memory_order_acquire));
    ASSERT_GT(observed.load(std::memory_order_acquire), 0U);
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

TEST(StaticBufferTests, RapidAllocDeallocCycle) {
    static constexpr std::size_t capacity = 32;
    StaticBuffer<int, capacity> buffer;

    std::atomic<bool> start{false};
    std::atomic<bool> failure{false};
    std::atomic<std::size_t> cycles{0};

    constexpr std::size_t thread_count = 8;
    constexpr std::size_t cycles_per_thread = 5'000;

    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    for (std::size_t tid = 0; tid < thread_count; ++tid) {
        threads.emplace_back([&, tid]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (std::size_t cycle = 0; cycle < cycles_per_thread && !failure.load(std::memory_order_relaxed);
                 ++cycle) {
                const int expected_value = static_cast<int>(tid * cycles_per_thread + cycle);

                auto index = buffer.allocate([expected_value](int& slot) { slot = expected_value; });

                if (!index.has_value()) {
                    continue;
                }

                bool read_ok = buffer.access(*index, [expected_value, &failure](const int& value) {
                    if (value != expected_value) {
                        failure.store(true, std::memory_order_release);
                    }
                });

                if (!read_ok) {
                    failure.store(true, std::memory_order_release);
                }

                bool write_ok = buffer.access_mut(*index, [](int& value) { value += 1; });

                if (!write_ok) {
                    failure.store(true, std::memory_order_release);
                }

                buffer.deallocate(*index);
                cycles.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    start.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    ASSERT_FALSE(failure.load(std::memory_order_acquire));
    ASSERT_GT(cycles.load(std::memory_order_acquire), 0U);
}

TEST(StaticBufferTests, StressTestAllocation) {
    static constexpr std::size_t capacity = 256;
    StaticBuffer<std::uint64_t, capacity> buffer;

    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<std::size_t> successful_allocs{0};
    std::atomic<std::size_t> failed_allocs{0};
    std::atomic<std::size_t> deallocations{0};

    constexpr std::size_t thread_count = 16;
    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    for (std::size_t tid = 0; tid < thread_count; ++tid) {
        threads.emplace_back([&, tid]() {
            std::vector<std::size_t> local_indices;
            local_indices.reserve(capacity / thread_count);

            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            std::size_t iteration = 0;
            while (!stop.load(std::memory_order_relaxed)) {
                if (local_indices.size() < capacity / thread_count) {
                    const std::uint64_t value = (tid << 32) | iteration;
                    auto index = buffer.allocate([value](std::uint64_t& slot) { slot = value; });

                    if (index.has_value()) {
                        local_indices.push_back(*index);
                        successful_allocs.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        failed_allocs.fetch_add(1, std::memory_order_relaxed);
                    }
                    ++iteration;
                }

                if (!local_indices.empty() && (iteration % 3 == 0)) {
                    const std::size_t idx = local_indices.back();
                    local_indices.pop_back();
                    buffer.deallocate(idx);
                    deallocations.fetch_add(1, std::memory_order_relaxed);
                }

                if (iteration % 100 == 0) {
                    std::this_thread::yield();
                }
            }

            for (std::size_t idx : local_indices) {
                buffer.deallocate(idx);
                deallocations.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    start.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    stop.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    ASSERT_GT(successful_allocs.load(std::memory_order_acquire), 0U);
    ASSERT_EQ(successful_allocs.load(std::memory_order_acquire), deallocations.load(std::memory_order_acquire));
}

TEST(StaticBufferTests, ContentionOnSameSlot) {
    static constexpr std::size_t capacity = 16;
    StaticBuffer<std::atomic<int>, capacity> buffer;

    std::vector<std::size_t> indices;
    indices.reserve(capacity);
    for (std::size_t i = 0; i < capacity; ++i) {
        auto index = buffer.allocate([](std::atomic<int>& slot) { slot.store(0, std::memory_order_relaxed); });
        ASSERT_TRUE(index.has_value());
        indices.push_back(*index);
    }

    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};

    constexpr std::size_t reader_count = 8;
    constexpr std::size_t writer_count = 8;
    std::vector<std::thread> threads;
    threads.reserve(reader_count + writer_count);

    for (std::size_t i = 0; i < writer_count; ++i) {
        threads.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            while (!stop.load(std::memory_order_relaxed)) {
                for (std::size_t idx : indices) {
                    buffer.access_mut(idx, [](std::atomic<int>& value) {
                        value.fetch_add(1, std::memory_order_relaxed);
                    });
                }
            }
        });
    }

    for (std::size_t i = 0; i < reader_count; ++i) {
        threads.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            while (!stop.load(std::memory_order_relaxed)) {
                for (std::size_t idx : indices) {
                    buffer.access(idx, [](const std::atomic<int>& value) {
                        const int val = value.load(std::memory_order_relaxed);
                        ASSERT_GE(val, 0);
                    });
                }
            }
        });
    }

    start.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    int total = 0;
    for (std::size_t idx : indices) {
        buffer.access(idx, [&total](const std::atomic<int>& value) {
            total += value.load(std::memory_order_acquire);
        });
        buffer.deallocate(idx);
    }

    ASSERT_GT(total, 0);
}

TEST(StaticBufferTests, MemoryOrderingValidation) {
    static constexpr std::size_t capacity = 64;
    StaticBuffer<std::pair<int, int>, capacity> buffer;

    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<bool> ordering_violation{false};

    constexpr std::size_t producer_count = 4;
    constexpr std::size_t validator_count = 4;
    std::vector<std::thread> threads;
    threads.reserve(producer_count + validator_count);

    for (std::size_t pid = 0; pid < producer_count; ++pid) {
        threads.emplace_back([&, pid]() {
            std::vector<std::size_t> owned;

            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            int seq = 0;
            while (!stop.load(std::memory_order_relaxed)) {
                auto index = buffer.allocate([seq](std::pair<int, int>& slot) {
                    slot.first = seq;
                    slot.second = seq;
                });

                if (index.has_value()) {
                    owned.push_back(*index);

                    buffer.access_mut(*index, [seq](std::pair<int, int>& slot) {
                        slot.first = seq + 1;
                        slot.second = seq + 1;
                    });

                    ++seq;
                }

                if (!owned.empty() && seq % 10 == 0) {
                    const std::size_t idx = owned.back();
                    owned.pop_back();
                    buffer.deallocate(idx);
                }
            }

            for (std::size_t idx : owned) {
                buffer.deallocate(idx);
            }
        });
    }

    for (std::size_t vid = 0; vid < validator_count; ++vid) {
        threads.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            while (!stop.load(std::memory_order_relaxed)) {
                for (std::size_t idx = 0; idx < capacity; ++idx) {
                    buffer.access(idx, [&](const std::pair<int, int>& value) {
                        if (value.first != value.second) {
                            ordering_violation.store(true, std::memory_order_release);
                        }
                    });
                }
            }
        });
    }

    start.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    stop.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    ASSERT_FALSE(ordering_violation.load(std::memory_order_acquire));
}

int main() {
    return CoronaTest::TestRunner::instance().run_all();
}
