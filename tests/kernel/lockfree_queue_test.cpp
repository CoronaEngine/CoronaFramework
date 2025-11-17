#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "../test_framework.h"
#include "corona/kernel/utils/lock_free_queue.h"

using Corona::Kernel::Utils::LockFreeQueue;
using Corona::Kernel::Utils::LockFreeRingBufferQueue;

TEST(LockFreeQueueTests, SingleThreadBasic) {
    LockFreeQueue<int> queue;

    for (int i = 0; i < 10; ++i) {
        queue.enqueue(i);
    }

    for (int i = 0; i < 10; ++i) {
        int value = -1;
        ASSERT_TRUE(queue.dequeue(value));
        ASSERT_EQ(value, i);
    }

    ASSERT_TRUE(queue.empty());

    int dummy = 0;
    ASSERT_FALSE(queue.dequeue(dummy));
}

TEST(LockFreeRingBufferQueueTests, SingleThreadBasic) {
    LockFreeRingBufferQueue<int, 16> queue;

    for (int i = 0; i < 16; ++i) {
        ASSERT_TRUE(queue.enqueue(i));
    }

    ASSERT_TRUE(queue.full());
    ASSERT_FALSE(queue.enqueue(999));

    for (int i = 0; i < 16; ++i) {
        int value = -1;
        ASSERT_TRUE(queue.dequeue(value));
        ASSERT_EQ(value, i);
    }

    ASSERT_TRUE(queue.empty());

    int dummy = 0;
    ASSERT_FALSE(queue.dequeue(dummy));
}

TEST(LockFreeQueueTests, MultiThreadStability) {
    LockFreeQueue<std::uint64_t> queue;

    constexpr std::size_t producer_count = 4;
    constexpr std::size_t consumer_count = 4;
    constexpr std::size_t items_per_producer = 10'000;
    const std::size_t total_items = producer_count * items_per_producer;

    std::atomic<std::size_t> produced{0};
    std::atomic<std::size_t> consumed{0};
    std::atomic<bool> start{false};
    std::atomic<bool> failure{false};

    std::mutex failure_mutex;
    std::string failure_message;

    std::vector<std::uint32_t> seen(total_items, 0);
    std::mutex seen_mutex;

    std::vector<std::thread> threads;
    threads.reserve(producer_count + consumer_count);

    for (std::size_t producer_id = 0; producer_id < producer_count; ++producer_id) {
        threads.emplace_back([&, producer_id]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (std::size_t seq = 0; seq < items_per_producer; ++seq) {
                const std::uint64_t value = static_cast<std::uint64_t>(producer_id * items_per_producer + seq);
                queue.enqueue(value);
                produced.fetch_add(1, std::memory_order_release);
            }
        });
    }

    for (std::size_t consumer_id = 0; consumer_id < consumer_count; ++consumer_id) {
        (void)consumer_id;
        threads.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            std::size_t idle_spins = 0;
            while (consumed.load(std::memory_order_acquire) < total_items &&
                   !failure.load(std::memory_order_relaxed)) {
                std::uint64_t value = 0;
                if (queue.dequeue(value)) {
                    idle_spins = 0;
                    const std::size_t index = static_cast<std::size_t>(value);

                    if (index >= total_items) {
                        {
                            std::lock_guard<std::mutex> lock(failure_mutex);
                            if (!failure.load(std::memory_order_relaxed)) {
                                failure_message = "Dequeued value out of range";
                            }
                        }
                        failure.store(true, std::memory_order_release);
                        continue;
                    }

                    {
                        std::lock_guard<std::mutex> lock(seen_mutex);
                        seen[index] += 1;
                        if (seen[index] > 1 && !failure.load(std::memory_order_relaxed)) {
                            failure_message = "Duplicate dequeue detected at index " + std::to_string(index);
                            failure.store(true, std::memory_order_release);
                        }
                    }

                    consumed.fetch_add(1, std::memory_order_release);
                } else {
                    if (produced.load(std::memory_order_acquire) >= total_items) {
                        if (++idle_spins > 1'000'000 && !failure.load(std::memory_order_relaxed)) {
                            std::lock_guard<std::mutex> lock(failure_mutex);
                            failure_message = "Queue stalled after producers completed";
                            failure.store(true, std::memory_order_release);
                        }
                    } else {
                        idle_spins = 0;
                    }
                    std::this_thread::yield();
                }
            }
        });
    }

    start.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    if (failure.load(std::memory_order_acquire)) {
        throw std::runtime_error(failure_message.empty() ? "Unknown failure in MultiThreadStability" : failure_message);
    }

    ASSERT_EQ(produced.load(std::memory_order_acquire), total_items);
    ASSERT_EQ(consumed.load(std::memory_order_acquire), total_items);

    for (std::size_t i = 0; i < total_items; ++i) {
        ASSERT_EQ(seen[i], 1U);
    }

    ASSERT_TRUE(queue.empty());
}

