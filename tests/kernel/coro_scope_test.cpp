#include <corona/kernel/coro/coro.h>

#include <atomic>
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

int main() {
    return TestRunner::instance().run_all();
}
