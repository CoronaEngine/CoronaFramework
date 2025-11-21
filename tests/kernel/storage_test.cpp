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
    ASSERT_TRUE(handle > 0);

    // 只读访问
    {
        auto accessor = storage.acquire_read(handle);
        ASSERT_TRUE(accessor.valid());
        ASSERT_EQ(*accessor, 42);
    }

    // 可写访问
    {
        auto accessor = storage.acquire_write(handle);
        ASSERT_TRUE(accessor.valid());
        *accessor = 100;
    }

    // 验证修改
    {
        auto accessor = storage.acquire_read(handle);
        ASSERT_TRUE(accessor.valid());
        ASSERT_EQ(*accessor, 100);
    }

    // 释放
    storage.deallocate(handle);

    // 验证释放后无法访问
    {
        auto accessor = storage.acquire_read(handle);
        ASSERT_FALSE(accessor.valid());
    }
}

TEST(StorageTests, MultipleAllocations) {
    Storage<int, 8> storage;
    std::vector<Storage<int, 8>::ObjectId> handles;

    // 分配多个槽位
    for (int i = 0; i < 8; ++i) {
        auto handle = storage.allocate([i](int& value) { value = i * 10; });
        ASSERT_TRUE(handle > 0);
        handles.push_back(handle);
    }

    // 验证所有值
    for (int i = 0; i < 8; ++i) {
        auto accessor = storage.acquire_read(handles[i]);
        ASSERT_TRUE(accessor.valid());
        ASSERT_EQ(*accessor, i * 10);
    }

    // 释放所有槽位
    for (const auto& handle : handles) {
        storage.deallocate(handle);
    }

    // 验证可以重新分配
    auto recycled = storage.allocate([](int& value) { value = 999; });
    ASSERT_TRUE(recycled > 0);
    {
        auto accessor = storage.acquire_read(recycled);
        ASSERT_TRUE(accessor.valid());
        ASSERT_EQ(*accessor, 999);
    }
}

TEST(StorageTests, AutoExpansion) {
    static constexpr std::size_t initial_capacity = 8;
    Storage<int, initial_capacity> storage;
    std::vector<Storage<int, initial_capacity>::ObjectId> handles;

    // 分配超过初始容量的槽位，触发自动扩容
    const std::size_t total_allocations = initial_capacity * 3;
    for (std::size_t i = 0; i < total_allocations; ++i) {
        auto handle = storage.allocate([i](int& value) { value = static_cast<int>(i); });
        ASSERT_TRUE(handle > 0);
        handles.push_back(handle);
    }

    // 验证所有值
    for (std::size_t i = 0; i < total_allocations; ++i) {
        auto accessor = storage.acquire_read(handles[i]);
        ASSERT_TRUE(accessor.valid());
        ASSERT_EQ(*accessor, static_cast<int>(i));
    }

    // 释放所有槽位
    for (const auto& handle : handles) {
        storage.deallocate(handle);
    }
}