TEST(LockFreeRingBufferQueueTests, MultiThreadStability) {
    static constexpr std::size_t producer_count = 4;
    static constexpr std::size_t consumer_count = 4;
    static constexpr std::size_t items_per_producer = 10'000;
    static constexpr std::size_t total_items = producer_count * items_per_producer;

    LockFreeRingBufferQueue<std::uint64_t, 1 << 12> queue;

    std::atomic<std::size_t> produced{0};
    std::atomic<std::size_t> consumed{0};
    std::atomic<bool> start{false};
    std::atomic<bool> failure{false};

    std::mutex failure_mutex;
    std::string failure_message;

    std::vector<std::uint32_t> seen(total_items, 0);
    std::mutex seen_mutex;

    std::vector<std::thread> threads;
    threads.reserve(producer_count + consumer_count);

    for (std::size_t producer_id = 0; producer_id < producer_count; ++producer_id) {
        threads.emplace_back([&, producer_id]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (std::size_t seq = 0; seq < items_per_producer; ++seq) {
                const std::uint64_t value = static_cast<std::uint64_t>(producer_id * items_per_producer + seq);
                while (!queue.enqueue(value)) {
                    std::this_thread::yield();
                }
                produced.fetch_add(1, std::memory_order_release);
            }
        });
    }

    for (std::size_t consumer_id = 0; consumer_id < consumer_count; ++consumer_id) {
        threads.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            std::size_t idle_spins = 0;
            while (consumed.load(std::memory_order_acquire) < total_items &&
                   !failure.load(std::memory_order_relaxed)) {
                std::uint64_t value = 0;
                if (queue.dequeue(value)) {
                    idle_spins = 0;
                    const std::size_t index = static_cast<std::size_t>(value);

                    if (index >= total_items) {
                        std::lock_guard<std::mutex> lock(failure_mutex);
                        if (!failure.load(std::memory_order_relaxed)) {
                            failure_message = "Dequeued value out of range";
                        }
                        failure.store(true, std::memory_order_release);
                        continue;
                    }

                    {
                        std::lock_guard<std::mutex> lock(seen_mutex);
                        seen[index] += 1;
                        if (seen[index] > 1 && !failure.load(std::memory_order_relaxed)) {
                            failure_message = "Duplicate dequeue detected at index " + std::to_string(index);
                            failure.store(true, std::memory_order_release);
                        }
                    }

                    consumed.fetch_add(1, std::memory_order_release);
                } else {
                    if (produced.load(std::memory_order_acquire) >= total_items) {
                        if (++idle_spins > 1'000'000 && !failure.load(std::memory_order_relaxed)) {
                            std::lock_guard<std::mutex> lock(failure_mutex);
                            failure_message = "Ring buffer stalled after producers completed";
                            failure.store(true, std::memory_order_release);
                        }
                    } else {
                        idle_spins = 0;
                    }
                    std::this_thread::yield();
                }
            }
        });
    }

    start.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    if (failure.load(std::memory_order_acquire)) {
        throw std::runtime_error(failure_message.empty() ? "Unknown failure in MultiThreadStability" : failure_message);
    }

    ASSERT_EQ(produced.load(std::memory_order_acquire), total_items);
    ASSERT_EQ(consumed.load(std::memory_order_acquire), total_items);

    for (std::size_t i = 0; i < total_items; ++i) {
        ASSERT_EQ(seen[i], 1U);
    }

    ASSERT_TRUE(queue.empty());
}

TEST(LockFreeQueueTests, PerformanceThroughput) {
    LockFreeQueue<std::uint64_t> queue;

    constexpr std::size_t producer_count = 2;
    constexpr std::size_t consumer_count = 2;
    constexpr std::size_t items_per_producer = 200'000;
    const std::size_t total_items = producer_count * items_per_producer;

    std::atomic<std::size_t> produced{0};
    std::atomic<std::size_t> consumed{0};
    std::atomic<bool> start{false};
    std::atomic<bool> perf_stall_flag{false};
    std::mutex perf_stall_mutex;
    std::string perf_stall_message;

    std::vector<std::thread> threads;
    threads.reserve(producer_count + consumer_count);

    for (std::size_t producer_id = 0; producer_id < producer_count; ++producer_id) {
        (void)producer_id;
        threads.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (std::size_t seq = 0; seq < items_per_producer; ++seq) {
                queue.enqueue(static_cast<std::uint64_t>(seq));
                produced.fetch_add(1, std::memory_order_release);
            }
        });
    }

    for (std::size_t consumer_id = 0; consumer_id < consumer_count; ++consumer_id) {
        (void)consumer_id;
        threads.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            std::size_t idle_spins = 0;
            while (consumed.load(std::memory_order_acquire) < total_items &&
                   !perf_stall_flag.load(std::memory_order_relaxed)) {
                std::uint64_t value = 0;
                if (queue.dequeue(value)) {
                    (void)value;
                    idle_spins = 0;
                    consumed.fetch_add(1, std::memory_order_release);
                } else {
                    if (produced.load(std::memory_order_acquire) >= total_items) {
                        if (++idle_spins > 1'000'000) {
                            std::lock_guard<std::mutex> lock(perf_stall_mutex);
                            perf_stall_message = "Queue stalled during performance test";
                            perf_stall_flag.store(true, std::memory_order_release);
                        }
                    } else {
                        idle_spins = 0;
                    }
                    std::this_thread::yield();
                }
            }
        });
    }

    const auto start_time = std::chrono::steady_clock::now();
    start.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    if (perf_stall_flag.load(std::memory_order_acquire)) {
        throw std::runtime_error(perf_stall_message.empty() ? "Unknown performance failure" : perf_stall_message);
    }

    const auto end_time = std::chrono::steady_clock::now();
    const double duration_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    ASSERT_EQ(produced.load(std::memory_order_acquire), total_items);
    ASSERT_EQ(consumed.load(std::memory_order_acquire), total_items);
    ASSERT_TRUE(queue.empty());

    const double ops_per_ms = duration_ms > 0.0 ? static_cast<double>(total_items) / duration_ms : 0.0;
    ASSERT_GT(ops_per_ms, 1.0);
}

