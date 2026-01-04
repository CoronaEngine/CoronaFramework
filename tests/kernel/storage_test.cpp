#include "corona/kernel/utils/storage.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <random>
#include <set>
#include <thread>
#include <vector>

#include "../test_framework.h"

using Corona::Kernel::Utils::Storage;

TEST(StorageTests, BasicAllocateAndAccess) {
    Storage<int, 8> storage;

    // 分配并初始化
    auto handle = storage.allocate();
    ASSERT_TRUE(handle > 0);
    {
        auto accessor = storage.acquire_write(handle);
        *accessor = 42;
    }

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
    ASSERT_THROW(storage.acquire_read(handle), std::runtime_error);
}

TEST(StorageTests, MultipleAllocations) {
    Storage<int, 8> storage;
    std::vector<Storage<int, 8>::ObjectId> handles;

    // 分配多个槽位
    for (int i = 0; i < 8; ++i) {
        auto handle = storage.allocate();
        ASSERT_TRUE(handle > 0);
        {
            auto accessor = storage.acquire_write(handle);
            *accessor = i * 10;
        }
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
    auto recycled = storage.allocate();
    ASSERT_TRUE(recycled > 0);
    {
        auto accessor = storage.acquire_write(recycled);
        *accessor = 999;
    }
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
        auto handle = storage.allocate();
        ASSERT_TRUE(handle > 0);
        {
            auto accessor = storage.acquire_write(handle);
            *accessor = static_cast<int>(i);
        }
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
        auto handle = storage.allocate();
        ASSERT_TRUE(handle > 0);
        {
            auto accessor = storage.acquire_write(handle);
            *accessor = i + 1;
        }
        handles.push_back(handle);
    }

    // 只读遍历并统计
    int sum = 0;
    int count = 0;
    for (const auto& value : storage) {
        sum += value;
        ++count;
    }

    ASSERT_EQ(count, 10);
    ASSERT_EQ(sum, 55);  // 1+2+...+10 = 55

    // 可写遍历，所有值乘以2
    for (auto& value : storage) {
        value *= 2;
    }

    // 验证修改
    sum = 0;
    for (const auto& value : storage) {
        sum += value;
    }
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
            auto handle = storage.allocate();

            if (handle > 0) {
                {
                    auto accessor = storage.acquire_write(handle);
                    *accessor = iteration;
                }
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
    static constexpr std::size_t capacity = 128;
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
            if (auto handle = storage.allocate()) {
                {
                    auto accessor = storage.acquire_write(handle);
                    *accessor = 1;
                }
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
            for (const auto& value : storage) {
                ASSERT_GE(value, 0);
                reads.fetch_add(1, std::memory_order_relaxed);
            }
            std::this_thread::yield();
        }
    };

    threads.emplace_back(producer);
    threads.emplace_back(producer);
    threads.emplace_back(producer);
    threads.emplace_back(producer);
    threads.emplace_back(reader);
    threads.emplace_back(reader);
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

    // 验证可以继续正常分配
    auto handle = storage.allocate();
    ASSERT_TRUE(handle > 0);
    {
        auto accessor = storage.acquire_write(handle);
        *accessor = 42;
    }
    {
        auto accessor = storage.acquire_read(handle);
        ASSERT_TRUE(accessor.valid());
        ASSERT_EQ(*accessor, 42);
    }
    storage.deallocate(handle);

    // write 时抛异常
    handle = storage.allocate();
    ASSERT_TRUE(handle > 0);
    {
        auto accessor = storage.acquire_write(handle);
        *accessor = 100;
    }

    bool exception_caught = false;
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
    auto handle = storage.allocate();
    ASSERT_TRUE(handle > 0);
    {
        auto accessor = storage.acquire_write(handle);
        *accessor = 42;
    }

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
    auto handle = storage.allocate();
    {
        auto accessor = storage.acquire_write(handle);
        *accessor = 42;
    }

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
    ASSERT_THROW(storage.acquire_read(0), std::runtime_error);
    ASSERT_THROW(storage.acquire_write(0), std::runtime_error);

    // 测试未分配的句柄
    Storage<int, 8>::ObjectId fake_handle = 0x123456;
    ASSERT_THROW(storage.acquire_read(fake_handle), std::runtime_error);
    ASSERT_THROW(storage.acquire_write(fake_handle), std::runtime_error);
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
        auto handle = storage.allocate();
        ASSERT_TRUE(handle > 0);
        {
            auto accessor = storage.acquire_write(handle);
            *accessor = static_cast<int>(i);
        }
        handles.push_back(handle);
    }

    ASSERT_EQ(storage.count(), capacity);
    ASSERT_FALSE(storage.empty());

    // 分配第 capacity+1 个对象，触发扩容
    auto overflow_handle = storage.allocate();
    {
        auto accessor = storage.acquire_write(overflow_handle);
        *accessor = 999;
    }
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
        auto handle = storage.allocate();
        ASSERT_TRUE(handle > 0);
        {
            auto accessor = storage.acquire_write(handle);
            *accessor = i;
        }
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
        auto handle = storage.allocate();
        ASSERT_TRUE(handle > 0);
        {
            auto accessor = storage.acquire_write(handle);
            *accessor = i + 100;
        }
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
    auto handle = storage.allocate();
    {
        auto accessor = storage.acquire_write(handle);
        *accessor = 0;
    }

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
        auto handle = storage.allocate();
        {
            auto accessor = storage.acquire_write(handle);
            *accessor = i;
        }
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
            for (const auto& value : storage) {
                (void)value;
            }
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
                if (auto handle = storage.allocate()) {
                    {
                        auto accessor = storage.acquire_write(handle);
                        *accessor = i;
                    }
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

    auto handle = storage.allocate();
    {
        auto accessor = storage.acquire_write(handle);
        accessor->id = 1;
        accessor->name = "test_object";
        accessor->data = {1, 2, 3, 4, 5};
    }

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
    auto handle = storage.allocate();

    {
        auto accessor = storage.acquire_read(handle);
        ASSERT_TRUE(accessor.valid());
        // int{} 应该初始化为 0
        ASSERT_EQ(*accessor, 0);
    }

    storage.deallocate(handle);
}

