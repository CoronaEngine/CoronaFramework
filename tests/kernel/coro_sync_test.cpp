#include <corona/kernel/coro/coro.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <thread>
#include <vector>

#include "../test_framework.h"

using namespace Corona::Kernel::Coro;
using namespace CoronaTest;

TEST(CoroSyncTest, AsyncMutexBasic) {
    AsyncMutex mutex;
    bool executed = false;

    auto task = [&]() -> Task<void> {
        co_await mutex.lock();
        executed = true;
        mutex.unlock();
    }();

    Runner::run(std::move(task));
    ASSERT_TRUE(executed);
}

TEST(CoroSyncTest, AsyncMutexScopedLock) {
    AsyncMutex mutex;
    bool executed = false;

    auto task = [&]() -> Task<void> {
        {
            auto guard = co_await mutex.lock_scoped();
            executed = true;
        }
        // 锁应在此处释放
    }();

    Runner::run(std::move(task));
    ASSERT_TRUE(executed);
}

TEST(CoroSyncTest, AsyncMutexContentionManual) {
    AsyncMutex mutex;
    std::vector<int> order;
    AsyncScope scope;

    // Task 1: 获取锁 -> 挂起 -> 释放锁
    std::coroutine_handle<> t1_resume_handle;
    auto task1 = [&]() -> Task<void> {
        co_await mutex.lock();
        order.push_back(1);

        // 手动挂起并保存句柄
        struct SaveHandle {
            std::coroutine_handle<>* out;
            bool await_ready() { return false; }
            void await_suspend(std::coroutine_handle<> h) { *out = h; }
            void await_resume() {}
        };
        co_await SaveHandle{&t1_resume_handle};

        order.push_back(2);
        mutex.unlock();
    };

    // Task 2: 获取锁 (应阻塞) -> 执行
    auto task2 = [&]() -> Task<void> {
        co_await mutex.lock();
        order.push_back(3);
        mutex.unlock();
    };

    scope.spawn(task1());
    // task1 运行，获取锁，打印 1，挂起。
    ASSERT_EQ(order.size(), 1);
    ASSERT_EQ(order[0], 1);
    ASSERT_TRUE(t1_resume_handle);

    scope.spawn(task2());
    // task2 运行，尝试获取锁，失败，挂起（加入等待队列）。
    // t2 不应该打印 3。
    ASSERT_EQ(order.size(), 1);

    // 恢复 t1
    if (t1_resume_handle) t1_resume_handle.resume();

    // t1 恢复后，打印 2，释放锁。
    // 释放锁会唤醒 t2。

    ASSERT_EQ(order.size(), 3);
    ASSERT_EQ(order[1], 2);
    ASSERT_EQ(order[2], 3);

    // 等待 scope 完成
    Runner::run([](AsyncScope& s) -> Task<void> { co_await s.join(); }(scope));
}

TEST(CoroSyncTest, AsyncMutexUnlockWithoutWaiters) {
    // 没有等待者时解锁
    AsyncMutex mutex;

    auto task = [&]() -> Task<void> {
        co_await mutex.lock();
        // 没有其他任务在等待
        mutex.unlock();
        co_return;
    }();

    Runner::run(std::move(task));
    ASSERT_TRUE(true);  // 到达这里说明正常
}

TEST(CoroSyncTest, AsyncMutexMultipleWaiters) {
    // 多个等待者按 FIFO 顺序获得锁
    AsyncMutex mutex;
    std::vector<int> order;
    AsyncScope scope;

    std::coroutine_handle<> first_resume;

    // 第一个任务获取锁并挂起
    auto first_task = [&]() -> Task<void> {
        co_await mutex.lock();
        order.push_back(0);

        struct SaveHandle {
            std::coroutine_handle<>* out;
            bool await_ready() { return false; }
            void await_suspend(std::coroutine_handle<> h) { *out = h; }
            void await_resume() {}
        };
        co_await SaveHandle{&first_resume};

        mutex.unlock();
    };

    // 后续任务尝试获取锁
    auto waiter_task = [&](int id) -> Task<void> {
        co_await mutex.lock();
        order.push_back(id);
        mutex.unlock();
    };

    scope.spawn(first_task());
    ASSERT_EQ(order.size(), 1);
    ASSERT_EQ(order[0], 0);

    // 按顺序添加等待者
    scope.spawn(waiter_task(1));
    scope.spawn(waiter_task(2));
    scope.spawn(waiter_task(3));

    // 目前都在等待锁
    ASSERT_EQ(order.size(), 1);

    // 释放第一个锁
    if (first_resume) first_resume.resume();

    // 所有任务按 FIFO 顺序执行
    ASSERT_EQ(order.size(), 4);
    ASSERT_EQ(order[1], 1);
    ASSERT_EQ(order[2], 2);
    ASSERT_EQ(order[3], 3);

    Runner::run([](AsyncScope& s) -> Task<void> { co_await s.join(); }(scope));
}

