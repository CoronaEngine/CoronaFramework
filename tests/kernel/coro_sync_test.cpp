#include <corona/kernel/coro/coro.h>

#include <deque>
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

int main() {
    return TestRunner::instance().run_all();
}