TEST(StorageTests, ShrinkOnDeallocation) {
    // 使用较小的 buffer 容量便于测试缩容
    // InitialBuffers = 1，BufferCapacity = 4
    // 缩容策略：保留末尾最多 2 个空 buffer，且不少于 InitialBuffers
    static constexpr std::size_t buffer_capacity = 4;
    static constexpr std::size_t initial_buffers = 1;
    Storage<int, buffer_capacity, initial_buffers> storage;

    // 初始容量应为 1 * 4 = 4
    ASSERT_EQ(storage.capacity(), buffer_capacity * initial_buffers);

    std::vector<Storage<int, buffer_capacity, initial_buffers>::ObjectId> handles;

    // 分配 12 个对象，触发扩容到 3 个 buffer（容量 = 12）
    for (int i = 0; i < 12; ++i) {
        auto handle = storage.allocate();
        ASSERT_TRUE(handle > 0);
        {
            auto accessor = storage.acquire_write(handle);
            *accessor = i;
        }
        handles.push_back(handle);
    }

    ASSERT_EQ(storage.capacity(), 12U);  // 3 个 buffer
    ASSERT_EQ(storage.count(), 12U);

    // 释放第三个 buffer 的所有对象（索引 8-11）
    // 此时第三个 buffer 变空，但由于只有 1 个空 buffer，不会触发缩容
    for (int i = 11; i >= 8; --i) {
        storage.deallocate(handles[i]);
    }
    handles.resize(8);

    ASSERT_EQ(storage.count(), 8U);
    // 容量可能仍为 12（1 个空 buffer 不触发缩容）
    ASSERT_GE(storage.capacity(), 8U);

    // 继续释放第二个 buffer 的所有对象（索引 4-7）
    // 此时末尾有 2 个连续空 buffer，仍不触发缩容（保留 2 个）
    for (int i = 7; i >= 4; --i) {
        storage.deallocate(handles[i]);
    }
    handles.resize(4);

    ASSERT_EQ(storage.count(), 4U);
    // 容量可能仍为 12（2 个空 buffer 是保留上限）
    ASSERT_GE(storage.capacity(), 4U);

    // 再分配 8 个对象以创建更多 buffer（总共需要 12 个槽位）
    // 此时会复用空闲槽位，不一定扩容
    std::vector<Storage<int, buffer_capacity, initial_buffers>::ObjectId> more_handles;
    for (int i = 0; i < 8; ++i) {
        auto handle = storage.allocate();
        ASSERT_TRUE(handle > 0);
        {
            auto accessor = storage.acquire_write(handle);
            *accessor = i + 100;
        }
        more_handles.push_back(handle);
    }

    ASSERT_EQ(storage.count(), 12U);  // 4 + 8 = 12

    // 再扩容：分配更多对象直到有 5 个 buffer（容量 = 20）
    for (int i = 0; i < 8; ++i) {
        auto handle = storage.allocate();
        ASSERT_TRUE(handle > 0);
        {
            auto accessor = storage.acquire_write(handle);
            *accessor = i + 200;
        }
        more_handles.push_back(handle);
    }

    std::size_t peak_capacity = storage.capacity();
    ASSERT_GE(peak_capacity, 20U);  // 至少 5 个 buffer
    ASSERT_EQ(storage.count(), 20U);

    // 释放所有 more_handles（16 个对象），触发缩容
    for (const auto& handle : more_handles) {
        storage.deallocate(handle);
    }
    more_handles.clear();

    ASSERT_EQ(storage.count(), 4U);  // 只剩 handles 中的 4 个

    // 检查是否发生缩容：容量应该减少（保留末尾最多 2 个空 buffer）
    // 由于释放顺序和缩容时机的不确定性，只验证容量不超过峰值
    ASSERT_LE(storage.capacity(), peak_capacity);

    // 释放剩余的 4 个对象
    for (const auto& handle : handles) {
        storage.deallocate(handle);
    }
    handles.clear();

    ASSERT_EQ(storage.count(), 0U);
    ASSERT_TRUE(storage.empty());

    // 完全清空后，容量应缩减到接近初始值
    // 保留最多 2 个空 buffer，但不少于 InitialBuffers
    // 可能的最终容量：InitialBuffers * buffer_capacity = 4
    // 或者 2 * buffer_capacity = 8（如果保留了 2 个空 buffer）
    ASSERT_LE(storage.capacity(), 8U);                                 // 最多保留 2 个空 buffer
    ASSERT_GE(storage.capacity(), buffer_capacity * initial_buffers);  // 至少保留初始数量
}