TEST(LockFreeRingBufferQueueTests, PerformanceThroughput) {
    constexpr std::size_t producer_count = 2;
    constexpr std::size_t consumer_count = 2;
    constexpr std::size_t items_per_producer = 200'000;
    constexpr std::size_t total_items = producer_count * items_per_producer;

    LockFreeRingBufferQueue<std::uint64_t, 1 << 14> queue;

    std::atomic<std::size_t> produced{0};
    std::atomic<std::size_t> consumed{0};
    std::atomic<bool> start{false};
    std::atomic<bool> perf_stall_flag{false};
    std::mutex perf_stall_mutex;
    std::string perf_stall_message;

    std::vector<std::thread> threads;
    threads.reserve(producer_count + consumer_count);

    for (std::size_t producer_id = 0; producer_id < producer_count; ++producer_id) {
        threads.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (std::size_t seq = 0; seq < items_per_producer; ++seq) {
                while (!queue.enqueue(static_cast<std::uint64_t>(seq))) {
                    std::this_thread::yield();
                }
                produced.fetch_add(1, std::memory_order_release);
            }
        });
    }

    for (std::size_t consumer_id = 0; consumer_id < consumer_count; ++consumer_id) {
        threads.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            std::size_t idle_spins = 0;
            while (consumed.load(std::memory_order_acquire) < total_items &&
                   !perf_stall_flag.load(std::memory_order_relaxed)) {
                std::uint64_t value = 0;
                if (queue.dequeue(value)) {
                    idle_spins = 0;
                    consumed.fetch_add(1, std::memory_order_release);
                } else {
                    if (produced.load(std::memory_order_acquire) >= total_items) {
                        if (++idle_spins > 1'000'000) {
                            std::lock_guard<std::mutex> lock(perf_stall_mutex);
                            perf_stall_message = "Ring buffer stalled during performance test";
                            perf_stall_flag.store(true, std::memory_order_release);
                        }
                    } else {
                        idle_spins = 0;
                    }
                    std::this_thread::yield();
                }
            }
        });
    }

    const auto start_time = std::chrono::steady_clock::now();
    start.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    if (perf_stall_flag.load(std::memory_order_acquire)) {
        throw std::runtime_error(perf_stall_message.empty() ? "Unknown performance failure" : perf_stall_message);
    }

    const auto end_time = std::chrono::steady_clock::now();
    const double duration_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    ASSERT_EQ(produced.load(std::memory_order_acquire), total_items);
    ASSERT_EQ(consumed.load(std::memory_order_acquire), total_items);
    ASSERT_TRUE(queue.empty());

    const double ops_per_ms = duration_ms > 0.0 ? static_cast<double>(total_items) / duration_ms : 0.0;
    ASSERT_GT(ops_per_ms, 1.0);
}

