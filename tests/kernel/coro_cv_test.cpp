/**
 * @file coro_cv_test.cpp
 * @brief ConditionVariable 异步特性测试
 *
 * 专门验证 ConditionVariable 的非阻塞特性和异步超时机制
 */

#include <corona/kernel/coro/coro.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <thread>
#include <vector>

#include "../test_framework.h"

using namespace Corona::Kernel::Coro;
using namespace CoronaTest;

// ========================================
// 辅助工具：手动单线程执行器
// ========================================

/**
 * @brief 手动控制的单线程执行器
 *
 * 用于确定性地测试协程挂起和恢复顺序。
 * 任务被放入队列，必须显式调用 run_next() 或 run_all() 执行。
 */
class ManualExecutor : public IExecutor {
   public:
    void execute(std::function<void()> task) override {
        std::lock_guard lock(mutex_);
        tasks_.push_back(std::move(task));
    }

    void execute_after(std::function<void()> task, std::chrono::milliseconds delay) override {
        execute(std::move(task));
    }

    [[nodiscard]] bool is_in_executor_thread() const override {
        return std::this_thread::get_id() == owner_thread_id_;
    }

    void set_owner_thread(std::thread::id id) {
        owner_thread_id_ = id;
    }

    // 执行下一个任务
    bool run_next() {
        std::function<void()> task;
        {
            std::lock_guard lock(mutex_);
            if (tasks_.empty()) return false;
            task = std::move(tasks_.front());
            tasks_.pop_front();
        }
        // 设置当前线程为 owner，模拟在执行器线程运行
        auto old_id = owner_thread_id_;
        owner_thread_id_ = std::this_thread::get_id();
        task();
        owner_thread_id_ = old_id;
        return true;
    }

    // 执行所有任务
    size_t run_all() {
        size_t count = 0;
        while (run_next()) {
            count++;
        }
        return count;
    }

    size_t task_count() const {
        std::lock_guard lock(mutex_);
        return tasks_.size();
    }

   private:
    mutable std::mutex mutex_;
    std::deque<std::function<void()>> tasks_;
    std::thread::id owner_thread_id_;
};

// ========================================
// 测试用例
// ========================================

TEST(CoroCV, AsyncWaitDoesNotBlock) {
    // 验证核心特性：等待 CV 时协程挂起，但不阻塞线程

    ManualExecutor executor;
    auto cv = make_condition_variable();
    bool task1_started = false;
    bool task1_resumed = false;
    bool task2_executed = false;
    bool condition = false;

    // 任务 1: 等待条件
    auto task1 = [&]() -> Task<void> {
        task1_started = true;
        // 切换到手动执行器
        co_await switch_to(executor);

        // 等待条件（应该挂起，交出控制权）
        co_await cv->wait([&]() { return condition; });

        task1_resumed = true;
        co_return;
    }();

    // 任务 2: 通知条件
    auto task2 = [&]() -> Task<void> {
        // 切换到手动执行器
        co_await switch_to(executor);

        task2_executed = true;
        condition = true;
        cv->notify_one();
        co_return;
    }();

    // 启动任务（这会将它们加入 executor 队列）
    task1.resume();
    task2.resume();

    // 此时 executor 应该有两个任务（switch_to 的回调）
    ASSERT_GE(executor.task_count(), 1u);

    // 运行第一个任务 (Task 1)
    // 它会执行到 cv->wait，发现条件不满足，然后挂起
    // 关键点：如果它是阻塞等待，run_next() 将不会返回，或者 task2 永远没机会运行
    executor.run_next();

    ASSERT_TRUE(task1_started);
    ASSERT_FALSE(task1_resumed);  // 应该挂起了

    // 运行第二个任务 (Task 2)
    // 只有在 Task 1 真正挂起（非阻塞）的情况下，Task 2 才能在同一个单线程执行器中运行
    executor.run_next();

    ASSERT_TRUE(task2_executed);

    // Task 2 notify 之后，Task 1 应该被重新加入队列
    // 注意：当前的 ConditionVariable 实现是直接 resume，不经过 executor
    // 所以 Task 1 会在 Task 2 调用 notify_one 时立即在当前线程恢复
    // 这意味着 task1_resumed 应该已经为 true
    // ASSERT_GE(executor.task_count(), 1u);
    // executor.run_next();

    ASSERT_TRUE(task1_resumed);
}

