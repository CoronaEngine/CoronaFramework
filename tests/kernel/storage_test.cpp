#include "corona/kernel/utils/storage.h"

#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

#include "../test_framework.h"

using Corona::Kernel::Utils::StorageBuffer;
using namespace CoronaTest;

namespace {

std::atomic<int> g_tracking_live{0};
std::atomic<int> g_tracking_assignments{0};

struct TrackingResource {
    int payload{0};

    TrackingResource() {
        g_tracking_live.fetch_add(1, std::memory_order_relaxed);
    }

    TrackingResource(const TrackingResource& other) : payload(other.payload) {
        g_tracking_live.fetch_add(1, std::memory_order_relaxed);
    }

    TrackingResource(TrackingResource&& other) noexcept : payload(other.payload) {
        g_tracking_live.fetch_add(1, std::memory_order_relaxed);
        other.payload = 0;
    }

    TrackingResource& operator=(const TrackingResource& other) {
        payload = other.payload;
        g_tracking_assignments.fetch_add(1, std::memory_order_relaxed);
        return *this;
    }

    TrackingResource& operator=(TrackingResource&& other) noexcept {
        payload = other.payload;
        g_tracking_assignments.fetch_add(1, std::memory_order_relaxed);
        other.payload = 0;
        return *this;
    }

    ~TrackingResource() {
        g_tracking_live.fetch_sub(1, std::memory_order_relaxed);
    }
};

}  // namespace

TEST(StorageBuffer, DefaultStateIsInvalid) {
    StorageBuffer<int, 2> buffer;

    bool threw = false;
    try {
        buffer.read(0, [](const int&) {});
    } catch (const std::runtime_error&) {
        threw = true;
    }

    ASSERT_TRUE(threw);
}

TEST(StorageBuffer, WriteAndReadSingleSlot) {
    StorageBuffer<int, 4> buffer;

    buffer.write(1, [](int& value) {
        value = 42;
    });

    int read_value = 0;
    buffer.read(1, [&](const int& value) {
        read_value = value;
    });

    ASSERT_EQ(read_value, 42);
}

TEST(StorageBuffer, FreeResetsSlot) {
    StorageBuffer<int, 3> buffer;

    buffer.write(0, [](int& value) {
        value = 99;
    });

    buffer.free(0);

    bool threw = false;
    try {
        buffer.read(0, [](const int&) {});
    } catch (const std::runtime_error&) {
        threw = true;
    }

    ASSERT_TRUE(threw);
}

TEST(StorageBuffer, ForEachReadAndWrite) {
    StorageBuffer<int, 5> buffer;

    buffer.write(0, [](int& value) {
        value = 1;
    });
    buffer.write(2, [](int& value) {
        value = 3;
    });
    buffer.write(4, [](int& value) {
        value = 5;
    });

    int sum = 0;
    buffer.for_each_read([&](const int& value) {
        sum += value;
    });
    ASSERT_EQ(sum, 9);

    buffer.for_each_write([](int& value) {
        value += 10;
    });

    int updated_sum = 0;
    buffer.for_each_read([&](const int& value) {
        updated_sum += value;
    });
    ASSERT_EQ(updated_sum, 39);

    bool still_invalid = false;
    try {
        buffer.read(1, [](const int&) {});
    } catch (const std::runtime_error&) {
        still_invalid = true;
    }
    ASSERT_TRUE(still_invalid);
}

TEST(StorageBuffer, InvalidIndexThrows) {
    StorageBuffer<int, 3> buffer;

    bool write_threw = false;
    try {
        buffer.write(5, [](int& value) {
            value = 1;
        });
    } catch (const std::out_of_range&) {
        write_threw = true;
    }
    ASSERT_TRUE(write_threw);

    bool free_threw = false;
    try {
        buffer.free(4);
    } catch (const std::out_of_range&) {
        free_threw = true;
    }
    ASSERT_TRUE(free_threw);
}