TEST(StorageTests, ForEachTraversal) {
    Storage<int, 16> storage;
    std::vector<Storage<int, 16>::ObjectId> handles;

    // 分配一些槽位
    for (int i = 0; i < 10; ++i) {
        auto handle = storage.allocate([i](int& value) { value = i + 1; });
        ASSERT_TRUE(handle > 0);
        handles.push_back(handle);
    }

    // 只读遍历并统计
    int sum = 0;
    int count = 0;
    storage.for_each_read([&](const int& value) {
        sum += value;
        ++count;
    });

    ASSERT_EQ(count, 10);
    ASSERT_EQ(sum, 55);  // 1+2+...+10 = 55

    // 可写遍历，所有值乘以2
    storage.for_each_write([](int& value) { value *= 2; });

    // 验证修改
    sum = 0;
    storage.for_each_read([&](const int& value) {
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

            if (handle > 0) {
                allocations.fetch_add(1, std::memory_order_relaxed);

                // 验证读取
                {
                    auto accessor = storage.acquire_read(handle);
                    if (accessor) {
                        ASSERT_EQ(*accessor, iteration);
                    }
                }

                // 释放
                storage.deallocate(handle);
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
        std::vector<Storage<int, capacity>::ObjectId> owned;
        owned.reserve(capacity / 2);

        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!stop.load(std::memory_order_relaxed)) {
            if (auto handle = storage.allocate([](int& value) { value = 1; })) {
                owned.push_back(handle);
                allocations.fetch_add(1, std::memory_order_relaxed);
            }

            if (!owned.empty() && owned.size() > capacity / 4) {
                const auto& handle = owned.back();
                bool wrote = false;
                {
                    auto accessor = storage.acquire_write(handle);
                    if (accessor) {
                        *accessor += 1;
                        writes.fetch_add(1, std::memory_order_relaxed);
                        wrote = true;
                    }
                }

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
            storage.for_each_read([&](const int& value) {
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
    ASSERT_TRUE(handle > 0);
    {
        auto accessor = storage.acquire_read(handle);
        ASSERT_TRUE(accessor.valid());
        ASSERT_EQ(*accessor, 42);
    }
    storage.deallocate(handle);

    // write 时抛异常
    handle = storage.allocate([](int& value) { value = 100; });
    ASSERT_TRUE(handle > 0);

    exception_caught = false;
    try {
        auto accessor = storage.acquire_write(handle);
        if (accessor) {
            throw std::runtime_error("Access exception");
        }
    } catch (const std::runtime_error&) {
        exception_caught = true;
    }
    ASSERT_TRUE(exception_caught);

    // 验证槽位仍可访问（数据可能处于部分修改状态）
    {
        auto accessor = storage.acquire_read(handle);
        ASSERT_TRUE(accessor.valid());
    }

    storage.deallocate(handle);
}

TEST(StorageTests, HandleLifetimeManagement) {
    Storage<int, 8> storage;

    // 测试句柄的 RAII 特性
    auto handle = storage.allocate([](int& value) { value = 42; });
    ASSERT_TRUE(handle > 0);

    // 读句柄在作用域内有效
    {
        auto read_accessor = storage.acquire_read(handle);
        ASSERT_TRUE(read_accessor.valid());
        ASSERT_EQ(*read_accessor, 42);

        // 可以同时持有多个读句柄
        auto read_accessor2 = storage.acquire_read(handle);
        ASSERT_TRUE(read_accessor2.valid());
        ASSERT_EQ(*read_accessor2, 42);
    }

    // 写句柄独占访问
    {
        auto write_accessor = storage.acquire_write(handle);
        ASSERT_TRUE(write_accessor.valid());
        *write_accessor = 100;
    }

    // 验证修改生效
    {
        auto read_accessor = storage.acquire_read(handle);
        ASSERT_TRUE(read_accessor.valid());
        ASSERT_EQ(*read_accessor, 100);
    }

    storage.deallocate(handle);
}

TEST(StorageTests, HandleMoveSemantics) {
    Storage<int, 8> storage;
    auto handle = storage.allocate([](int& value) { value = 42; });

    {  // 测试读句柄的移动语义
        auto read_accessor1 = storage.acquire_read(handle);
        ASSERT_TRUE(read_accessor1.valid());

        auto read_accessor2 = std::move(read_accessor1);
        ASSERT_TRUE(read_accessor2.valid());
        ASSERT_EQ(*read_accessor2, 42);
    }

    {  // 测试写句柄的移动语义
        auto write_accessor1 = storage.acquire_write(handle);
        ASSERT_TRUE(write_accessor1.valid());

        auto write_accessor2 = std::move(write_accessor1);
        ASSERT_TRUE(write_accessor2.valid());
        *write_accessor2 = 100;
    }

    storage.deallocate(handle);
}

TEST(StorageTests, InvalidHandleAccess) {
    Storage<int, 8> storage;

    // 测试无效句柄（0）
    {
        auto accessor = storage.acquire_read(0);
        ASSERT_FALSE(accessor.valid());
    }

    {
        auto accessor = storage.acquire_write(0);
        ASSERT_FALSE(accessor.valid());
    }

    // 测试未分配的句柄
    Storage<int, 8>::ObjectId fake_handle = 0x123456;
    {
        auto accessor = storage.acquire_read(fake_handle);
        ASSERT_FALSE(accessor.valid());
    }

    {
        auto accessor = storage.acquire_write(fake_handle);
        ASSERT_FALSE(accessor.valid());
    }
}

TEST(StorageTests, CapacityBoundaryTest) {
    static constexpr std::size_t capacity = 4;
    Storage<int, capacity, 1> storage;

    // 初始容量应为 4
    ASSERT_EQ(storage.capacity(), capacity);
    ASSERT_EQ(storage.count(), 0U);
    ASSERT_TRUE(storage.empty());

    // 填满第一个 buffer
    std::vector<Storage<int, capacity, 1>::ObjectId> handles;
    for (std::size_t i = 0; i < capacity; ++i) {
        auto handle = storage.allocate([i](int& value) { value = static_cast<int>(i); });
        ASSERT_TRUE(handle > 0);
        handles.push_back(handle);
    }

    ASSERT_EQ(storage.count(), capacity);
    ASSERT_FALSE(storage.empty());

    // 分配第 capacity+1 个对象，触发扩容
    auto overflow_handle = storage.allocate([](int& value) { value = 999; });
    ASSERT_TRUE(overflow_handle > 0);
    ASSERT_EQ(storage.capacity(), capacity * 2);  // 应该扩容到 8
    ASSERT_EQ(storage.count(), capacity + 1);

    // 验证所有值
    for (std::size_t i = 0; i < capacity; ++i) {
        auto accessor = storage.acquire_read(handles[i]);
        ASSERT_TRUE(accessor.valid());
        ASSERT_EQ(*accessor, static_cast<int>(i));
    }

    {
        auto accessor = storage.acquire_read(overflow_handle);
        ASSERT_TRUE(accessor.valid());
        ASSERT_EQ(*accessor, 999);
    }

    // 清理
    for (const auto& handle : handles) {
        storage.deallocate(handle);
    }
    storage.deallocate(overflow_handle);

    ASSERT_EQ(storage.count(), 0U);
    ASSERT_TRUE(storage.empty());
}

TEST(StorageTests, SlotReuse) {
    Storage<int, 8> storage;

    // 分配多个槽位，然后全部释放
    std::vector<Storage<int, 8>::ObjectId> handles;
    for (int i = 0; i < 8; ++i) {
        auto handle = storage.allocate([i](int& value) { value = i; });
        ASSERT_TRUE(handle > 0);
        handles.push_back(handle);
    }
    ASSERT_EQ(storage.count(), 8U);

    // 全部释放
    for (const auto& handle : handles) {
        storage.deallocate(handle);
    }
    ASSERT_EQ(storage.count(), 0U);

    // 再次分配应该复用之前的槽位，而不会触发扩容
    std::size_t initial_capacity = storage.capacity();
    std::vector<Storage<int, 8>::ObjectId> reused_handles;
    for (int i = 0; i < 8; ++i) {
        auto handle = storage.allocate([i](int& value) { value = i + 100; });
        ASSERT_TRUE(handle > 0);
        reused_handles.push_back(handle);
    }

    // 容量不应该增加，证明槽位被复用
    ASSERT_EQ(storage.capacity(), initial_capacity);
    ASSERT_EQ(storage.count(), 8U);

    // 验证新值
    for (int i = 0; i < 8; ++i) {
        auto accessor = storage.acquire_read(reused_handles[i]);
        ASSERT_TRUE(accessor.valid());
        ASSERT_EQ(*accessor, i + 100);
    }

    // 清理
    for (const auto& handle : reused_handles) {
        storage.deallocate(handle);
    }
}

TEST(StorageTests, ReadWriteLockContention) {
    Storage<int, 32> storage;
    auto handle = storage.allocate([](int& value) { value = 0; });

    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<std::size_t> successful_reads{0};
    std::atomic<std::size_t> successful_writes{0};

    // 多个读者线程
    auto reader = [&]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!stop.load(std::memory_order_relaxed)) {
            auto accessor = storage.acquire_read(handle);
            if (accessor) {
                (void)*accessor;
                successful_reads.fetch_add(1, std::memory_order_relaxed);
            }
            std::this_thread::yield();
        }
    };

    // 少量写者线程
    auto writer = [&]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!stop.load(std::memory_order_relaxed)) {
            auto accessor = storage.acquire_write(handle);
            if (accessor) {
                *accessor += 1;
                successful_writes.fetch_add(1, std::memory_order_relaxed);
            }
            std::this_thread::yield();
        }
    };

    std::vector<std::thread> threads;
    // 多个读者，少数写者
    for (int i = 0; i < 6; ++i) {
        threads.emplace_back(reader);
    }
    for (int i = 0; i < 2; ++i) {
        threads.emplace_back(writer);
    }

    start.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    ASSERT_GT(successful_reads.load(), 0U);
    ASSERT_GT(successful_writes.load(), 0U);

    // 验证最终值等于写入次数
    {
        auto accessor = storage.acquire_read(handle);
        ASSERT_TRUE(accessor.valid());
        ASSERT_EQ(*accessor, static_cast<int>(successful_writes.load()));
    }

    storage.deallocate(handle);
}

TEST(StorageTests, ForEachWithConcurrentModifications) {
    Storage<int, 64> storage;
    std::vector<Storage<int, 64>::ObjectId> handles;

    // 预分配一些对象
    for (int i = 0; i < 20; ++i) {
        auto handle = storage.allocate([i](int& value) { value = i; });
        handles.push_back(handle);
    }

    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<std::size_t> iterations{0};

    // 遍历线程
    auto iterator = [&]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!stop.load(std::memory_order_relaxed)) {
            storage.for_each_read([](const int& value) {
                (void)value;
            });
            iterations.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    };

    // 修改线程
    auto modifier = [&]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!stop.load(std::memory_order_relaxed)) {
            for (const auto& handle : handles) {
                auto accessor = storage.acquire_write(handle);
                if (accessor) {
                    *accessor += 1;
                }
            }
            std::this_thread::yield();
        }
    };

    std::vector<std::thread> threads;
    threads.emplace_back(iterator);
    threads.emplace_back(iterator);
    threads.emplace_back(modifier);

    start.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    ASSERT_GT(iterations.load(), 0U);

    // 清理
    for (const auto& handle : handles) {
        storage.deallocate(handle);
    }
}

