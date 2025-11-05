#include "corona/kernel/utils/work_stealing_queue.h"

#include <utility>


void Corona::Kernal::Utils::WorkStealingQueue::push(Task task) {
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.push_back(std::move(task));
}

std::optional<Corona::Kernal::Utils::Task>
Corona::Kernal::Utils::WorkStealingQueue::try_pop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (tasks_.empty()) {
        return std::nullopt;
    }
    Task task = std::move(tasks_.front());
    tasks_.pop_front();
    return task;
}

std::optional<Corona::Kernal::Utils::Task>
Corona::Kernal::Utils::WorkStealingQueue::try_steal() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (tasks_.empty()) {
        return std::nullopt;
    }
    Task task = std::move(tasks_.back());
    tasks_.pop_back();
    return task;
}