TEST(StorageBuffer, ConcurrentWriteAndRead) {
    constexpr std::size_t kSize = 64;
    StorageBuffer<int, kSize> buffer;

    std::vector<std::thread> writers;
    writers.reserve(kSize);
    for (std::size_t i = 0; i < kSize; ++i) {
        writers.emplace_back([i, &buffer]() {
            buffer.write(i, [i](int& value) {
                value = static_cast<int>(i);
            });
        });
    }

    for (auto& thread : writers) {
        thread.join();
    }

    std::vector<int> collected;
    collected.reserve(kSize);
    buffer.for_each_read([&](const int& value) {
        collected.push_back(value);
    });

    ASSERT_EQ(collected.size(), kSize);

    std::atomic<bool> mismatch{false};
    std::vector<std::thread> readers;
    readers.reserve(kSize);
    for (std::size_t i = 0; i < kSize; ++i) {
        readers.emplace_back([i, &buffer, &mismatch]() {
            try {
                buffer.read(i, [i, &mismatch](const int& value) {
                    if (value != static_cast<int>(i)) {
                        mismatch.store(true);
                    }
                });
            } catch (...) {
                mismatch.store(true);
            }
        });
    }

    for (auto& thread : readers) {
        thread.join();
    }

    ASSERT_FALSE(mismatch.load());
}

