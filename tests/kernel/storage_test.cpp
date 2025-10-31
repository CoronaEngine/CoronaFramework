#include "corona/kernel/utils/storage.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "../test_framework.h"

using Corona::Kernel::Utils::Storage;

TEST(StorageTests, BasicAllocateAndAccess) {
    Storage<int, 8> storage;

    // 分配并初始化
    auto handle = storage.allocate([](int& value) { value = 42; });
    ASSERT_TRUE(handle.has_value());

    // 只读访问
    bool read_success = storage.access(*handle, [](const int& value) {
        ASSERT_EQ(value, 42);
    });
    ASSERT_TRUE(read_success);

    // 可写访问
    bool write_success = storage.access_mut(*handle, [](int& value) {
        value = 100;
    });
    ASSERT_TRUE(write_success);

    // 验证修改
    storage.access(*handle, [](const int& value) {
        ASSERT_EQ(value, 100);
    });

    // 释放
    storage.deallocate(*handle);

    // 验证释放后无法访问
    bool access_after_free = storage.access(*handle, [](const int&) {});
    ASSERT_FALSE(access_after_free);
}

TEST(StorageTests, MultipleAllocations) {
    Storage<int, 8> storage;
    std::vector<Storage<int, 8>::Handle> handles;

    // 分配多个槽位
    for (int i = 0; i < 8; ++i) {
        auto handle = storage.allocate([i](int& value) { value = i * 10; });
        ASSERT_TRUE(handle.has_value());
        handles.push_back(*handle);
    }

    // 验证所有值
    for (int i = 0; i < 8; ++i) {
        storage.access(handles[i], [i](const int& value) {
            ASSERT_EQ(value, i * 10);
        });
    }

    // 释放所有槽位
    for (const auto& handle : handles) {
        storage.deallocate(handle);
    }

    // 验证可以重新分配
    auto recycled = storage.allocate([](int& value) { value = 999; });
    ASSERT_TRUE(recycled.has_value());
    storage.access(*recycled, [](const int& value) {
        ASSERT_EQ(value, 999);
    });
}

TEST(StorageTests, AutoExpansion) {
    static constexpr std::size_t initial_capacity = 8;
    Storage<int, initial_capacity> storage;
    std::vector<Storage<int, initial_capacity>::Handle> handles;

    // 分配超过初始容量的槽位，触发自动扩容
    const std::size_t total_allocations = initial_capacity * 3;
    for (std::size_t i = 0; i < total_allocations; ++i) {
        auto handle = storage.allocate([i](int& value) { value = static_cast<int>(i); });
        ASSERT_TRUE(handle.has_value());
        handles.push_back(*handle);
    }

    // 验证所有值
    for (std::size_t i = 0; i < total_allocations; ++i) {
        storage.access(handles[i], [i](const int& value) {
            ASSERT_EQ(value, static_cast<int>(i));
        });
    }

    // 释放所有槽位
    for (const auto& handle : handles) {
        storage.deallocate(handle);
    }
}

TEST(StorageTests, ForEachTraversal) {
    Storage<int, 16> storage;
    std::vector<Storage<int, 16>::Handle> handles;

    // 分配一些槽位
    for (int i = 0; i < 10; ++i) {
        auto handle = storage.allocate([i](int& value) { value = i + 1; });
        ASSERT_TRUE(handle.has_value());
        handles.push_back(*handle);
    }

    // 只读遍历并统计
    int sum = 0;
    int count = 0;
    storage.for_each_const([&](const int& value) {
        sum += value;
        ++count;
    });

    ASSERT_EQ(count, 10);
    ASSERT_EQ(sum, 55);  // 1+2+...+10 = 55

    // 可写遍历，所有值乘以2
    storage.for_each([](int& value) { value *= 2; });

    // 验证修改
    sum = 0;
    storage.for_each_const([&](const int& value) {
        sum += value;
    });
    ASSERT_EQ(sum, 110);  // 2*(1+2+...+10) = 110

    // 清理
    for (const auto& handle : handles) {
        storage.deallocate(handle);
    }
}

