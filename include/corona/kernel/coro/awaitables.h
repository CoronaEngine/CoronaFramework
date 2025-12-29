#pragma once

#include <chrono>
#include <concepts>
#include <condition_variable>
#include <coroutine>
#include <cstdint>
#include <mutex>
#include <thread>
#include <type_traits>

#include "coro_concepts.h"

// 前向声明 Scheduler，避免循环依赖
namespace Corona::Kernel::Coro {
class Scheduler;
}  // namespace Corona::Kernel::Coro

namespace Corona::Kernel::Coro {

// ========================================
// 基础 Awaitable
// ========================================

/**
 * @brief 延时等待器
 *
 * 在指定时间后恢复协程执行。
 * 支持两种模式：
 * - 调度器模式（默认）：使用定时器调度器，不阻塞线程
 * - 阻塞模式：使用 sleep，会阻塞当前线程
 */
class SuspendFor {
   public:
    /**
     * @brief 构造延时等待器
     *
     * @param duration 延时时长
     * @param use_scheduler 是否使用调度器（默认 true）
     */
    explicit SuspendFor(std::chrono::milliseconds duration,
                        bool use_scheduler = true) noexcept
        : duration_(duration), use_scheduler_(use_scheduler) {}

    /**
     * @brief 检查是否可以立即返回
     *
     * 如果延时为 0 或负数，则不需要挂起
     */
    [[nodiscard]] bool await_ready() const noexcept { return duration_.count() <= 0; }

    /**
     * @brief 挂起协程并在延时后恢复
     *
     * 如果使用调度器模式，将协程提交到调度器的定时队列，不阻塞线程。
     * 如果使用阻塞模式，使用 sleep 阻塞当前线程。
     *
     * @param handle 协程句柄
     * @return noop_coroutine（调度器模式）或 handle（阻塞模式）
     */
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) const;

    /**
     * @brief 恢复后的操作（无操作）
     */
    void await_resume() const noexcept {}

   private:
    std::chrono::milliseconds duration_;
    bool use_scheduler_;
};

/**
 * @brief 便捷函数：延时指定时间（使用调度器，不阻塞线程）
 *
 * @param duration 延时时长
 * @return SuspendFor 等待器
 */
[[nodiscard]] inline SuspendFor suspend_for(std::chrono::milliseconds duration) {
    return SuspendFor{duration, true};
}

/**
 * @brief 便捷函数：延时指定时间（阻塞模式）
 *
 * @param duration 延时时长
 * @return SuspendFor 等待器
 */
[[nodiscard]] inline SuspendFor suspend_for_blocking(std::chrono::milliseconds duration) {
    return SuspendFor{duration, false};
}

/**
 * @brief 便捷函数：延时指定秒数（阻塞模式，与 Task::get() 兼容）
 *
 * @param seconds 秒数
 * @return SuspendFor 等待器
 */
[[nodiscard]] inline SuspendFor suspend_for_seconds(double seconds) {
    return SuspendFor{
        std::chrono::milliseconds{static_cast<std::int64_t>(seconds * 1000)}, false};
}

// ========================================
// 让出执行
// ========================================

/**
 * @brief 让出当前时间片
 *
 * 挂起当前协程并立即重新调度，允许其他协程执行。
 */
struct Yield {
    [[nodiscard]] bool await_ready() const noexcept { return false; }

    /**
     * @brief 挂起并立即恢复
     *
     * 使用对称转移确保安全恢复协程。
     *
     * @param handle 协程句柄
     * @return 对称转移到的协程句柄
     */
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) const noexcept {
        // 对称转移：立即重新调度
        return handle;
    }

    void await_resume() const noexcept {}
};

/**
 * @brief 便捷函数：让出执行
 *
 * @return Yield 等待器
 */
[[nodiscard]] inline Yield yield() noexcept { return Yield{}; }

// ========================================
// 条件等待
// ========================================