TEST(StorageTests, StressTestAllocationDeallocation) {
    static constexpr std::size_t capacity = 64;
    Storage<int, capacity> storage;

    std::atomic<bool> start{false};
    std::atomic<std::size_t> total_ops{0};

    auto worker = [&]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        std::vector<Storage<int, capacity>::ObjectId> local_handles;
        local_handles.reserve(capacity / 4);

        for (int iteration = 0; iteration < 1000; ++iteration) {
            // 分配一批
            for (int i = 0; i < 10; ++i) {
                if (auto handle = storage.allocate([i](int& value) { value = i; })) {
                    local_handles.push_back(handle);
                }
            }

            // 访问部分
            for (std::size_t i = 0; i < local_handles.size(); i += 2) {
                auto accessor = storage.acquire_read(local_handles[i]);
                if (accessor) {
                    (void)*accessor;
                }
            }

            // 修改部分
            for (std::size_t i = 1; i < local_handles.size(); i += 2) {
                auto accessor = storage.acquire_write(local_handles[i]);
                if (accessor) {
                    *accessor += 1;
                }
            }

            // 释放一半
            while (local_handles.size() > capacity / 8) {
                storage.deallocate(local_handles.back());
                local_handles.pop_back();
            }

            total_ops.fetch_add(1, std::memory_order_relaxed);
        }

        // 清理剩余
        for (const auto& handle : local_handles) {
            storage.deallocate(handle);
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back(worker);
    }

    start.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    ASSERT_EQ(total_ops.load(), 8000U);  // 8 threads * 1000 iterations
}