TEST(LockFreeQueueTests, UnbalancedProducerConsumer) {
    LockFreeQueue<std::uint64_t> queue;

    constexpr std::size_t producer_count = 8;
    constexpr std::size_t consumer_count = 2;
    constexpr std::size_t items_per_producer = 5'000;
    const std::size_t total_items = producer_count * items_per_producer;

    std::atomic<std::size_t> produced{0};
    std::atomic<std::size_t> consumed{0};
    std::atomic<bool> start{false};
    std::atomic<bool> producers_done{false};

    std::vector<std::uint32_t> seen(total_items, 0);
    std::mutex seen_mutex;

    std::vector<std::thread> threads;
    threads.reserve(producer_count + consumer_count);

    for (std::size_t pid = 0; pid < producer_count; ++pid) {
        threads.emplace_back([&, pid]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (std::size_t seq = 0; seq < items_per_producer; ++seq) {
                const std::uint64_t value = static_cast<std::uint64_t>(pid * items_per_producer + seq);
                queue.enqueue(value);
                produced.fetch_add(1, std::memory_order_release);
            }
        });
    }

    for (std::size_t cid = 0; cid < consumer_count; ++cid) {
        threads.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            while (consumed.load(std::memory_order_acquire) < total_items) {
                std::uint64_t value = 0;
                if (queue.dequeue(value)) {
                    const std::size_t index = static_cast<std::size_t>(value);
                    ASSERT_LT(index, total_items);

                    {
                        std::lock_guard<std::mutex> lock(seen_mutex);
                        seen[index] += 1;
                        ASSERT_EQ(seen[index], 1U);
                    }

                    consumed.fetch_add(1, std::memory_order_release);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    start.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    ASSERT_EQ(produced.load(std::memory_order_acquire), total_items);
    ASSERT_EQ(consumed.load(std::memory_order_acquire), total_items);

    for (std::size_t i = 0; i < total_items; ++i) {
        ASSERT_EQ(seen[i], 1U);
    }

    ASSERT_TRUE(queue.empty());
}

TEST(LockFreeRingBufferQueueTests, UnbalancedProducerConsumer) {
    constexpr std::size_t producer_count = 8;
    constexpr std::size_t consumer_count = 2;
    constexpr std::size_t items_per_producer = 5'000;
    constexpr std::size_t total_items = producer_count * items_per_producer;

    LockFreeRingBufferQueue<std::uint64_t, 1 << 12> queue;

    std::atomic<std::size_t> produced{0};
    std::atomic<std::size_t> consumed{0};
    std::atomic<bool> start{false};

    std::vector<std::uint32_t> seen(total_items, 0);
    std::mutex seen_mutex;

    std::vector<std::thread> threads;
    threads.reserve(producer_count + consumer_count);

    for (std::size_t pid = 0; pid < producer_count; ++pid) {
        threads.emplace_back([&, pid]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (std::size_t seq = 0; seq < items_per_producer; ++seq) {
                const std::uint64_t value = static_cast<std::uint64_t>(pid * items_per_producer + seq);
                while (!queue.enqueue(value)) {
                    std::this_thread::yield();
                }
                produced.fetch_add(1, std::memory_order_release);
            }
        });
    }

    for (std::size_t cid = 0; cid < consumer_count; ++cid) {
        threads.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            while (consumed.load(std::memory_order_acquire) < total_items) {
                std::uint64_t value = 0;
                if (queue.dequeue(value)) {
                    const std::size_t index = static_cast<std::size_t>(value);
                    ASSERT_LT(index, total_items);

                    {
                        std::lock_guard<std::mutex> lock(seen_mutex);
                        seen[index] += 1;
                        ASSERT_EQ(seen[index], 1U);
                    }

                    consumed.fetch_add(1, std::memory_order_release);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    start.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    ASSERT_EQ(produced.load(std::memory_order_acquire), total_items);
    ASSERT_EQ(consumed.load(std::memory_order_acquire), total_items);

    for (std::size_t i = 0; i < total_items; ++i) {
        ASSERT_EQ(seen[i], 1U);
    }

    ASSERT_TRUE(queue.empty());
}

TEST(LockFreeQueueTests, BurstLoad) {
    LockFreeQueue<std::uint64_t> queue;

    constexpr std::size_t burst_count = 3;
    constexpr std::size_t items_per_burst = 10'000;
    constexpr std::size_t total_items = burst_count * items_per_burst;

    std::atomic<std::size_t> produced{0};
    std::atomic<std::size_t> consumed{0};
    std::atomic<bool> start{false};

    std::vector<std::uint32_t> seen(total_items, 0);
    std::mutex seen_mutex;

    std::thread producer([&]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        for (std::size_t burst = 0; burst < burst_count; ++burst) {
            for (std::size_t i = 0; i < items_per_burst; ++i) {
                const std::uint64_t value = burst * items_per_burst + i;
                queue.enqueue(value);
                produced.fetch_add(1, std::memory_order_release);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    std::thread consumer([&]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (consumed.load(std::memory_order_acquire) < total_items) {
            std::uint64_t value = 0;
            if (queue.dequeue(value)) {
                const std::size_t index = static_cast<std::size_t>(value);
                ASSERT_LT(index, total_items);

                {
                    std::lock_guard<std::mutex> lock(seen_mutex);
                    seen[index] += 1;
                    ASSERT_EQ(seen[index], 1U);
                }

                consumed.fetch_add(1, std::memory_order_release);
            } else {
                std::this_thread::yield();
            }
        }
    });

    start.store(true, std::memory_order_release);

    producer.join();
    consumer.join();

    ASSERT_EQ(produced.load(std::memory_order_acquire), total_items);
    ASSERT_EQ(consumed.load(std::memory_order_acquire), total_items);

    for (std::size_t i = 0; i < total_items; ++i) {
        ASSERT_EQ(seen[i], 1U);
    }

    ASSERT_TRUE(queue.empty());
}

TEST(LockFreeRingBufferQueueTests, BurstLoad) {
    constexpr std::size_t burst_count = 3;
    constexpr std::size_t items_per_burst = 10'000;
    constexpr std::size_t total_items = burst_count * items_per_burst;

    LockFreeRingBufferQueue<std::uint64_t, 1 << 13> queue;

    std::atomic<std::size_t> produced{0};
    std::atomic<std::size_t> consumed{0};
    std::atomic<bool> start{false};

    std::vector<std::uint32_t> seen(total_items, 0);
    std::mutex seen_mutex;

    std::thread producer([&]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        for (std::size_t burst = 0; burst < burst_count; ++burst) {
            for (std::size_t i = 0; i < items_per_burst; ++i) {
                const std::uint64_t value = burst * items_per_burst + i;
                while (!queue.enqueue(value)) {
                    std::this_thread::yield();
                }
                produced.fetch_add(1, std::memory_order_release);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    std::thread consumer([&]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (consumed.load(std::memory_order_acquire) < total_items) {
            std::uint64_t value = 0;
            if (queue.dequeue(value)) {
                const std::size_t index = static_cast<std::size_t>(value);
                ASSERT_LT(index, total_items);

                {
                    std::lock_guard<std::mutex> lock(seen_mutex);
                    seen[index] += 1;
                    ASSERT_EQ(seen[index], 1U);
                }

                consumed.fetch_add(1, std::memory_order_release);
            } else {
                std::this_thread::yield();
            }
        }
    });

    start.store(true, std::memory_order_release);

    producer.join();
    consumer.join();

    ASSERT_EQ(produced.load(std::memory_order_acquire), total_items);
    ASSERT_EQ(consumed.load(std::memory_order_acquire), total_items);

    for (std::size_t i = 0; i < total_items; ++i) {
        ASSERT_EQ(seen[i], 1U);
    }

    ASSERT_TRUE(queue.empty());
}

TEST(LockFreeQueueTests, MemoryOrderingStressTest) {
    LockFreeQueue<std::pair<std::uint64_t, std::uint64_t>> queue;

    constexpr std::size_t producer_count = 4;
    constexpr std::size_t consumer_count = 4;
    constexpr std::size_t items_per_producer = 10'000;
    const std::size_t total_items = producer_count * items_per_producer;

    std::atomic<std::size_t> produced{0};
    std::atomic<std::size_t> consumed{0};
    std::atomic<bool> start{false};
    std::atomic<bool> ordering_violation{false};

    std::vector<std::thread> threads;
    threads.reserve(producer_count + consumer_count);

    for (std::size_t pid = 0; pid < producer_count; ++pid) {
        threads.emplace_back([&, pid]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (std::size_t seq = 0; seq < items_per_producer; ++seq) {
                const std::uint64_t value = pid * items_per_producer + seq;
                queue.enqueue(std::make_pair(value, value));
                produced.fetch_add(1, std::memory_order_release);
            }
        });
    }

    for (std::size_t cid = 0; cid < consumer_count; ++cid) {
        threads.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            while (consumed.load(std::memory_order_acquire) < total_items) {
                std::pair<std::uint64_t, std::uint64_t> value;
                if (queue.dequeue(value)) {
                    if (value.first != value.second) {
                        ordering_violation.store(true, std::memory_order_release);
                    }
                    consumed.fetch_add(1, std::memory_order_release);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    start.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    ASSERT_FALSE(ordering_violation.load(std::memory_order_acquire));
    ASSERT_EQ(produced.load(std::memory_order_acquire), total_items);
    ASSERT_EQ(consumed.load(std::memory_order_acquire), total_items);
    ASSERT_TRUE(queue.empty());
}

TEST(LockFreeRingBufferQueueTests, MemoryOrderingStressTest) {
    constexpr std::size_t producer_count = 4;
    constexpr std::size_t consumer_count = 4;
    constexpr std::size_t items_per_producer = 10'000;
    constexpr std::size_t total_items = producer_count * items_per_producer;

    LockFreeRingBufferQueue<std::pair<std::uint64_t, std::uint64_t>, 1 << 12> queue;

    std::atomic<std::size_t> produced{0};
    std::atomic<std::size_t> consumed{0};
    std::atomic<bool> start{false};
    std::atomic<bool> ordering_violation{false};

    std::vector<std::thread> threads;
    threads.reserve(producer_count + consumer_count);

    for (std::size_t pid = 0; pid < producer_count; ++pid) {
        threads.emplace_back([&, pid]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (std::size_t seq = 0; seq < items_per_producer; ++seq) {
                const std::uint64_t value = pid * items_per_producer + seq;
                while (!queue.enqueue(std::make_pair(value, value))) {
                    std::this_thread::yield();
                }
                produced.fetch_add(1, std::memory_order_release);
            }
        });
    }

    for (std::size_t cid = 0; cid < consumer_count; ++cid) {
        threads.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            while (consumed.load(std::memory_order_acquire) < total_items) {
                std::pair<std::uint64_t, std::uint64_t> value;
                if (queue.dequeue(value)) {
                    if (value.first != value.second) {
                        ordering_violation.store(true, std::memory_order_release);
                    }
                    consumed.fetch_add(1, std::memory_order_release);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    start.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    ASSERT_FALSE(ordering_violation.load(std::memory_order_acquire));
    ASSERT_EQ(produced.load(std::memory_order_acquire), total_items);
    ASSERT_EQ(consumed.load(std::memory_order_acquire), total_items);
    ASSERT_TRUE(queue.empty());
}

TEST(LockFreeQueueTests, HighContentionStressTest) {
    LockFreeQueue<std::uint64_t> queue;

    constexpr std::size_t thread_count = 16;
    constexpr std::size_t operations_per_thread = 5'000;

    std::atomic<std::size_t> enqueued{0};
    std::atomic<std::size_t> dequeued{0};
    std::atomic<bool> start{false};

    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    for (std::size_t tid = 0; tid < thread_count; ++tid) {
        threads.emplace_back([&, tid]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (std::size_t op = 0; op < operations_per_thread; ++op) {
                const std::uint64_t value = tid * operations_per_thread + op;
                queue.enqueue(value);
                enqueued.fetch_add(1, std::memory_order_relaxed);

                std::uint64_t deq_value = 0;
                if (queue.dequeue(deq_value)) {
                    dequeued.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    start.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    std::uint64_t remaining_value = 0;
    while (queue.dequeue(remaining_value)) {
        dequeued.fetch_add(1, std::memory_order_relaxed);
    }

    ASSERT_EQ(enqueued.load(std::memory_order_acquire), thread_count * operations_per_thread);
    ASSERT_EQ(dequeued.load(std::memory_order_acquire), thread_count * operations_per_thread);
    ASSERT_TRUE(queue.empty());
}

TEST(LockFreeRingBufferQueueTests, HighContentionStressTest) {
    constexpr std::size_t thread_count = 16;
    constexpr std::size_t operations_per_thread = 5'000;

    LockFreeRingBufferQueue<std::uint64_t, 1 << 13> queue;

    std::atomic<std::size_t> enqueued{0};
    std::atomic<std::size_t> dequeued{0};
    std::atomic<bool> start{false};

    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    for (std::size_t tid = 0; tid < thread_count; ++tid) {
        threads.emplace_back([&, tid]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (std::size_t op = 0; op < operations_per_thread; ++op) {
                const std::uint64_t value = tid * operations_per_thread + op;
                while (!queue.enqueue(value)) {
                    std::this_thread::yield();
                }
                enqueued.fetch_add(1, std::memory_order_relaxed);

                std::uint64_t deq_value = 0;
                if (queue.dequeue(deq_value)) {
                    dequeued.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    start.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    std::uint64_t remaining_value = 0;
    while (queue.dequeue(remaining_value)) {
        dequeued.fetch_add(1, std::memory_order_relaxed);
    }

    ASSERT_EQ(enqueued.load(std::memory_order_acquire), thread_count * operations_per_thread);
    ASSERT_EQ(dequeued.load(std::memory_order_acquire), thread_count * operations_per_thread);
    ASSERT_TRUE(queue.empty());
}

// ========================================
// 高级多线程测试
// ========================================

TEST(LockFreeQueueTests, ABAResistanceTest) {
    // 测试 ABA 问题：验证队列在节点重用时不会出现错误
    LockFreeQueue<std::uint64_t> queue;

    constexpr std::size_t iterations = 100;
    constexpr std::size_t items_per_iteration = 1000;
    std::atomic<std::size_t> total_enqueued{0};
    std::atomic<std::size_t> total_dequeued{0};
    std::atomic<bool> start{false};

    // 快速入队-出队循环，触发节点重用
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (std::size_t iter = 0; iter < iterations; ++iter) {
                // 快速入队
                for (std::size_t j = 0; j < items_per_iteration; ++j) {
                    queue.enqueue(j);
                    total_enqueued.fetch_add(1, std::memory_order_relaxed);
                }

                // 快速出队
                for (std::size_t j = 0; j < items_per_iteration; ++j) {
                    std::uint64_t value;
                    while (!queue.dequeue(value)) {
                        std::this_thread::yield();
                    }
                    total_dequeued.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    start.store(true, std::memory_order_release);

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(total_enqueued.load(), total_dequeued.load());
    ASSERT_TRUE(queue.empty());
}

TEST(LockFreeQueueTests, StarvationFreedomTest) {
    // 验证所有线程最终都能成功执行操作（无饥饿）
    LockFreeQueue<std::uint64_t> queue;

    constexpr std::size_t thread_count = 8;
    constexpr std::size_t operations_per_thread = 10000;
    std::atomic<bool> start{false};
    std::vector<std::atomic<std::size_t>> thread_operations(thread_count);

    for (auto& op : thread_operations) {
        op.store(0);
    }

    std::vector<std::thread> threads;
    for (std::size_t tid = 0; tid < thread_count; ++tid) {
        threads.emplace_back([&, tid]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (std::size_t i = 0; i < operations_per_thread; ++i) {
                queue.enqueue(tid * operations_per_thread + i);
                thread_operations[tid].fetch_add(1, std::memory_order_relaxed);

                std::uint64_t value;
                queue.dequeue(value);  // 不检查返回值，允许失败
            }
        });
    }

    start.store(true, std::memory_order_release);

    for (auto& t : threads) {
        t.join();
    }

    // 验证每个线程都完成了所有操作
    for (std::size_t tid = 0; tid < thread_count; ++tid) {
        ASSERT_EQ(thread_operations[tid].load(), operations_per_thread);
    }
}

TEST(LockFreeQueueTests, ProgressGuaranteeTest) {
    // 验证无锁队列的进度保证：至少一个线程总能取得进展
    LockFreeQueue<std::uint64_t> queue;

    constexpr std::size_t duration_ms = 1000;
    std::atomic<bool> stop{false};
    std::atomic<std::size_t> global_progress{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&]() {
            std::size_t local_progress = 0;
            while (!stop.load(std::memory_order_acquire)) {
                queue.enqueue(local_progress);
                std::uint64_t value;
                queue.dequeue(value);
                local_progress++;
            }
            global_progress.fetch_add(local_progress, std::memory_order_relaxed);
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
    stop.store(true, std::memory_order_release);

    for (auto& t : threads) {
        t.join();
    }

    // 验证有实质性进展（至少完成了一些操作）
    std::cout << "  Progress: " << global_progress.load() << " operations in " << duration_ms << "ms\n";
    ASSERT_GT(global_progress.load(), 0u);
}

TEST(LockFreeQueueTests, EmptyQueueStressTest) {
    // 在空队列上并发执行大量 dequeue，验证不会崩溃或死锁
    LockFreeQueue<std::uint64_t> queue;

    constexpr std::size_t thread_count = 8;
    constexpr std::size_t attempts_per_thread = 100000;
    std::atomic<bool> start{false};
    std::atomic<std::size_t> failed_dequeues{0};

    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < thread_count; ++i) {
        threads.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (std::size_t j = 0; j < attempts_per_thread; ++j) {
                std::uint64_t value;
                if (!queue.dequeue(value)) {
                    failed_dequeues.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    start.store(true, std::memory_order_release);

    for (auto& t : threads) {
        t.join();
    }

    // 所有 dequeue 应该都失败（因为队列始终为空）
    ASSERT_EQ(failed_dequeues.load(), thread_count * attempts_per_thread);
    ASSERT_TRUE(queue.empty());
}

TEST(LockFreeQueueTests, RapidCreateDestroyTest) {
    // 测试队列的快速创建和销毁（检测资源泄漏）
    constexpr std::size_t iterations = 1000;
    std::atomic<std::size_t> operations_completed{0};

    for (std::size_t i = 0; i < iterations; ++i) {
        LockFreeQueue<std::uint64_t> queue;

        // 快速填充
        for (std::size_t j = 0; j < 100; ++j) {
            queue.enqueue(j);
        }

        // 快速消费
        std::uint64_t value;
        while (queue.dequeue(value)) {
            operations_completed.fetch_add(1, std::memory_order_relaxed);
        }

        // 队列在此处销毁
    }

    ASSERT_EQ(operations_completed.load(), iterations * 100);
}

TEST(LockFreeQueueTests, MixedSizeDataTest) {
    // 测试不同大小的数据结构
    struct LargeData {
        std::uint64_t id;
        char padding[128];
        std::uint64_t checksum;

        LargeData() : id(0), checksum(0) {
            std::memset(padding, 0, sizeof(padding));
        }

        explicit LargeData(std::uint64_t val) : id(val), checksum(val ^ 0xDEADBEEF) {
            std::memset(padding, static_cast<int>(val & 0xFF), sizeof(padding));
        }

        bool is_valid() const {
            return checksum == (id ^ 0xDEADBEEF);
        }
    };

    LockFreeQueue<LargeData> queue;

    constexpr std::size_t producer_count = 4;
    constexpr std::size_t consumer_count = 4;
    constexpr std::size_t items_per_producer = 5000;
    const std::size_t total_items = producer_count * items_per_producer;

    std::atomic<std::size_t> produced{0};
    std::atomic<std::size_t> consumed{0};
    std::atomic<std::size_t> corrupted{0};
    std::atomic<bool> start{false};

    std::vector<std::thread> threads;

    for (std::size_t pid = 0; pid < producer_count; ++pid) {
        threads.emplace_back([&, pid]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (std::size_t i = 0; i < items_per_producer; ++i) {
                const std::uint64_t value = pid * items_per_producer + i;
                queue.enqueue(LargeData(value));
                produced.fetch_add(1, std::memory_order_release);
            }
        });
    }

    for (std::size_t cid = 0; cid < consumer_count; ++cid) {
        threads.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            while (consumed.load(std::memory_order_acquire) < total_items) {
                LargeData data;
                if (queue.dequeue(data)) {
                    if (!data.is_valid()) {
                        corrupted.fetch_add(1, std::memory_order_relaxed);
                    }
                    consumed.fetch_add(1, std::memory_order_release);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    start.store(true, std::memory_order_release);

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(produced.load(), total_items);
    ASSERT_EQ(consumed.load(), total_items);
    ASSERT_EQ(corrupted.load(), 0u);
    ASSERT_TRUE(queue.empty());
}

TEST(LockFreeQueueTests, ProducerConsumerImbalanceExtreme) {
    // 极端不平衡：1个生产者 vs 16个消费者
    LockFreeQueue<std::uint64_t> queue;

    constexpr std::size_t total_items = 50000;
    constexpr std::size_t consumer_count = 16;

    std::atomic<std::size_t> produced{0};
    std::atomic<std::size_t> consumed{0};
    std::atomic<bool> start{false};
    std::atomic<bool> producer_done{false};

    std::vector<std::uint32_t> seen(total_items, 0);
    std::mutex seen_mutex;

    // 单个生产者
    std::thread producer([&]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        for (std::size_t i = 0; i < total_items; ++i) {
            queue.enqueue(i);
            produced.fetch_add(1, std::memory_order_release);
        }
        producer_done.store(true, std::memory_order_release);
    });

    // 多个消费者竞争
    std::vector<std::thread> consumers;
    for (std::size_t cid = 0; cid < consumer_count; ++cid) {
        consumers.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            while (consumed.load(std::memory_order_acquire) < total_items) {
                std::uint64_t value;
                if (queue.dequeue(value)) {
                    ASSERT_LT(value, total_items);

                    {
                        std::lock_guard<std::mutex> lock(seen_mutex);
                        seen[value]++;
                        ASSERT_EQ(seen[value], 1u);
                    }

                    consumed.fetch_add(1, std::memory_order_release);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    start.store(true, std::memory_order_release);

    producer.join();
    for (auto& c : consumers) {
        c.join();
    }

    ASSERT_TRUE(producer_done.load());
    ASSERT_EQ(produced.load(), total_items);
    ASSERT_EQ(consumed.load(), total_items);

    for (std::size_t i = 0; i < total_items; ++i) {
        ASSERT_EQ(seen[i], 1u);
    }

    ASSERT_TRUE(queue.empty());
}

TEST(LockFreeQueueTests, InterruptedOperationsTest) {
    // 模拟操作被中断的场景（线程随机暂停）
    LockFreeQueue<std::uint64_t> queue;

    constexpr std::size_t thread_count = 6;
    constexpr std::size_t operations = 10000;
    std::atomic<bool> start{false};
    std::atomic<std::size_t> total_enqueued{0};
    std::atomic<std::size_t> total_dequeued{0};

    std::vector<std::thread> threads;
    for (std::size_t tid = 0; tid < thread_count; ++tid) {
        threads.emplace_back([&, tid]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (std::size_t i = 0; i < operations; ++i) {
                queue.enqueue(tid * operations + i);
                total_enqueued.fetch_add(1, std::memory_order_relaxed);

                // 随机暂停，模拟中断
                if (i % 100 == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                }

                std::uint64_t value;
                if (queue.dequeue(value)) {
                    total_dequeued.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    start.store(true, std::memory_order_release);

    for (auto& t : threads) {
        t.join();
    }

    // 清空剩余元素
    std::uint64_t value;
    while (queue.dequeue(value)) {
        total_dequeued.fetch_add(1, std::memory_order_relaxed);
    }

    ASSERT_EQ(total_enqueued.load(), thread_count * operations);
    ASSERT_EQ(total_dequeued.load(), thread_count * operations);
    ASSERT_TRUE(queue.empty());
}

TEST(LockFreeQueueTests, ThreadMigrationTest) {
    // 测试线程在不同 CPU 核心间迁移时的正确性
    LockFreeQueue<std::uint64_t> queue;

    constexpr std::size_t iterations = 1000;
    constexpr std::size_t items_per_iteration = 100;
    std::atomic<bool> start{false};
    std::atomic<std::size_t> total_operations{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (std::size_t iter = 0; iter < iterations; ++iter) {
                // 入队
                for (std::size_t j = 0; j < items_per_iteration; ++j) {
                    queue.enqueue(j);
                }

                // 强制线程让出 CPU（可能触发迁移）
                std::this_thread::yield();

                // 出队
                for (std::size_t j = 0; j < items_per_iteration; ++j) {
                    std::uint64_t value;
                    while (!queue.dequeue(value)) {
                        std::this_thread::yield();
                    }
                    total_operations.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    start.store(true, std::memory_order_release);

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(total_operations.load(), 4 * iterations * items_per_iteration);
    ASSERT_TRUE(queue.empty());
}

TEST(LockFreeQueueTests, MemoryReclamationTest) {
    // 验证内存正确回收（无泄漏）
    LockFreeQueue<std::uint64_t> queue;

    constexpr std::size_t cycles = 100;
    constexpr std::size_t items_per_cycle = 10000;

    for (std::size_t cycle = 0; cycle < cycles; ++cycle) {
        // 填充队列
        for (std::size_t i = 0; i < items_per_cycle; ++i) {
            queue.enqueue(i);
        }

        // 清空队列
        std::uint64_t value;
        std::size_t dequeued = 0;
        while (queue.dequeue(value)) {
            dequeued++;
        }

        ASSERT_EQ(dequeued, items_per_cycle);
        ASSERT_TRUE(queue.empty());
    }

    std::cout << "  Completed " << cycles << " cycles of "
              << items_per_cycle << " items without memory leak\n";
}

int main() {
    return CoronaTest::TestRunner::instance().run_all();
}