/**
 * @brief 事件驱动的条件变量等待器
 *
 * 使用 std::condition_variable 实现真正的事件驱动等待，
 * 避免轮询带来的 CPU 浪费。外部代码在条件可能改变时调用 notify()。
 *
 * 使用示例：
 * @code
 * auto cond = std::make_shared<ConditionVariable>();
 *
 * // 在协程中等待
 * co_await cond->wait([&]() { return data_ready; });
 *
 * // 在其他地方通知
 * data_ready = true;
 * cond->notify_one();
 * @endcode
 */
class ConditionVariable : public std::enable_shared_from_this<ConditionVariable> {
   public:
    ConditionVariable() = default;

    // 禁止拷贝和移动
    ConditionVariable(const ConditionVariable&) = delete;
    ConditionVariable& operator=(const ConditionVariable&) = delete;
    ConditionVariable(ConditionVariable&&) = delete;
    ConditionVariable& operator=(ConditionVariable&&) = delete;

    /**
     * @brief 通知一个等待者
     */
    void notify_one() noexcept {
        std::lock_guard lock(mutex_);
        cv_.notify_one();
    }

    /**
     * @brief 通知所有等待者
     */
    void notify_all() noexcept {
        std::lock_guard lock(mutex_);
        cv_.notify_all();
    }

    /**
     * @brief 等待条件满足的 Awaitable
     *
     * @tparam Predicate 谓词类型
     */
    template <typename Predicate>
        requires BoolPredicate<Predicate>
    class WaitAwaitable {
       public:
        WaitAwaitable(std::shared_ptr<ConditionVariable> cv, Predicate pred)
            : cv_(std::move(cv)), predicate_(std::move(pred)) {}

        [[nodiscard]] bool await_ready() const { return predicate_(); }

        /**
         * @brief 事件驱动等待
         *
         * 使用条件变量等待，而非轮询。当 notify_one/notify_all 被调用时唤醒。
         *
         * @param handle 协程句柄
         * @return 对称转移到的协程句柄
         */
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) {
            std::unique_lock lock(cv_->mutex_);
            cv_->cv_.wait(lock, [this]() { return predicate_(); });
            return handle;  // 对称转移：编译器负责安全恢复协程
        }

        void await_resume() const noexcept {}

       private:
        std::shared_ptr<ConditionVariable> cv_;
        Predicate predicate_;
    };

    /**
     * @brief 带超时的等待 Awaitable
     *
     * @tparam Predicate 谓词类型
     */
    template <typename Predicate>
        requires BoolPredicate<Predicate>
    class WaitForAwaitable {
       public:
        WaitForAwaitable(std::shared_ptr<ConditionVariable> cv, Predicate pred,
                         std::chrono::milliseconds timeout)
            : cv_(std::move(cv)), predicate_(std::move(pred)), timeout_(timeout) {}

        [[nodiscard]] bool await_ready() const { return predicate_(); }

        /**
         * @brief 带超时的事件驱动等待
         *
         * @param handle 协程句柄
         * @return 对称转移到的协程句柄
         */
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) {
            std::unique_lock lock(cv_->mutex_);
            timed_out_ = !cv_->cv_.wait_for(lock, timeout_, [this]() { return predicate_(); });
            return handle;
        }

        /**
         * @brief 返回是否超时
         *
         * @return 如果超时返回 true，条件满足返回 false
         */
        [[nodiscard]] bool await_resume() const noexcept { return timed_out_; }

       private:
        std::shared_ptr<ConditionVariable> cv_;
        Predicate predicate_;
        std::chrono::milliseconds timeout_;
        mutable bool timed_out_ = false;
    };

    /**
     * @brief 创建等待 Awaitable
     *
     * @tparam Predicate 谓词类型
     * @param pred 条件谓词
     * @return WaitAwaitable
     */
    template <typename Predicate>
        requires BoolPredicate<Predicate>
    [[nodiscard]] WaitAwaitable<std::decay_t<Predicate>> wait(Predicate&& pred) {
        return WaitAwaitable<std::decay_t<Predicate>>{shared_from_this(),
                                                      std::forward<Predicate>(pred)};
    }

    /**
     * @brief 创建带超时的等待 Awaitable
     *
     * @tparam Predicate 谓词类型
     * @param pred 条件谓词
     * @param timeout 超时时间
     * @return WaitForAwaitable，await_resume() 返回是否超时
     */
    template <typename Predicate>
        requires BoolPredicate<Predicate>
    [[nodiscard]] WaitForAwaitable<std::decay_t<Predicate>> wait_for(
        Predicate&& pred, std::chrono::milliseconds timeout) {
        return WaitForAwaitable<std::decay_t<Predicate>>{
            shared_from_this(), std::forward<Predicate>(pred), timeout};
    }

   private:
    std::mutex mutex_;
    std::condition_variable cv_;
};

