#pragma once

#include <concepts>
#include <iostream>

#include "corona/kernel/utils/task_scheduler.h"

namespace Corona::Kernal::Utils {

class TaskGroup {
   public:
    TaskGroup() = default;
    TaskGroup(const TaskGroup&) = delete;
    TaskGroup& operator=(const TaskGroup&) = delete;
    ~TaskGroup() = default;

    template <std::invocable F, typename... Args>
    void run(F&& func, Args&&... args) {
        task_count_.fetch_add(1, std::memory_order_relaxed);
        auto task_wrapper = [this, func = std::forward<F>(func), ... args = std::forward<Args>(args)]() {
            try {
                func(std::forward<Args>(args)...);
            } catch (std::exception& e) {
                // 处理异常，例如记录日志
                std::cerr << "Error occurred: " << e.what() << std::endl;
            }

            if (task_count_.fetch_sub(1, std::memory_order_relaxed) == 1) {
                task_count_.notify_all();
            }
        };
        scheduler_.submit(std::move(task_wrapper));
    }

    void wait();

   private:
    TaskScheduler& scheduler_{TaskScheduler::instance()};
    std::atomic<std::size_t> task_count_{0};
};

}  // namespace Corona::Kernal::Utils