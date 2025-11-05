#include "corona/kernel/utils/task_group.h"

#include <atomic>

void Corona::Kernal::Utils::TaskGroup::wait() {
    while (task_count_.load(std::memory_order_acquire) > 0) {
        if (!scheduler_.try_execute_task()) {
            break;
        }
    }

    int current_tasks = task_count_.load(std::memory_order_relaxed);
    while (current_tasks > 0) {
        task_count_.wait(current_tasks, std::memory_order_relaxed);
        current_tasks = task_count_.load(std::memory_order_relaxed);
    }
}