TEST(StorageTests, ConcurrentAllocateAndDeallocate) {
    static constexpr std::size_t capacity = 32;
    Storage<int, capacity> storage;

    std::atomic<bool> start{false};
    std::atomic<std::size_t> allocations{0};
    std::atomic<std::size_t> deallocations{0};

    auto worker = [&]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        for (int iteration = 0; iteration < 500; ++iteration) {
            auto handle = storage.allocate([iteration](int& value) {
                value = iteration;
            });

            if (handle.has_value()) {
                allocations.fetch_add(1, std::memory_order_relaxed);

                // 验证读取
                bool read_ok = storage.access(*handle, [iteration](const int& value) {
                    ASSERT_EQ(value, iteration);
                });
                ASSERT_TRUE(read_ok);

                // 释放
                storage.deallocate(*handle);
                deallocations.fetch_add(1, std::memory_order_relaxed);
            } else {
                std::this_thread::yield();
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back(worker);
    }

    start.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    ASSERT_EQ(allocations.load(), deallocations.load());
    ASSERT_GT(allocations.load(), 0U);
}

TEST(StorageTests, MixedConcurrentOperations) {
    static constexpr std::size_t capacity = 64;
    Storage<int, capacity> storage;

    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<std::size_t> allocations{0};
    std::atomic<std::size_t> reads{0};
    std::atomic<std::size_t> writes{0};

    std::vector<std::thread> threads;

    // 生产者线程：分配、修改、释放
    auto producer = [&]() {
        std::vector<Storage<int, capacity>::Handle> owned;
        owned.reserve(capacity / 2);

        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!stop.load(std::memory_order_relaxed)) {
            if (auto handle = storage.allocate([](int& value) { value = 1; })) {
                owned.push_back(*handle);
                allocations.fetch_add(1, std::memory_order_relaxed);
            }

            if (!owned.empty() && owned.size() > capacity / 4) {
                const auto& handle = owned.back();
                bool wrote = storage.access_mut(handle, [&](int& value) {
                    value += 1;
                    writes.fetch_add(1, std::memory_order_relaxed);
                });

                if (wrote) {
                    storage.deallocate(handle);
                    owned.pop_back();
                }
            }

            std::this_thread::yield();
        }

        // 清理剩余句柄
        for (const auto& handle : owned) {
            storage.deallocate(handle);
        }
    };

    // 读者线程：遍历读取
    auto reader = [&]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!stop.load(std::memory_order_relaxed)) {
            storage.for_each_const([&](const int& value) {
                ASSERT_GE(value, 1);
                reads.fetch_add(1, std::memory_order_relaxed);
            });
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

    ASSERT_GT(allocations.load(), 0U);
    ASSERT_GT(reads.load(), 0U);
    ASSERT_GT(writes.load(), 0U);
}

TEST(StorageTests, ExceptionSafety) {
    Storage<int, 8> storage;

    // 分配时抛异常
    bool exception_caught = false;
    try {
        storage.allocate([](int&) {
            throw std::runtime_error("Test exception");
        });
    } catch (const std::runtime_error&) {
        exception_caught = true;
    }
    ASSERT_TRUE(exception_caught);

    // 验证可以继续正常分配
    auto handle = storage.allocate([](int& value) { value = 42; });
    ASSERT_TRUE(handle.has_value());
    storage.access(*handle, [](const int& value) {
        ASSERT_EQ(value, 42);
    });
    storage.deallocate(*handle);

    // access_mut 时抛异常
    handle = storage.allocate([](int& value) { value = 100; });
    ASSERT_TRUE(handle.has_value());

    exception_caught = false;
    try {
        storage.access_mut(*handle, [](int&) {
            throw std::runtime_error("Access exception");
        });
    } catch (const std::runtime_error&) {
        exception_caught = true;
    }
    ASSERT_TRUE(exception_caught);

    // 验证槽位仍可访问（数据可能处于部分修改状态）
    bool still_accessible = storage.access(*handle, [](const int&) {});
    ASSERT_TRUE(still_accessible);

    storage.deallocate(*handle);
}