/**
 * @brief 便捷函数：创建条件变量
 *
 * @return ConditionVariable 的智能指针
 */
[[nodiscard]] inline std::shared_ptr<ConditionVariable> make_condition_variable() {
    return std::make_shared<ConditionVariable>();
}

/**
 * @brief 轮询式条件等待器（已废弃，建议使用 ConditionVariable）
 *
 * 等待直到条件满足。使用轮询方式检查条件。
 * 对于需要事件驱动的场景，请使用 ConditionVariable 类。
 *
 * @tparam Predicate 谓词类型（必须返回 bool）
 * @deprecated 推荐使用 ConditionVariable::wait() 实现事件驱动等待
 */
template <typename Predicate>
    requires BoolPredicate<Predicate>
class WaitUntil {
   public:
    /**
     * @brief 构造条件等待器
     *
     * @param pred 条件谓词
     * @param poll_interval 轮询间隔
     */
    explicit WaitUntil(Predicate pred,
                       std::chrono::milliseconds poll_interval = std::chrono::milliseconds{10})
        : predicate_(std::move(pred)), poll_interval_(poll_interval) {}

    /**
     * @brief 检查条件是否已满足
     */
    [[nodiscard]] bool await_ready() const { return predicate_(); }

    /**
     * @brief 轮询等待条件满足
     *
     * 注意：此实现使用轮询，会消耗 CPU 资源。
     * 对于需要高效等待的场景，请使用 ConditionVariable。
     *
     * @param handle 协程句柄
     * @return 对称转移到的协程句柄
     */
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) const {
        while (!predicate_()) {
            std::this_thread::sleep_for(poll_interval_);
        }
        return handle;  // 对称转移：编译器负责安全恢复协程
    }

    void await_resume() const noexcept {}

   private:
    Predicate predicate_;
    std::chrono::milliseconds poll_interval_;
};

/**
 * @brief 便捷函数：等待条件满足
 *
 * @tparam Predicate 谓词类型
 * @param pred 条件谓词
 * @return WaitUntil 等待器
 */
template <typename Predicate>
[[nodiscard]] WaitUntil<std::decay_t<Predicate>> wait_until(Predicate&& pred) {
    return WaitUntil<std::decay_t<Predicate>>{std::forward<Predicate>(pred)};
}

/**
 * @brief 便捷函数：等待条件满足（带轮询间隔）
 *
 * @tparam Predicate 谓词类型
 * @param pred 条件谓词
 * @param poll_interval 轮询间隔
 * @return WaitUntil 等待器
 */
template <typename Predicate>
[[nodiscard]] WaitUntil<std::decay_t<Predicate>> wait_until(
    Predicate&& pred, std::chrono::milliseconds poll_interval) {
    return WaitUntil<std::decay_t<Predicate>>{std::forward<Predicate>(pred), poll_interval};
}

// ========================================
// 执行器相关 Awaitable
// ========================================

/**
 * @brief 切换到指定执行器
 *
 * 挂起当前协程，并在指定执行器上恢复执行。
 *
 * @tparam E 执行器类型
 */
template <Executor E>
class SwitchToExecutor {
   public:
    explicit SwitchToExecutor(E& executor) noexcept : executor_(executor) {}

    /**
     * @brief 检查是否已在目标执行器线程上
     *
     * 如果已经在目标执行器线程，可以直接恢复而无需切换。
     */
    [[nodiscard]] bool await_ready() const noexcept {
        return executor_.is_in_executor_thread();
    }