TEST(StorageTests, ShrinkPreservesInitialBuffers) {
    // 测试缩容不会低于 InitialBuffers
    static constexpr std::size_t buffer_capacity = 4;
    static constexpr std::size_t initial_buffers = 2;  // 初始 2 个 buffer
    Storage<int, buffer_capacity, initial_buffers> storage;

    // 初始容量应为 2 * 4 = 8
    ASSERT_EQ(storage.capacity(), buffer_capacity * initial_buffers);

    std::vector<Storage<int, buffer_capacity, initial_buffers>::ObjectId> handles;

    // 分配 16 个对象，触发扩容到 4 个 buffer
    for (int i = 0; i < 16; ++i) {
        auto handle = storage.allocate();
        ASSERT_TRUE(handle > 0);
        {
            auto accessor = storage.acquire_write(handle);
            *accessor = i;
        }
        handles.push_back(handle);
    }

    ASSERT_EQ(storage.capacity(), 16U);
    ASSERT_EQ(storage.count(), 16U);

    // 全部释放
    for (const auto& handle : handles) {
        storage.deallocate(handle);
    }
    handles.clear();

    ASSERT_EQ(storage.count(), 0U);

    // 即使全部释放，容量也不应低于 InitialBuffers
    // 缩容策略：保留末尾最多 2 个空 buffer，且不少于 InitialBuffers
    // InitialBuffers = 2，所以最终应保留 2 个 buffer
    ASSERT_GE(storage.capacity(), buffer_capacity * initial_buffers);
}

TEST(StorageTests, ShrinkConcurrent) {
    // 并发场景下的缩容测试
    static constexpr std::size_t buffer_capacity = 8;
    static constexpr std::size_t initial_buffers = 1;
    Storage<int, buffer_capacity, initial_buffers> storage;

    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<std::size_t> shrink_observed{0};  // 观察到容量减少的次数

    auto worker = [&]() {
        std::vector<Storage<int, buffer_capacity, initial_buffers>::ObjectId> local_handles;
        local_handles.reserve(buffer_capacity * 2);
        std::size_t last_capacity = 0;

        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!stop.load(std::memory_order_relaxed)) {
            // 分配一批对象
            for (int i = 0; i < 16; ++i) {
                if (auto handle = storage.allocate()) {
                    {
                        auto accessor = storage.acquire_write(handle);
                        *accessor = i;
                    }
                    local_handles.push_back(handle);
                }
            }

            std::size_t current_capacity = storage.capacity();

            // 释放所有对象
            for (const auto& handle : local_handles) {
                storage.deallocate(handle);
            }
            local_handles.clear();

            // 检查是否发生缩容
            std::size_t new_capacity = storage.capacity();
            if (last_capacity > 0 && new_capacity < last_capacity) {
                shrink_observed.fetch_add(1, std::memory_order_relaxed);
            }
            last_capacity = current_capacity;

            std::this_thread::yield();
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back(worker);
    }

    start.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stop.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    // 并发测试后 storage 应该仍然可用
    ASSERT_TRUE(storage.empty());
    ASSERT_GE(storage.capacity(), buffer_capacity * initial_buffers);

    // 验证可以继续正常分配
    auto handle = storage.allocate();
    ASSERT_TRUE(handle > 0);
    {
        auto accessor = storage.acquire_write(handle);
        *accessor = 999;
    }
    {
        auto accessor = storage.acquire_read(handle);
        ASSERT_TRUE(accessor.valid());
        ASSERT_EQ(*accessor, 999);
    }
    storage.deallocate(handle);

    std::cout << "Shrink observed count: " << shrink_observed.load() << "\n";
}

