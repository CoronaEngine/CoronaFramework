#pragma once

#include <atomic>
#include <coroutine>
#include <exception>
#include <memory>
#include <tuple>
#include <variant>
#include <vector>

#include "task.h"

namespace Corona::Kernel::Coro {

namespace detail {

struct WhenAllState {
    std::atomic<size_t> counter;
    std::coroutine_handle<> awaiting_coroutine;
    std::exception_ptr exception;
    std::atomic_flag exception_set = ATOMIC_FLAG_INIT;

    explicit WhenAllState(size_t count) : counter(count) {}

    void on_task_completed() {
        // fetch_sub returns the value BEFORE subtraction
        if (counter.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            if (awaiting_coroutine) {
                awaiting_coroutine.resume();
            }
        }
    }

    void set_exception(std::exception_ptr e) {
        if (!exception_set.test_and_set(std::memory_order_acquire)) {
            exception = e;
        }
    }
};

struct WhenAllAwaiter {
    std::shared_ptr<WhenAllState> state;

    bool await_ready() const noexcept { return state->counter.load(std::memory_order_acquire) == 0; }

    void await_suspend(std::coroutine_handle<> h) noexcept {
        state->awaiting_coroutine = h;
        // Double check to avoid race if last task finished just before assignment
        if (state->counter.load(std::memory_order_acquire) == 0) {
            h.resume();
        }
    }

    void await_resume() {
        if (state->exception) {
            std::rethrow_exception(state->exception);
        }
    }
};

// Helper to handle void vs non-void results
template <typename T>
struct WhenAllResult {
    using type = T;
};

template <>
struct WhenAllResult<void> {
    using type = std::monostate;
};

template <typename T>
using when_all_result_t = typename WhenAllResult<T>::type;

template <typename T>
Task<void> when_all_wrapper(Task<T> task, std::shared_ptr<WhenAllState> state, when_all_result_t<T>* result) {
    try {
        if constexpr (std::is_void_v<T>) {
            co_await task;
            *result = std::monostate{};
        } else {
            *result = co_await task;
        }
    } catch (...) {
        state->set_exception(std::current_exception());
    }
    state->on_task_completed();
}

// Helper for variadic when_all wrapper creation
template <typename... Tasks, size_t... Is, typename ResultTuple>
auto create_wrappers(std::tuple<Tasks...>& tasks_tuple,
                     std::shared_ptr<WhenAllState> state,
                     ResultTuple& results,
                     std::index_sequence<Is...>) {
    return std::make_tuple(
        when_all_wrapper(
            std::move(std::get<Is>(tasks_tuple)),
            state,
            &std::get<Is>(results))...);
}

}  // namespace detail

// ========================================
// when_all (Variadic)
// ========================================

/**
 * @brief 并行等待多个不同类型的任务
 *
 * 启动所有传入的任务，并等待它们全部完成。
 *
 * @param tasks 任务列表
 * @return Task<std::tuple<...>> 包含所有任务结果的元组。void 任务对应 std::monostate。
 * @throws 如果任何任务抛出异常，重新抛出第一个捕获的异常。
 */
template <typename... Tasks>
Task<std::tuple<detail::when_all_result_t<typename Tasks::value_type>...>> when_all(Tasks... tasks) {
    using ResultTuple = std::tuple<detail::when_all_result_t<typename Tasks::value_type>...>;

    const size_t count = sizeof...(Tasks);
    if (count == 0) {
        co_return ResultTuple{};
    }

    auto state = std::make_shared<detail::WhenAllState>(count);
    ResultTuple results;

    // Move tasks into a tuple so we can index them
    std::tuple<Tasks...> tasks_tuple(std::move(tasks)...);

    auto wrappers = detail::create_wrappers(
        tasks_tuple,
        state,
        results,
        std::make_index_sequence<sizeof...(Tasks)>{});

    // Start all wrappers
    std::apply([](auto&... w) { (w.resume(), ...); }, wrappers);

    // Await completion
    co_await detail::WhenAllAwaiter{state};

    co_return results;
}

// ========================================
// when_all (Vector)
// ========================================

/**
 * @brief 并行等待多个相同类型的任务
 *
 * @param tasks 任务向量
 * @return Task<std::vector<...>> 包含所有任务结果的向量。
 */
template <typename T>
Task<std::vector<detail::when_all_result_t<T>>> when_all(std::vector<Task<T>> tasks) {
    using ResultType = detail::when_all_result_t<T>;

    if (tasks.empty()) {
        co_return std::vector<ResultType>{};
    }

    size_t count = tasks.size();
    auto state = std::make_shared<detail::WhenAllState>(count);
    std::vector<ResultType> results(count);
    std::vector<Task<void>> wrappers;
    wrappers.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        wrappers.push_back(detail::when_all_wrapper(
            std::move(tasks[i]),
            state,
            &results[i]));
    }

    // Start all
    for (auto& w : wrappers) {
        w.resume();
    }

    co_await detail::WhenAllAwaiter{state};

    co_return results;
}

}  // namespace Corona::Kernel::Coro