TEST(StorageTests, ComplexDataStructure) {
    struct ComplexObject {
        int id = 0;
        std::string name;
        std::vector<int> data;
    };

    Storage<ComplexObject, 16> storage;

    auto handle = storage.allocate([](ComplexObject& obj) {
        obj.id = 1;
        obj.name = "test_object";
        obj.data = {1, 2, 3, 4, 5};
    });

    ASSERT_TRUE(handle > 0);

    // 读取复杂对象
    {
        auto accessor = storage.acquire_read(handle);
        ASSERT_TRUE(accessor.valid());
        ASSERT_EQ(accessor->id, 1);
        ASSERT_EQ(accessor->name, "test_object");
        ASSERT_EQ(accessor->data.size(), 5U);
        ASSERT_EQ(accessor->data[0], 1);
    }

    // 修改复杂对象
    {
        auto accessor = storage.acquire_write(handle);
        ASSERT_TRUE(accessor.valid());
        accessor->name = "modified";
        accessor->data.push_back(6);
    }

    // 验证修改
    {
        auto accessor = storage.acquire_read(handle);
        ASSERT_TRUE(accessor.valid());
        ASSERT_EQ(accessor->name, "modified");
        ASSERT_EQ(accessor->data.size(), 6U);
    }

    storage.deallocate(handle);
}

