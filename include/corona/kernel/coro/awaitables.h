#pragma once

#include <chrono>
#include <concepts>
#include <coroutine>
#include <thread>
#include <type_traits>

#include "coro_concepts.h"

namespace Corona::Kernel::Coro {

// ========================================
// 基础 Awaitable
// ========================================

/**
 * @brief 延时等待器
 *
 * 在指定时间后恢复协程执行。
 * 注意：当前实现使用 sleep，实际应用中应使用定时器调度器。
 */
class SuspendFor {
   public:
    /**
     * @brief 构造延时等待器
     *
     * @param duration 延时时长
     */
    explicit SuspendFor(std::chrono::milliseconds duration) noexcept : duration_(duration) {}

    /**
     * @brief 检查是否可以立即返回
     *
     * 如果延时为 0 或负数，则不需要挂起
     */
    [[nodiscard]] bool await_ready() const noexcept { return duration_.count() <= 0; }

    /**
     * @brief 挂起协程并在延时后恢复
     *
     * @param handle 协程句柄
     */
    void await_suspend(std::coroutine_handle<> handle) const {
        // TODO: 应该使用定时器调度器而不是阻塞 sleep
        std::this_thread::sleep_for(duration_);
        handle.resume();
    }

    /**
     * @brief 恢复后的操作（无操作）
     */
    void await_resume() const noexcept {}

   private:
    std::chrono::milliseconds duration_;
};

/**
 * @brief 便捷函数：延时指定时间
 *
 * @param duration 延时时长
 * @return SuspendFor 等待器
 */
[[nodiscard]] inline SuspendFor suspend_for(std::chrono::milliseconds duration) {
    return SuspendFor{duration};
}

/**
 * @brief 便捷函数：延时指定秒数
 *
 * @param seconds 秒数
 * @return SuspendFor 等待器
 */
[[nodiscard]] inline SuspendFor suspend_for_seconds(double seconds) {
    return SuspendFor{
        std::chrono::milliseconds{static_cast<long long>(seconds * 1000)}};
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

    void await_suspend(std::coroutine_handle<> handle) const noexcept {
        // 立即重新调度
        handle.resume();
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
 * @brief 条件等待器
 *
 * 等待直到条件满足。使用轮询方式检查条件。
 *
 * @tparam Predicate 谓词类型（必须返回 bool）
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
     */
    void await_suspend(std::coroutine_handle<> handle) const {
        // TODO: 应该使用事件驱动而不是轮询
        while (!predicate_()) {
            std::this_thread::sleep_for(poll_interval_);
        }
        handle.resume();
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

    [[nodiscard]] bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) const {
        executor_.execute([handle]() mutable { handle.resume(); });
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

    void await_suspend(std::coroutine_handle<> handle) const {
        executor_.execute_after([handle]() mutable { handle.resume(); }, delay_);
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