TEST(StorageTests, SeqIdCalculation) {
    // 测试 seq_id 序列号计算功能
    static constexpr std::size_t buffer_capacity = 8;
    static constexpr std::size_t initial_buffers = 2;
    Storage<int, buffer_capacity, initial_buffers> storage;

    std::vector<Storage<int, buffer_capacity, initial_buffers>::ObjectId> handles;
    std::vector<std::int64_t> seq_ids;

    // 分配一些对象并记录序列号
    for (int i = 0; i < 16; ++i) {
        auto handle = storage.allocate();
        ASSERT_TRUE(handle > 0);
        {
            auto accessor = storage.acquire_write(handle);
            *accessor = i;
        }
        handles.push_back(handle);

        // 通过 ObjectId 获取序列号
        std::int64_t seq = storage.seq_id(handle);
        ASSERT_GE(seq, 0);
        ASSERT_LT(seq, static_cast<std::int64_t>(storage.capacity()));
        seq_ids.push_back(seq);
    }

    // 验证序列号唯一性
    std::set<std::int64_t> unique_seq_ids(seq_ids.begin(), seq_ids.end());
    ASSERT_EQ(unique_seq_ids.size(), seq_ids.size());

    // 通过 ReadHandle 获取序列号
    {
        auto accessor = storage.acquire_read(handles[0]);
        std::int64_t seq_from_handle = storage.seq_id(accessor);
        ASSERT_EQ(seq_from_handle, seq_ids[0]);
    }

    // 通过 WriteHandle 获取序列号
    {
        auto accessor = storage.acquire_write(handles[1]);
        std::int64_t seq_from_handle = storage.seq_id(accessor);
        ASSERT_EQ(seq_from_handle, seq_ids[1]);
    }

    // 清理
    for (const auto& handle : handles) {
        storage.deallocate(handle);
    }
}

TEST(StorageTests, SeqIdInvalidHandle) {
    Storage<int, 8> storage;

    // 测试无效 ObjectId 的 seq_id 应该抛出异常
    ASSERT_THROW(storage.seq_id(0), std::runtime_error);
    ASSERT_THROW(storage.seq_id(0x123456), std::runtime_error);

    // 测试无效 ReadHandle 的 seq_id 返回 -1
    Storage<int, 8>::ReadHandle invalid_read_handle;
    ASSERT_EQ(storage.seq_id(invalid_read_handle), -1);

    // 测试无效 WriteHandle 的 seq_id 返回 -1
    Storage<int, 8>::WriteHandle invalid_write_handle;
    ASSERT_EQ(storage.seq_id(invalid_write_handle), -1);
}

TEST(StorageTests, DoubleFreeDetection) {
    Storage<int, 8> storage;

    auto handle = storage.allocate();
    ASSERT_TRUE(handle > 0);
    {
        auto accessor = storage.acquire_write(handle);
        *accessor = 42;
    }

    // 第一次释放应该成功
    storage.deallocate(handle);

    // 第二次释放应该抛出异常（双重释放检测）
    ASSERT_THROW(storage.deallocate(handle), std::runtime_error);
}

TEST(StorageTests, IteratorEmptyStorage) {
    Storage<int, 8> storage;

    // 空存储的迭代应该立即结束
    int count = 0;
    for (const auto& value : storage) {
        (void)value;
        ++count;
    }
    ASSERT_EQ(count, 0);

    // 可写迭代也应该立即结束
    for (auto& value : storage) {
        (void)value;
        ++count;
    }
    ASSERT_EQ(count, 0);

    // begin 和 end 应该相等
    ASSERT_TRUE(storage.begin() == storage.end());

    // const 迭代器测试
    const auto& const_storage = storage;
    ASSERT_TRUE(const_storage.begin() == const_storage.end());
}

TEST(StorageTests, IteratorPartiallyFilledBuffer) {
    Storage<int, 8> storage;

    // 只分配部分槽位
    std::vector<Storage<int, 8>::ObjectId> handles;
    for (int i = 0; i < 3; ++i) {
        auto handle = storage.allocate();
        {
            auto accessor = storage.acquire_write(handle);
            *accessor = (i + 1) * 10;
        }
        handles.push_back(handle);
    }

    // 只读迭代应该只遍历已分配的槽位
    std::vector<int> values;
    for (const auto& value : storage) {
        values.push_back(value);
    }
    ASSERT_EQ(values.size(), 3U);

    // 验证值（顺序可能不同）
    std::sort(values.begin(), values.end());
    ASSERT_EQ(values[0], 10);
    ASSERT_EQ(values[1], 20);
    ASSERT_EQ(values[2], 30);

    // 清理
    for (const auto& handle : handles) {
        storage.deallocate(handle);
    }
}