TEST(StorageTests, ZeroInitialization) {
    Storage<int, 8> storage;

    // 测试默认初始化（应该调用 T{} 进行零初始化）
    auto handle = storage.allocate([](int& value) {
        // 不设置值，验证默认构造的行为
        (void)value;
    });

    {
        auto accessor = storage.acquire_read(handle);
        ASSERT_TRUE(accessor.valid());
        // int{} 应该初始化为 0
        ASSERT_EQ(*accessor, 0);
    }

    storage.deallocate(handle);
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
        std::vector<Storage<int, capacity>::ObjectId> owned;
        owned.reserve(capacity);

        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!stop.load(std::memory_order_relaxed)) {
            // 分配新槽位
            if (auto handle = storage.allocate([](int& value) { value = 0; })) {
                owned.push_back(handle);
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
        std::vector<Storage<int, capacity>::ObjectId> to_deallocate;
        to_deallocate.reserve(capacity / 4);

        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!stop.load(std::memory_order_relaxed)) {
            // 尝试分配一批句柄用于后续释放
            for (int i = 0; i < 10 && to_deallocate.size() < capacity / 4; ++i) {
                if (auto handle = storage.allocate([i](int& value) { value = i; })) {
                    to_deallocate.push_back(handle);
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
        std::vector<Storage<int, capacity>::ObjectId> cached_handles;
        cached_handles.reserve(capacity / 4);

        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!stop.load(std::memory_order_relaxed)) {
            // 获取一些句柄进行读取
            for (int i = 0; i < 5; ++i) {
                if (auto handle = storage.allocate([](int& value) { value = 999; })) {
                    cached_handles.push_back(handle);
                }
            }

            // 读取这些句柄
            for (const auto& handle : cached_handles) {
                auto accessor = storage.acquire_read(handle);
                if (accessor) {
                    (void)*accessor;
                    reads.fetch_add(1, std::memory_order_relaxed);
                }
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
        std::vector<Storage<int, capacity>::ObjectId> cached_handles;
        cached_handles.reserve(capacity / 4);

        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!stop.load(std::memory_order_relaxed)) {
            // 获取一些句柄进行写入
            for (int i = 0; i < 5; ++i) {
                if (auto handle = storage.allocate([](int& value) { value = 0; })) {
                    cached_handles.push_back(handle);
                }
            }

            // 修改这些句柄
            for (const auto& handle : cached_handles) {
                auto accessor = storage.acquire_write(handle);
                if (accessor) {
                    *accessor += 1;
                    writes.fetch_add(1, std::memory_order_relaxed);
                }
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
            storage.for_each_read([&](const int& value) {
                (void)value;
            });

            // 可写遍历
            storage.for_each_write([&](int& value) {
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
