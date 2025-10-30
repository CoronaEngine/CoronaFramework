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

int main() {
    return CoronaTest::TestRunner::instance().run_all();
}