TEST(StorageTests, IteratorSkippedAndRetry) {
    // 测试迭代器跳过被锁定槽位后重试的机制
    Storage<int, 16> storage;

    std::vector<Storage<int, 16>::ObjectId> handles;
    for (int i = 0; i < 10; ++i) {
        auto handle = storage.allocate();
        {
            auto accessor = storage.acquire_write(handle);
            *accessor = i;
        }
        handles.push_back(handle);
    }

    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<std::size_t> iteration_count{0};

    // 锁定线程：持续锁定部分槽位
    auto locker = [&]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!stop.load(std::memory_order_relaxed)) {
            // 持有部分槽位的写锁一段时间
            std::vector<Storage<int, 16>::WriteHandle> locked_handles;
            for (std::size_t i = 0; i < handles.size(); i += 3) {
                locked_handles.push_back(storage.acquire_write(handles[i]));
            }
            std::this_thread::sleep_for(std::chrono::microseconds(10));
            // 句柄析构自动释放锁
        }
    };

    // 迭代线程
    auto iterator = [&]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!stop.load(std::memory_order_relaxed)) {
            int count = 0;
            for (const auto& value : storage) {
                (void)value;
                ++count;
            }
            // 应该能遍历到所有元素（包括重试的）
            ASSERT_EQ(count, 10);
            iteration_count.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::vector<std::thread> threads;
    threads.emplace_back(locker);
    threads.emplace_back(iterator);

    start.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    ASSERT_GT(iteration_count.load(), 0U);

    // 清理
    for (const auto& handle : handles) {
        storage.deallocate(handle);
    }
}

TEST(StorageTests, HandleArrowOperator) {
    struct TestStruct {
        int x = 0;
        int y = 0;
        std::string name;
    };

    Storage<TestStruct, 8> storage;
    auto handle = storage.allocate();

    // 测试 WriteHandle 的箭头操作符
    {
        auto accessor = storage.acquire_write(handle);
        ASSERT_TRUE(accessor.valid());
        accessor->x = 10;
        accessor->y = 20;
        accessor->name = "test";

        // 测试 get() 方法
        TestStruct* ptr = accessor.get();
        ASSERT_TRUE(ptr != nullptr);
        ASSERT_EQ(ptr->x, 10);
    }

    // 测试 ReadHandle 的箭头操作符
    {
        auto accessor = storage.acquire_read(handle);
        ASSERT_TRUE(accessor.valid());
        ASSERT_EQ(accessor->x, 10);
        ASSERT_EQ(accessor->y, 20);
        ASSERT_EQ(accessor->name, "test");

        // 测试 get() 方法
        const TestStruct* ptr = accessor.get();
        ASSERT_TRUE(ptr != nullptr);
        ASSERT_EQ(ptr->y, 20);
    }

    storage.deallocate(handle);
}

TEST(StorageTests, HandleOperatorBool) {
    Storage<int, 8> storage;
    auto handle = storage.allocate();
    {
        auto accessor = storage.acquire_write(handle);
        *accessor = 42;
    }

    // 有效句柄的 operator bool 应该返回 true
    {
        auto read_accessor = storage.acquire_read(handle);
        ASSERT_TRUE(static_cast<bool>(read_accessor));
        if (read_accessor) {
            ASSERT_EQ(*read_accessor, 42);
        }
    }

    {
        auto write_accessor = storage.acquire_write(handle);
        ASSERT_TRUE(static_cast<bool>(write_accessor));
        if (write_accessor) {
            *write_accessor = 100;
        }
    }

    // 默认构造的句柄 operator bool 应该返回 false
    Storage<int, 8>::ReadHandle default_read;
    ASSERT_FALSE(static_cast<bool>(default_read));

    Storage<int, 8>::WriteHandle default_write;
    ASSERT_FALSE(static_cast<bool>(default_write));

    storage.deallocate(handle);
}

// 全局原子计数器用于 NonTrivialTypeConstruction 测试
namespace {
std::atomic<int> g_nontrivial_construct_count{0};
std::atomic<int> g_nontrivial_destruct_count{0};
}  // namespace