TEST(CoroCV, AsyncTimeout) {
    // 验证超时机制是异步的

    // 使用 TbbExecutor 确保有真实的时间调度
    TbbExecutor executor(2);
    auto cv = make_condition_variable();
    std::atomic<bool> timed_out{false};
    std::atomic<bool> task_done{false};

    auto task = [&]() -> Task<void> {
        co_await switch_to(executor);

        // 等待一个永远不会满足的条件，超时 50ms
        bool result = co_await cv->wait_for([]() { return false; }, std::chrono::milliseconds{50});

        timed_out = result;
        task_done = true;
        co_return;
    }();

    auto start = std::chrono::steady_clock::now();
    task.resume();

    // 等待任务完成
    while (!task_done) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    ASSERT_TRUE(timed_out);
    ASSERT_GE(elapsed.count(), 40);  // 至少等待了超时时间
}

TEST(CoroCV, FifoWakeup) {
    // 验证唤醒顺序是 FIFO

    ManualExecutor executor;
    auto cv = make_condition_variable();
    std::vector<int> wake_order;
    bool condition = false;

    auto make_waiter = [&](int id) -> Task<void> {
        co_await switch_to(executor);
        co_await cv->wait([&]() { return condition; });
        wake_order.push_back(id);
    };

    auto w1 = make_waiter(1);
    auto w2 = make_waiter(2);
    auto w3 = make_waiter(3);

    w1.resume();
    w2.resume();
    w3.resume();

    // 执行 switch_to，让它们都进入 wait 状态
    executor.run_all();
    ASSERT_TRUE(wake_order.empty());

    // 满足条件
    condition = true;

    // 唤醒第一个
    cv->notify_one();
    // 注意：ConditionVariable 直接恢复协程，不经过 executor
    // 所以协程会在当前线程立即执行
    ASSERT_EQ(wake_order.size(), 1u);
    ASSERT_EQ(wake_order[0], 1);

    // 唤醒第二个
    cv->notify_one();
    ASSERT_EQ(wake_order.size(), 2u);
    ASSERT_EQ(wake_order[1], 2);

    // 唤醒第三个
    cv->notify_one();
    ASSERT_EQ(wake_order.size(), 3u);
    ASSERT_EQ(wake_order[2], 3);
}

TEST(CoroCV, NotifyAllAsync) {
    ManualExecutor executor;
    auto cv = make_condition_variable();
    int wake_count = 0;
    bool condition = false;

    auto make_waiter = [&]() -> Task<void> {
        co_await switch_to(executor);
        co_await cv->wait([&]() { return condition; });
        wake_count++;
    };

    std::vector<Task<void>> tasks;
    for (int i = 0; i < 5; ++i) {
        tasks.push_back(make_waiter());
        tasks.back().resume();
    }

    // 让所有任务进入等待
    executor.run_all();
    ASSERT_EQ(wake_count, 0);

    // 广播
    condition = true;
    cv->notify_all();

    // 所有任务立即恢复
    ASSERT_EQ(wake_count, 5);
}

TEST(CoroCV, TimeoutVsNotifyRace) {
    // 测试超时和通知的竞态：如果几乎同时发生，只能生效一个

    TbbExecutor executor(4);
    auto cv = make_condition_variable();
    std::atomic<int> success_count{0};
    std::atomic<int> timeout_count{0};
    std::atomic<bool> condition{false};
    constexpr int num_tasks = 100;

    std::vector<Task<void>> tasks;
    for (int i = 0; i < num_tasks; ++i) {
        tasks.push_back([&]() -> Task<void> {
            co_await switch_to(executor);
            // 等待 10ms
            bool timed_out = co_await cv->wait_for([&]() { return condition.load(); }, std::chrono::milliseconds{10});
            if (timed_out) {
                timeout_count++;
            } else {
                success_count++;
            }
        }());
        tasks.back().resume();
    }

    // 在 10ms 左右通知
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    condition = true;
    cv->notify_all();

    // 等待所有任务完成
    // 这里简单等待一段时间
    std::this_thread::sleep_for(std::chrono::milliseconds{100});

    // 总数应该等于 num_tasks
    // 具体的分布取决于调度，但不能有丢失或重复
    ASSERT_EQ(success_count.load() + timeout_count.load(), num_tasks);
}

int main() {
    return TestRunner::instance().run_all();
}