TEST(StorageTests, ComprehensiveConcurrentOperations) {
    static constexpr std::size_t capacity = 128;
    Storage<int, capacity> storage;

    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<std::size_t> allocations{0};
    std::atomic<std::size_t> deallocations{0};
    std::atomic<std::size_t> reads{0};
    std::atomic<std::size_t> writes{0};
    std::atomic<std::size_t> iterations{0};

    std::vector<std::thread> threads;

    // 分配器线程：持续分配新槽位
    auto allocator = [&]() {
        std::vector<Storage<int, capacity>::Handle> owned;
        owned.reserve(capacity);

        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!stop.load(std::memory_order_relaxed)) {
            // 分配新槽位
            if (auto handle = storage.allocate([](int& value) { value = 0; })) {
                owned.push_back(*handle);
                allocations.fetch_add(1, std::memory_order_relaxed);
            }

            // 控制持有数量，避免耗尽所有句柄
            if (owned.size() > capacity / 2) {
                storage.deallocate(owned.front());
                owned.erase(owned.begin());
                deallocations.fetch_add(1, std::memory_order_relaxed);
            }

            std::this_thread::yield();
        }

        // 清理剩余句柄
        for (const auto& handle : owned) {
            storage.deallocate(handle);
            deallocations.fetch_add(1, std::memory_order_relaxed);
        }
    };

    // 释放器线程：随机释放现有槽位
    auto deallocator = [&]() {
        std::vector<Storage<int, capacity>::Handle> to_deallocate;
        to_deallocate.reserve(capacity / 4);

        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!stop.load(std::memory_order_relaxed)) {
            // 尝试分配一批句柄用于后续释放
            for (int i = 0; i < 10 && to_deallocate.size() < capacity / 4; ++i) {
                if (auto handle = storage.allocate([i](int& value) { value = i; })) {
                    to_deallocate.push_back(*handle);
                    allocations.fetch_add(1, std::memory_order_relaxed);
                }
            }

            // 释放一半
            while (to_deallocate.size() > capacity / 8) {
                storage.deallocate(to_deallocate.back());
                to_deallocate.pop_back();
                deallocations.fetch_add(1, std::memory_order_relaxed);
            }

            std::this_thread::yield();
        }

        // 清理剩余
        for (const auto& handle : to_deallocate) {
            storage.deallocate(handle);
            deallocations.fetch_add(1, std::memory_order_relaxed);
        }
    };

    // 读者线程：随机读取槽位
    auto reader = [&]() {
        std::vector<Storage<int, capacity>::Handle> cached_handles;
        cached_handles.reserve(capacity / 4);

        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!stop.load(std::memory_order_relaxed)) {
            // 获取一些句柄进行读取
            for (int i = 0; i < 5; ++i) {
                if (auto handle = storage.allocate([](int& value) { value = 999; })) {
                    cached_handles.push_back(*handle);
                }
            }

            // 读取这些句柄
            for (const auto& handle : cached_handles) {
                bool read_ok = storage.access(handle, [&](const int& value) {
                    (void)value;
                    reads.fetch_add(1, std::memory_order_relaxed);
                });
                (void)read_ok;
            }

            // 释放并清空
            for (const auto& handle : cached_handles) {
                storage.deallocate(handle);
            }
            cached_handles.clear();

            std::this_thread::yield();
        }
    };

    // 写者线程：随机修改槽位
    auto writer = [&]() {
        std::vector<Storage<int, capacity>::Handle> cached_handles;
        cached_handles.reserve(capacity / 4);

        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!stop.load(std::memory_order_relaxed)) {
            // 获取一些句柄进行写入
            for (int i = 0; i < 5; ++i) {
                if (auto handle = storage.allocate([](int& value) { value = 0; })) {
                    cached_handles.push_back(*handle);
                }
            }

            // 修改这些句柄
            for (const auto& handle : cached_handles) {
                bool write_ok = storage.access_mut(handle, [&](int& value) {
                    value += 1;
                    writes.fetch_add(1, std::memory_order_relaxed);
                });
                (void)write_ok;
            }

            // 释放并清空
            for (const auto& handle : cached_handles) {
                storage.deallocate(handle);
            }
            cached_handles.clear();

            std::this_thread::yield();
        }
    };

    // 迭代器线程：遍历所有槽位
    auto iterator = [&]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!stop.load(std::memory_order_relaxed)) {
            // 只读遍历
            storage.for_each_const([&](const int& value) {
                (void)value;
            });

            // 可写遍历
            storage.for_each([&](int& value) {
                value = (value + 1) % 1000;
            });

            iterations.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    };

    // 启动所有线程
    threads.emplace_back(allocator);
    threads.emplace_back(allocator);
    threads.emplace_back(deallocator);
    threads.emplace_back(reader);
    threads.emplace_back(reader);
    threads.emplace_back(writer);
    threads.emplace_back(writer);
    threads.emplace_back(iterator);

    start.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    stop.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    // 验证统计数据
    ASSERT_EQ(allocations.load(), deallocations.load());
    ASSERT_GT(allocations.load(), 0U);
    ASSERT_GT(reads.load(), 0U);
    ASSERT_GT(writes.load(), 0U);
    ASSERT_GT(iterations.load(), 0U);

    std::cout << "Comprehensive test results:\n";
    std::cout << "  Allocations: " << allocations.load() << "\n";
    std::cout << "  Deallocations: " << deallocations.load() << "\n";
    std::cout << "  Reads: " << reads.load() << "\n";
    std::cout << "  Writes: " << writes.load() << "\n";
    std::cout << "  Iterations: " << iterations.load() << "\n";
}

int main() {
    return CoronaTest::TestRunner::instance().run_all();
}