    /**
     * @brief 切换到目标执行器
     *
     * 如果执行器是同步的（如 InlineExecutor），使用对称转移确保安全。
     * 如果执行器是异步的（如 TbbExecutor），则真正挂起协程。
     *
     * @param handle 协程句柄
     * @return noop_coroutine 表示真正挂起，等待异步恢复
     */
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) const {
        executor_.execute([handle]() mutable { handle.resume(); });
        return std::noop_coroutine();  // 真正挂起，等待执行器异步恢复
    }

    void await_resume() const noexcept {}

   private:
    E& executor_;
};

/**
 * @brief 便捷函数：切换到指定执行器
 *
 * @tparam E 执行器类型
 * @param executor 执行器引用
 * @return SwitchToExecutor 等待器
 */
template <Executor E>
[[nodiscard]] SwitchToExecutor<E> switch_to(E& executor) {
    return SwitchToExecutor<E>{executor};
}

/**
 * @brief 在执行器上延时执行
 *
 * @tparam E 执行器类型
 */
template <Executor E>
class SwitchToExecutorAfter {
   public:
    SwitchToExecutorAfter(E& executor, std::chrono::milliseconds delay) noexcept
        : executor_(executor), delay_(delay) {}

    [[nodiscard]] bool await_ready() const noexcept { return delay_.count() <= 0; }

    /**
     * @brief 延迟后切换到目标执行器
     *
     * @param handle 协程句柄
     * @return noop_coroutine 表示真正挂起，等待延迟后异步恢复
     */
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) const {
        executor_.execute_after([handle]() mutable { handle.resume(); }, delay_);
        return std::noop_coroutine();  // 真正挂起，等待执行器延迟后恢复
    }

    void await_resume() const noexcept {}

   private:
    E& executor_;
    std::chrono::milliseconds delay_;
};

/**
 * @brief 便捷函数：在执行器上延时执行
 *
 * @tparam E 执行器类型
 * @param executor 执行器引用
 * @param delay 延时时长
 * @return SwitchToExecutorAfter 等待器
 */
template <Executor E>
[[nodiscard]] SwitchToExecutorAfter<E> switch_to_after(E& executor,
                                                       std::chrono::milliseconds delay) {
    return SwitchToExecutorAfter<E>{executor, delay};
}

// ========================================
// 立即返回 Awaitable
// ========================================

/**
 * @brief 立即返回值的 Awaitable
 *
 * 用于将普通值包装为可 co_await 的形式
 *
 * @tparam T 值类型
 */
template <typename T>
class ReadyValue {
   public:
    explicit ReadyValue(T value) : value_(std::move(value)) {}

    [[nodiscard]] bool await_ready() const noexcept { return true; }

    void await_suspend(std::coroutine_handle<>) const noexcept {
        // 永远不会被调用
    }

    T await_resume() { return std::move(value_); }

   private:
    T value_;
};

/**
 * @brief void 特化
 */
template <>
class ReadyValue<void> {
   public:
    [[nodiscard]] bool await_ready() const noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    void await_resume() const noexcept {}
};

/**
 * @brief 便捷函数：创建立即返回的 Awaitable
 *
 * @tparam T 值类型
 * @param value 值
 * @return ReadyValue 等待器
 */
template <typename T>
[[nodiscard]] ReadyValue<std::decay_t<T>> ready(T&& value) {
    return ReadyValue<std::decay_t<T>>{std::forward<T>(value)};
}

/**
 * @brief 便捷函数：创建立即返回的 void Awaitable
 */
[[nodiscard]] inline ReadyValue<void> ready() { return ReadyValue<void>{}; }

}  // namespace Corona::Kernel::Coro

// ========================================
// SuspendFor 的延迟实现（需要 Scheduler）
// ========================================

#include "scheduler.h"

namespace Corona::Kernel::Coro {

inline std::coroutine_handle<> SuspendFor::await_suspend(
    std::coroutine_handle<> handle) const {
    if (use_scheduler_) {
        // 使用调度器：提交到定时队列，不阻塞线程
        Scheduler::instance().schedule_after(handle, duration_);
        return std::noop_coroutine();  // 真正挂起，等待调度器异步恢复
    } else {
        // 阻塞模式：使用 sleep
        std::this_thread::sleep_for(duration_);
        return handle;  // 对称转移：编译器负责安全恢复协程
    }
}

}  // namespace Corona::Kernel::Coro