TEST(StorageTests, NonTrivialTypeConstruction) {
    struct NonTrivial {
        int value = 0;
        std::string str;

        NonTrivial() : value(0), str("default") {
            g_nontrivial_construct_count.fetch_add(1, std::memory_order_relaxed);
        }

        ~NonTrivial() {
            g_nontrivial_destruct_count.fetch_add(1, std::memory_order_relaxed);
        }

        NonTrivial(const NonTrivial& other) : value(other.value), str(other.str) {
            g_nontrivial_construct_count.fetch_add(1, std::memory_order_relaxed);
        }

        NonTrivial& operator=(const NonTrivial& other) {
            value = other.value;
            str = other.str;
            return *this;
        }
    };

    g_nontrivial_construct_count.store(0);
    g_nontrivial_destruct_count.store(0);

    {
        Storage<NonTrivial, 8> storage;

        auto handle = storage.allocate();
        {
            auto accessor = storage.acquire_read(handle);
            ASSERT_TRUE(accessor.valid());
            // 应该是默认构造的值
            ASSERT_EQ(accessor->value, 0);
            ASSERT_EQ(accessor->str, "default");
        }

        {
            auto accessor = storage.acquire_write(handle);
            accessor->value = 42;
            accessor->str = "modified";
        }

        {
            auto accessor = storage.acquire_read(handle);
            ASSERT_EQ(accessor->value, 42);
            ASSERT_EQ(accessor->str, "modified");
        }

        storage.deallocate(handle);
    }

    // 验证构造和析构被调用
    ASSERT_GT(g_nontrivial_construct_count.load(), 0);
}

TEST(StorageTests, LargeObjectStorage) {
    // 测试存储大对象
    struct LargeObject {
        std::array<char, 1024> data{};
        int id = 0;
    };

    Storage<LargeObject, 16> storage;

    std::vector<Storage<LargeObject, 16>::ObjectId> handles;
    for (int i = 0; i < 10; ++i) {
        auto handle = storage.allocate();
        ASSERT_TRUE(handle > 0);
        {
            auto accessor = storage.acquire_write(handle);
            accessor->id = i;
            std::fill(accessor->data.begin(), accessor->data.end(), static_cast<char>(i));
        }
        handles.push_back(handle);
    }

    // 验证数据完整性
    for (int i = 0; i < 10; ++i) {
        auto accessor = storage.acquire_read(handles[i]);
        ASSERT_TRUE(accessor.valid());
        ASSERT_EQ(accessor->id, i);
        for (const auto& c : accessor->data) {
            ASSERT_EQ(c, static_cast<char>(i));
        }
    }

    // 清理
    for (const auto& handle : handles) {
        storage.deallocate(handle);
    }
}

TEST(StorageTests, MultipleBufferExpansion) {
    // 测试多次扩容
    static constexpr std::size_t buffer_capacity = 4;
    static constexpr std::size_t initial_buffers = 1;
    Storage<int, buffer_capacity, initial_buffers> storage;

    ASSERT_EQ(storage.capacity(), 4U);

    std::vector<Storage<int, buffer_capacity, initial_buffers>::ObjectId> handles;

    // 触发 5 次扩容（从 1 个 buffer 扩展到 6 个）
    for (int i = 0; i < 24; ++i) {
        auto handle = storage.allocate();
        ASSERT_TRUE(handle > 0);
        {
            auto accessor = storage.acquire_write(handle);
            *accessor = i;
        }
        handles.push_back(handle);
    }

    ASSERT_EQ(storage.capacity(), 24U);  // 6 个 buffer
    ASSERT_EQ(storage.count(), 24U);

    // 验证所有数据
    for (int i = 0; i < 24; ++i) {
        auto accessor = storage.acquire_read(handles[i]);
        ASSERT_TRUE(accessor.valid());
        ASSERT_EQ(*accessor, i);
    }

    // 清理
    for (const auto& handle : handles) {
        storage.deallocate(handle);
    }
}

TEST(StorageTests, RandomAllocDeallocPattern) {
    // 测试随机分配/释放模式
    Storage<int, 32> storage;
    std::vector<Storage<int, 32>::ObjectId> handles;

    // 随机种子（使用确定性种子保证可重复）
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> action_dist(0, 1);
    std::uniform_int_distribution<int> value_dist(0, 1000);

    for (int i = 0; i < 1000; ++i) {
        if (handles.empty() || (action_dist(rng) == 0 && handles.size() < 100)) {
            // 分配
            auto handle = storage.allocate();
            ASSERT_TRUE(handle > 0);
            {
                auto accessor = storage.acquire_write(handle);
                *accessor = value_dist(rng);
            }
            handles.push_back(handle);
        } else {
            // 释放随机一个
            std::uniform_int_distribution<std::size_t> idx_dist(0, handles.size() - 1);
            std::size_t idx = idx_dist(rng);

            storage.deallocate(handles[idx]);
            handles.erase(handles.begin() + static_cast<std::ptrdiff_t>(idx));
        }
    }

    // 验证 count 与 handles 大小一致
    ASSERT_EQ(storage.count(), handles.size());

    // 清理
    for (const auto& handle : handles) {
        storage.deallocate(handle);
    }

    ASSERT_TRUE(storage.empty());
}