TEST(StorageBuffer, ConcurrentMixedOperations) {
    constexpr std::size_t kSize = 32;
    constexpr std::size_t kIterations = 256;
    constexpr std::size_t kThreadCount = 5;

    StorageBuffer<int, kSize> buffer;

    std::atomic<bool> unexpected_failure{false};
    std::atomic<size_t> total_writes{0};
    std::atomic<size_t> total_reads{0};
    std::atomic<size_t> total_frees{0};
    std::atomic<size_t> total_traversals{0};

    std::array<std::atomic<int>, kSize> last_written{};
    for (auto& entry : last_written) {
        entry.store(-1, std::memory_order_relaxed);
    }

    std::barrier sync_point(static_cast<std::ptrdiff_t>(kThreadCount));

    auto writer = [&]() {
        sync_point.arrive_and_wait();
        for (std::size_t iter = 0; iter < kIterations; ++iter) {
            const std::size_t index = (iter * 7) % kSize;
            try {
                buffer.write(index, [&, index, iter](int& value) {
                    value = static_cast<int>(iter);
                    last_written[index].store(static_cast<int>(iter), std::memory_order_relaxed);
                });
                total_writes.fetch_add(1, std::memory_order_relaxed);
            } catch (...) {
                unexpected_failure.store(true, std::memory_order_relaxed);
                return;
            }
            std::this_thread::yield();
        }
    };

    auto reader = [&]() {
        sync_point.arrive_and_wait();
        for (std::size_t iter = 0; iter < kIterations * 2; ++iter) {
            const std::size_t index = (iter * 5) % kSize;
            try {
                buffer.read(index, [&, index](const int& value) {
                    if (value < -1) {
                        unexpected_failure.store(true, std::memory_order_relaxed);
                    }
                    const int expected_min = last_written[index].load(std::memory_order_relaxed);
                    if (expected_min >= 0 && value < expected_min) {
                        unexpected_failure.store(true, std::memory_order_relaxed);
                    }
                    total_reads.fetch_add(1, std::memory_order_relaxed);
                });
            } catch (const std::runtime_error&) {
                // Slot might have been freed concurrently; that is expected.
            } catch (...) {
                unexpected_failure.store(true, std::memory_order_relaxed);
                return;
            }
            std::this_thread::yield();
        }
    };

    auto traverser = [&]() {
        sync_point.arrive_and_wait();
        for (std::size_t iter = 0; iter < kIterations; ++iter) {
            int running_sum = 0;
            buffer.for_each_read([&](const int& value) {
                running_sum += value;
            });
            if (running_sum < 0) {
                unexpected_failure.store(true, std::memory_order_relaxed);
            }
            total_traversals.fetch_add(1, std::memory_order_relaxed);
            buffer.for_each_write([](int& value) {
                value += 1;
            });
            std::this_thread::yield();
        }
    };

    auto freer = [&]() {
        sync_point.arrive_and_wait();
        for (std::size_t iter = 0; iter < kIterations; ++iter) {
            const std::size_t index = (iter * 11) % kSize;
            buffer.free(index);
            last_written[index].store(-1, std::memory_order_relaxed);
            total_frees.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    };

    std::thread writer_thread_a(writer);
    std::thread writer_thread_b(writer);
    std::thread reader_thread(reader);
    std::thread traverser_thread(traverser);
    std::thread freer_thread(freer);

    writer_thread_a.join();
    writer_thread_b.join();
    reader_thread.join();
    traverser_thread.join();
    freer_thread.join();

    ASSERT_FALSE(unexpected_failure.load(std::memory_order_relaxed));
    ASSERT_GT(total_writes.load(std::memory_order_relaxed), 0u);
    ASSERT_GT(total_reads.load(std::memory_order_relaxed), 0u);
    ASSERT_GT(total_frees.load(std::memory_order_relaxed), 0u);
    ASSERT_GT(total_traversals.load(std::memory_order_relaxed), 0u);

    std::size_t remaining_valid = 0;
    buffer.for_each_read([&](const int&) {
        ++remaining_valid;
    });
    ASSERT_LE(remaining_valid, kSize);
}

TEST(StorageBuffer, HandlesFullCapacityContention) {
    constexpr std::size_t kSize = 16;
    constexpr std::size_t kIterations = 256;
    constexpr std::size_t kThreadCount = 4;

    StorageBuffer<int, kSize> buffer;

    int base_sum = 0;
    for (std::size_t i = 0; i < kSize; ++i) {
        buffer.write(i, [i](int& value) {
            value = static_cast<int>(i);
        });
        base_sum += static_cast<int>(i);
    }

    std::atomic<bool> unexpected_failure{false};
    std::barrier start(static_cast<std::ptrdiff_t>(kThreadCount));

    std::vector<std::thread> workers;
    workers.reserve(kThreadCount);

    for (std::size_t t = 0; t < kThreadCount; ++t) {
        workers.emplace_back([&, t]() {
            start.arrive_and_wait();
            for (std::size_t iter = 0; iter < kIterations; ++iter) {
                const std::size_t index = (iter + t) % kSize;
                try {
                    buffer.write(index, [&](int& value) {
                        value += 1;
                        std::this_thread::sleep_for(std::chrono::microseconds(10));
                    });
                } catch (...) {
                    unexpected_failure.store(true, std::memory_order_relaxed);
                    return;
                }
            }
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }

    ASSERT_FALSE(unexpected_failure.load(std::memory_order_relaxed));

    int observed_sum = 0;
    buffer.for_each_read([&](const int& value) {
        observed_sum += value;
    });

    const int expected_sum = base_sum + static_cast<int>(kThreadCount * kIterations);
    ASSERT_EQ(observed_sum, expected_sum);
}

TEST(StorageBuffer, LongRunningStressWithRandomOps) {
    constexpr std::size_t kSize = 16;
    constexpr std::size_t kThreadCount = 6;
    constexpr std::size_t kIterations = 20000;

    StorageBuffer<int, kSize> buffer;

    std::atomic<bool> failure{false};
    std::atomic<size_t> successful_reads{0};
    std::atomic<size_t> successful_writes{0};
    std::atomic<size_t> successful_frees{0};
    std::atomic<size_t> traversals{0};

    std::barrier start(static_cast<std::ptrdiff_t>(kThreadCount));

    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);

    for (std::size_t t = 0; t < kThreadCount; ++t) {
        threads.emplace_back([&, t]() {
            std::mt19937 rng(static_cast<unsigned int>(t + 1337));
            std::uniform_int_distribution<int> op_dist(0, 3);
            std::uniform_int_distribution<int> index_dist(0, static_cast<int>(kSize - 1));
            std::uniform_int_distribution<int> sleep_dist(0, 5);

            start.arrive_and_wait();

            for (std::size_t iter = 0; iter < kIterations; ++iter) {
                const int index = index_dist(rng);
                const int op = op_dist(rng);

                try {
                    switch (op) {
                        case 0: {
                            buffer.write(static_cast<std::size_t>(index), [&](int& value) {
                                value = static_cast<int>(iter + t);
                            });
                            successful_writes.fetch_add(1, std::memory_order_relaxed);
                            break;
                        }
                        case 1: {
                            try {
                                buffer.read(static_cast<std::size_t>(index), [&](const int&) {
                                    successful_reads.fetch_add(1, std::memory_order_relaxed);
                                });
                            } catch (const std::runtime_error&) {
                                // Slot invalidated by concurrent free; acceptable.
                            }
                            break;
                        }
                        case 2: {
                            buffer.free(static_cast<std::size_t>(index));
                            successful_frees.fetch_add(1, std::memory_order_relaxed);
                            break;
                        }
                        case 3: {
                            buffer.for_each_read([&](const int&) {
                                // no-op, just ensure traversal works under contention
                            });
                            buffer.for_each_write([](int&) {
                                // intentionally touch write path under contention
                            });
                            traversals.fetch_add(1, std::memory_order_relaxed);
                            break;
                        }
                    }
                } catch (...) {
                    failure.store(true, std::memory_order_relaxed);
                    return;
                }

                if (sleep_dist(rng) == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(50));
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    ASSERT_FALSE(failure.load(std::memory_order_relaxed));
    ASSERT_GT(successful_writes.load(std::memory_order_relaxed), 0u);
    ASSERT_GT(successful_frees.load(std::memory_order_relaxed), 0u);
    ASSERT_GT(traversals.load(std::memory_order_relaxed), 0u);
}

TEST(StorageBuffer, WriterExceptionLeavesSlotInvalid) {
    StorageBuffer<int, 1> buffer;

    bool write_threw = false;
    try {
        buffer.write(0, [](int& value) {
            value = 123;
            throw std::runtime_error("writer failure");
        });
    } catch (const std::runtime_error&) {
        write_threw = true;
    }

    ASSERT_TRUE(write_threw);

    bool read_still_invalid = false;
    try {
        buffer.read(0, [](const int&) {});
    } catch (const std::runtime_error&) {
        read_still_invalid = true;
    }

    ASSERT_TRUE(read_still_invalid);
}

TEST(StorageBuffer, ReaderExceptionKeepsSlotValid) {
    StorageBuffer<int, 1> buffer;

    buffer.write(0, [](int& value) {
        value = 42;
    });

    bool read_threw = false;
    try {
        buffer.read(0, [](const int&) {
            throw std::runtime_error("reader failure");
        });
    } catch (const std::runtime_error&) {
        read_threw = true;
    }

    ASSERT_TRUE(read_threw);

    int observed = 0;
    buffer.read(0, [&](const int& value) {
        observed = value;
    });

    ASSERT_EQ(observed, 42);
}

TEST(StorageBuffer, ManagesNonTrivialTypes) {
    g_tracking_live.store(0, std::memory_order_relaxed);
    g_tracking_assignments.store(0, std::memory_order_relaxed);

    {
        StorageBuffer<TrackingResource, 3> buffer;

        ASSERT_EQ(g_tracking_live.load(std::memory_order_relaxed), 3);

        buffer.write(0, [](TrackingResource& res) {
            ASSERT_EQ(res.payload, 0);
            res.payload = 7;
        });

        buffer.write(1, [](TrackingResource& res) {
            res.payload = 11;
        });

        int sum = 0;
        buffer.for_each_read([&](const TrackingResource& res) {
            sum += res.payload;
        });
        ASSERT_EQ(sum, 18);

        buffer.free(0);
        ASSERT_GE(g_tracking_assignments.load(std::memory_order_relaxed), 1);

        buffer.write(0, [](TrackingResource& res) {
            ASSERT_EQ(res.payload, 0);
            res.payload = 5;
        });

        int new_sum = 0;
        buffer.for_each_read([&](const TrackingResource& res) {
            new_sum += res.payload;
        });
        ASSERT_EQ(new_sum, 16);
    }

    ASSERT_EQ(g_tracking_live.load(std::memory_order_relaxed), 0);
}

TEST(StorageBuffer, PerformanceSequentialAccess) {
    constexpr std::size_t kSize = 128;
    constexpr std::size_t kIterations = 200000;

    StorageBuffer<int, kSize> buffer;
    for (std::size_t i = 0; i < kSize; ++i) {
        buffer.write(i, [i](int& value) {
            value = static_cast<int>(i);
        });
    }

    auto start = std::chrono::steady_clock::now();

    for (std::size_t iter = 0; iter < kIterations; ++iter) {
        const std::size_t index = iter % kSize;
        buffer.write(index, [iter](int& value) {
            value = static_cast<int>(iter);
        });
        buffer.read(index, [&](const int& value) {
            ASSERT_EQ(value, static_cast<int>(iter));
        });
    }

    auto end = std::chrono::steady_clock::now();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    ASSERT_LT(elapsed_ms, 2000);  // Guard against performance regressions.
}

TEST(StorageBuffer, PerformanceMultiThreadedAccess) {
    constexpr std::size_t kSize = 128;
    constexpr std::size_t kThreadCount = 8;
    constexpr std::size_t kIterations = 50000;

    StorageBuffer<int, kSize> buffer;
    for (std::size_t i = 0; i < kSize; ++i) {
        buffer.write(i, [](int& value) {
            value = 0;
        });
    }

    std::atomic<bool> failure{false};
    std::atomic<size_t> completed_reads{0};

    std::chrono::steady_clock::time_point start_time{};
    std::barrier sync_point(static_cast<std::ptrdiff_t>(kThreadCount), [&]() noexcept {
        start_time = std::chrono::steady_clock::now();
    });

    std::vector<std::thread> workers;
    workers.reserve(kThreadCount);

    for (std::size_t thread_id = 0; thread_id < kThreadCount; ++thread_id) {
        workers.emplace_back([&, thread_id]() {
            sync_point.arrive_and_wait();

            for (std::size_t iter = 0; iter < kIterations; ++iter) {
                const std::size_t index = (iter + thread_id * 7) % kSize;

                try {
                    buffer.write(index, [thread_id, iter](int& value) {
                        value = static_cast<int>((thread_id << 16) | (iter & 0xFFFF));
                    });
                } catch (...) {
                    failure.store(true, std::memory_order_relaxed);
                    return;
                }

                try {
                    buffer.read(index, [&](const int& value) {
                        (void)value;
                        completed_reads.fetch_add(1, std::memory_order_relaxed);
                    });
                } catch (...) {
                    failure.store(true, std::memory_order_relaxed);
                    return;
                }
            }
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }

    const auto end_time = std::chrono::steady_clock::now();

    ASSERT_FALSE(failure.load(std::memory_order_relaxed));
    ASSERT_EQ(completed_reads.load(std::memory_order_relaxed), kThreadCount * kIterations);

    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    ASSERT_LT(elapsed_ms, 3000);  // Ensure multi-threaded path stays performant.
}

TEST(StorageBuffer, SizeReflectsCapacity) {
    StorageBuffer<int, 7> buffer;
    ASSERT_EQ(buffer.size(), 7u);
}

int main() {
    return CoronaTest::TestRunner::instance().run_all();
}