#include <corona/kernel/coro/coro.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "../test_framework.h"

using namespace Corona::Kernel::Coro;
using namespace CoronaTest;

TEST(CoroScopeTest, SpawnAndJoin) {
    AsyncScope scope;
    std::atomic<int> counter{0};

    auto task = [&]() -> Task<void> {
        counter++;
        co_return;
    };

    scope.spawn(task());
    scope.spawn(task());
    scope.spawn(task());

    Runner::run([](AsyncScope& s) -> Task<void> { co_await s.join(); }(scope));

    ASSERT_EQ(counter.load(), 3);
}

TEST(CoroScopeTest, SpawnLazyExecution) {
    AsyncScope scope;
    bool executed = false;

    auto task = [&]() -> Task<void> {
        executed = true;
        co_return;
    };

    scope.spawn(task());

    // 此时 executed 应该为 true，因为 task 中没有挂起点，FireAndForget 立即执行
    ASSERT_TRUE(executed);

    Runner::run([](AsyncScope& s) -> Task<void> { co_await s.join(); }(scope));
}

TEST(CoroScopeTest, EmptyScopeJoin) {
    // 空 scope 的 join 应该立即返回
    AsyncScope scope;

    auto task = [](AsyncScope& s) -> Task<void> {
        co_await s.join();
        co_return;
    };

    Runner::run(task(scope));
    // 到达这里说明 join 正常返回
    ASSERT_TRUE(true);
}

TEST(CoroScopeTest, ManyTasks) {
    // 测试大量任务
    AsyncScope scope;
    std::atomic<int> counter{0};
    constexpr int num_tasks = 1000;

    auto task = [&]() -> Task<void> {
        counter.fetch_add(1, std::memory_order_relaxed);
        co_return;
    };

    for (int i = 0; i < num_tasks; ++i) {
        scope.spawn(task());
    }

    Runner::run([](AsyncScope& s) -> Task<void> { co_await s.join(); }(scope));

    ASSERT_EQ(counter.load(), num_tasks);
}

TEST(CoroScopeTest, TasksWithSuspension) {
    // 包含挂起点的任务
    TbbExecutor executor(4);
    AsyncScope scope;
    std::atomic<int> counter{0};

    auto task = [&]() -> Task<void> {
        co_await switch_to(executor);
        // 模拟一些工作
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
        counter.fetch_add(1);
        co_return;
    };

    for (int i = 0; i < 10; ++i) {
        scope.spawn(task());
    }

    Runner::run([](AsyncScope& s) -> Task<void> { co_await s.join(); }(scope));

    ASSERT_EQ(counter.load(), 10);
}

TEST(CoroScopeTest, TaskWithReturnValue) {
    // spawn 可以接受带返回值的任务（返回值被忽略）
    AsyncScope scope;
    std::atomic<int> sum{0};

    auto task = [&](int val) -> Task<int> {
        sum.fetch_add(val);
        co_return val * 2;  // 返回值被忽略
    };

    scope.spawn(task(1));
    scope.spawn(task(2));
    scope.spawn(task(3));

    Runner::run([](AsyncScope& s) -> Task<void> { co_await s.join(); }(scope));

    ASSERT_EQ(sum.load(), 6);
}

TEST(CoroScopeTest, NestedScopes) {
    // 嵌套的 AsyncScope
    std::atomic<int> counter{0};

    auto outer_task = [&]() -> Task<void> {
        AsyncScope outer;

        auto inner_task = [&]() -> Task<void> {
            AsyncScope inner;

            auto leaf_task = [&]() -> Task<void> {
                counter.fetch_add(1);
                co_return;
            };

            inner.spawn(leaf_task());
            inner.spawn(leaf_task());

            co_await inner.join();
            counter.fetch_add(10);
            co_return;
        };

        outer.spawn(inner_task());
        outer.spawn(inner_task());

        co_await outer.join();
        counter.fetch_add(100);
        co_return;
    };

    Runner::run(outer_task());

    // 2 个 inner_task，每个有 2 个 leaf_task (2*2=4)
    // 加上 2 个 inner_task 完成后的 10 (2*10=20)
    // 加上 outer 完成后的 100
    ASSERT_EQ(counter.load(), 4 + 20 + 100);
}

TEST(CoroScopeTest, ConcurrentSpawnAndJoin) {
    // 在多线程环境中 spawn 和 join
    TbbExecutor executor(4);
    AsyncScope scope;
    std::atomic<int> counter{0};

    auto spawner = [&]() -> Task<void> {
        co_await switch_to(executor);
        for (int i = 0; i < 50; ++i) {
            scope.spawn([&]() -> Task<void> {
                counter.fetch_add(1);
                co_return;
            }());
        }
        co_return;
    };

    // 启动多个 spawner
    AsyncScope spawner_scope;
    for (int i = 0; i < 4; ++i) {
        spawner_scope.spawn(spawner());
    }

    Runner::run([](AsyncScope& s) -> Task<void> { co_await s.join(); }(spawner_scope));
    Runner::run([](AsyncScope& s) -> Task<void> { co_await s.join(); }(scope));

    ASSERT_EQ(counter.load(), 200);  // 4 spawners * 50 tasks
}

TEST(CoroScopeTest, JoinMultipleTimes) {
    // 多次调用 join（在 scope 为空后）
    AsyncScope scope;
    std::atomic<int> counter{0};

    scope.spawn([&]() -> Task<void> {
        counter++;
        co_return;
    }());

    Runner::run([](AsyncScope& s) -> Task<void> { co_await s.join(); }(scope));
    ASSERT_EQ(counter.load(), 1);

    // 再次 join 应该立即返回
    Runner::run([](AsyncScope& s) -> Task<void> { co_await s.join(); }(scope));
    ASSERT_EQ(counter.load(), 1);
}

TEST(CoroScopeTest, SpawnAfterPartialJoin) {
    // 在部分任务完成后继续 spawn
    AsyncScope scope;
    std::atomic<int> counter{0};

    scope.spawn([&]() -> Task<void> {
        counter++;
        co_return;
    }());

    // 第一批任务完成
    Runner::run([](AsyncScope& s) -> Task<void> { co_await s.join(); }(scope));
    ASSERT_EQ(counter.load(), 1);

    // 继续 spawn 新任务
    scope.spawn([&]() -> Task<void> {
        counter++;
        co_return;
    }());
    scope.spawn([&]() -> Task<void> {
        counter++;
        co_return;
    }());

    Runner::run([](AsyncScope& s) -> Task<void> { co_await s.join(); }(scope));
    ASSERT_EQ(counter.load(), 3);
}

int main() {
    return TestRunner::instance().run_all();
}