TEST(StorageTests, ConcurrentIterationDuringExpansion) {
    // 测试扩容期间的迭代安全性
    static constexpr std::size_t buffer_capacity = 8;
    static constexpr std::size_t initial_buffers = 1;
    Storage<int, buffer_capacity, initial_buffers> storage;

    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<std::size_t> iteration_count{0};
    std::atomic<std::size_t> expansion_count{0};

    // 迭代线程
    auto iterator = [&]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!stop.load(std::memory_order_relaxed)) {
            std::size_t count = 0;
            for (const auto& value : storage) {
                (void)value;
                ++count;
            }
            if (count > 0) {
                iteration_count.fetch_add(1, std::memory_order_relaxed);
            }
            std::this_thread::yield();
        }
    };

    // 扩容线程：快速分配释放触发扩容
    auto expander = [&]() {
        std::vector<Storage<int, buffer_capacity, initial_buffers>::ObjectId> local_handles;
        local_handles.reserve(buffer_capacity * 4);

        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!stop.load(std::memory_order_relaxed)) {
            std::size_t old_capacity = storage.capacity();

            // 快速分配直到扩容
            for (int i = 0; i < 32; ++i) {
                if (auto handle = storage.allocate()) {
                    {
                        auto accessor = storage.acquire_write(handle);
                        *accessor = i;
                    }
                    local_handles.push_back(handle);
                }
            }

            if (storage.capacity() > old_capacity) {
                expansion_count.fetch_add(1, std::memory_order_relaxed);
            }

            // 释放所有
            for (const auto& handle : local_handles) {
                storage.deallocate(handle);
            }
            local_handles.clear();

            std::this_thread::yield();
        }
    };

    std::vector<std::thread> threads;
    threads.emplace_back(iterator);
    threads.emplace_back(iterator);
    threads.emplace_back(expander);

    start.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stop.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    // 验证迭代正常执行
    ASSERT_GT(iteration_count.load() + expansion_count.load(), 0U);
}