TEST(CoroSyncTest, AsyncMutexScopedLockMove) {
    // 测试 ScopedLock 的移动语义
    AsyncMutex mutex;
    int counter = 0;

    auto task = [&]() -> Task<void> {
        auto guard1 = co_await mutex.lock_scoped();
        counter++;

        // 移动 guard
        auto guard2 = std::move(guard1);
        counter++;

        // guard1 已经无效，guard2 持有锁
        // 当 guard2 超出作用域时才会解锁
    }();

    Runner::run(std::move(task));
    ASSERT_EQ(counter, 2);
}

TEST(CoroSyncTest, AsyncMutexProtectsSharedData) {
    // 验证互斥锁确实保护共享数据
    TbbExecutor executor(4);
    AsyncMutex mutex;
    int shared_counter = 0;
    constexpr int iterations = 1000;
    constexpr int num_tasks = 4;

    auto increment_task = [&]() -> Task<void> {
        co_await switch_to(executor);
        for (int i = 0; i < iterations; ++i) {
            auto guard = co_await mutex.lock_scoped();
            // 临界区
            int val = shared_counter;
            // 模拟一些工作
            std::this_thread::yield();
            shared_counter = val + 1;
        }
        co_return;
    };

    AsyncScope scope;
    for (int i = 0; i < num_tasks; ++i) {
        scope.spawn(increment_task());
    }

    Runner::run([](AsyncScope& s) -> Task<void> { co_await s.join(); }(scope));

    // 如果互斥正确工作，计数器应该精确等于预期值
    ASSERT_EQ(shared_counter, num_tasks * iterations);
}

TEST(CoroSyncTest, AsyncMutexLockUnlockSequence) {
    // 测试锁-解锁-锁-解锁序列
    AsyncMutex mutex;
    int counter = 0;

    auto task = [&]() -> Task<void> {
        co_await mutex.lock();
        counter++;
        mutex.unlock();

        co_await mutex.lock();
        counter++;
        mutex.unlock();

        co_await mutex.lock();
        counter++;
        mutex.unlock();

        co_return;
    }();

    Runner::run(std::move(task));
    ASSERT_EQ(counter, 3);
}

TEST(CoroSyncTest, AsyncMutexTryLockImmediateSuccess) {
    // 锁空闲时 await_ready 应该返回 true
    AsyncMutex mutex;
    bool immediate = false;

    auto task = [&]() -> Task<void> {
        // 第一次获取锁应该立即成功
        auto lock_awaiter = mutex.lock();
        immediate = lock_awaiter.await_ready();
        if (!immediate) {
            // 如果没有立即成功，正常挂起
            lock_awaiter.await_suspend(std::coroutine_handle<>());
        }
        lock_awaiter.await_resume();
        mutex.unlock();
        co_return;
    }();

    // 因为 Task 是惰性的，需要手动触发
    task.resume();

    // 锁空闲时应该立即获取成功
    ASSERT_TRUE(immediate);
}

TEST(CoroSyncTest, AsyncMutexConcurrentStress) {
    // 高并发压力测试
    TbbExecutor executor(8);
    AsyncMutex mutex;
    std::atomic<int> concurrent_holders{0};
    std::atomic<int> max_concurrent{0};
    constexpr int num_tasks = 100;

    auto task = [&]() -> Task<void> {
        co_await switch_to(executor);

        auto guard = co_await mutex.lock_scoped();

        // 检查并发持有者数量（应该始终为 1）
        int current = concurrent_holders.fetch_add(1) + 1;
        int max = max_concurrent.load();
        while (current > max && !max_concurrent.compare_exchange_weak(max, current)) {
        }

        // 模拟工作
        std::this_thread::sleep_for(std::chrono::microseconds{10});

        concurrent_holders.fetch_sub(1);
        co_return;
    };

    AsyncScope scope;
    for (int i = 0; i < num_tasks; ++i) {
        scope.spawn(task());
    }

    Runner::run([](AsyncScope& s) -> Task<void> { co_await s.join(); }(scope));

    // 互斥锁应确保同时只有一个持有者
    ASSERT_EQ(max_concurrent.load(), 1);
}

int main() {
    return TestRunner::instance().run_all();
}