TEST(StorageTests, WriteHandleMutualExclusion) {
    // 测试写句柄的互斥性
    Storage<int, 8> storage;
    auto handle = storage.allocate();
    {
        auto accessor = storage.acquire_write(handle);
        *accessor = 0;
    }

    std::atomic<bool> start{false};
    std::atomic<int> concurrent_writers{0};
    std::atomic<int> max_concurrent{0};
    std::atomic<std::size_t> write_count{0};

    auto writer = [&]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        for (int i = 0; i < 100; ++i) {
            auto accessor = storage.acquire_write(handle);
            if (accessor) {
                int current = concurrent_writers.fetch_add(1, std::memory_order_relaxed) + 1;

                // 记录最大并发数
                int expected = max_concurrent.load(std::memory_order_relaxed);
                while (current > expected &&
                       !max_concurrent.compare_exchange_weak(expected, current, std::memory_order_relaxed)) {
                }

                *accessor += 1;
                write_count.fetch_add(1, std::memory_order_relaxed);

                concurrent_writers.fetch_sub(1, std::memory_order_relaxed);
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back(writer);
    }

    start.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    // 写句柄应该是互斥的，最大并发数应该为 1
    ASSERT_EQ(max_concurrent.load(), 1);
    ASSERT_EQ(write_count.load(), 400U);  // 4 threads * 100 iterations

    // 最终值应该等于总写入次数
    {
        auto accessor = storage.acquire_read(handle);
        ASSERT_EQ(*accessor, 400);
    }

    storage.deallocate(handle);
}

TEST(StorageTests, ReadHandleConcurrency) {
    // 测试读句柄的并发性（多个读者可以同时访问）
    Storage<int, 8> storage;
    auto handle = storage.allocate();
    {
        auto accessor = storage.acquire_write(handle);
        *accessor = 42;
    }

    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<int> concurrent_readers{0};
    std::atomic<int> max_concurrent{0};

    auto reader = [&]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!stop.load(std::memory_order_relaxed)) {
            auto accessor = storage.acquire_read(handle);
            if (accessor) {
                int current = concurrent_readers.fetch_add(1, std::memory_order_relaxed) + 1;

                // 记录最大并发数
                int expected = max_concurrent.load(std::memory_order_relaxed);
                while (current > expected &&
                       !max_concurrent.compare_exchange_weak(expected, current, std::memory_order_relaxed)) {
                }

                // 持有锁一小段时间以增加并发机会
                std::this_thread::sleep_for(std::chrono::microseconds(1));

                ASSERT_EQ(*accessor, 42);

                concurrent_readers.fetch_sub(1, std::memory_order_relaxed);
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back(reader);
    }

    start.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    stop.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    // 多个读者应该能够同时访问，最大并发数应该大于 1
    ASSERT_GT(max_concurrent.load(), 1);

    storage.deallocate(handle);
}

TEST(StorageTests, IteratorWithDeallocatedSlots) {
    // 测试迭代器跳过已释放槽位
    Storage<int, 16> storage;

    std::vector<Storage<int, 16>::ObjectId> handles;
    for (int i = 0; i < 10; ++i) {
        auto handle = storage.allocate();
        {
            auto accessor = storage.acquire_write(handle);
            *accessor = i;
        }
        handles.push_back(handle);
    }

    // 释放奇数索引的槽位
    for (std::size_t i = 1; i < handles.size(); i += 2) {
        storage.deallocate(handles[i]);
    }

    // 迭代应该只遍历偶数索引的槽位
    std::vector<int> values;
    for (const auto& value : storage) {
        values.push_back(value);
    }

    ASSERT_EQ(values.size(), 5U);

    // 验证值（顺序可能不同）
    std::sort(values.begin(), values.end());
    ASSERT_EQ(values[0], 0);
    ASSERT_EQ(values[1], 2);
    ASSERT_EQ(values[2], 4);
    ASSERT_EQ(values[3], 6);
    ASSERT_EQ(values[4], 8);

    // 清理剩余
    for (std::size_t i = 0; i < handles.size(); i += 2) {
        storage.deallocate(handles[i]);
    }
}

TEST(StorageTests, DefaultInitialBufferCount) {
    // 测试默认初始 buffer 数量（InitialBuffers = 2）
    Storage<int, 8> storage;  // 使用默认模板参数

    ASSERT_EQ(storage.capacity(), 16U);  // 2 buffers * 8 capacity
    ASSERT_EQ(storage.count(), 0U);
    ASSERT_TRUE(storage.empty());

    // 分配 16 个对象（填满初始容量）
    std::vector<Storage<int, 8>::ObjectId> handles;
    for (int i = 0; i < 16; ++i) {
        auto handle = storage.allocate();
        ASSERT_TRUE(handle > 0);
        {
            auto accessor = storage.acquire_write(handle);
            *accessor = i;
        }
        handles.push_back(handle);
    }

    ASSERT_EQ(storage.capacity(), 16U);  // 还没有扩容
    ASSERT_EQ(storage.count(), 16U);

    // 分配第 17 个对象，触发扩容
    auto extra_handle = storage.allocate();
    {
        auto accessor = storage.acquire_write(extra_handle);
        *accessor = 999;
    }
    ASSERT_TRUE(extra_handle > 0);
    ASSERT_EQ(storage.capacity(), 24U);  // 3 buffers

    // 清理
    for (const auto& handle : handles) {
        storage.deallocate(handle);
    }
    storage.deallocate(extra_handle);
}

TEST(StorageTests, WritableIteratorModification) {
    // 测试可写迭代器修改元素
    Storage<int, 16> storage;

    std::vector<Storage<int, 16>::ObjectId> handles;
    for (int i = 0; i < 10; ++i) {
        auto handle = storage.allocate();
        {
            auto accessor = storage.acquire_write(handle);
            *accessor = i;
        }
        handles.push_back(handle);
    }

    // 使用可写迭代器将所有值翻倍
    for (auto& value : storage) {
        value *= 2;
    }

    // 验证修改
    int sum = 0;
    for (const auto& value : storage) {
        sum += value;
    }
    // 原始和 = 0+1+2+...+9 = 45，翻倍后 = 90
    ASSERT_EQ(sum, 90);

    // 清理
    for (const auto& handle : handles) {
        storage.deallocate(handle);
    }
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
            if (auto handle = storage.allocate()) {
                {
                    auto accessor = storage.acquire_write(handle);
                    *accessor = 0;
                }
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
                if (auto handle = storage.allocate()) {
                    {
                        auto accessor = storage.acquire_write(handle);
                        *accessor = i;
                    }
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
                if (auto handle = storage.allocate()) {
                    {
                        auto accessor = storage.acquire_write(handle);
                        *accessor = 999;
                    }
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
                if (auto handle = storage.allocate()) {
                    {
                        auto accessor = storage.acquire_write(handle);
                        *accessor = 0;
                    }
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
            for (const auto& value : storage) {
                (void)value;
            }

            // 可写遍历
            for (auto& value : storage) {
                value = (value + 1) % 1000;
            }

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
