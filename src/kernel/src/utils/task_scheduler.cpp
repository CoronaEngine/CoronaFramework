#include "corona/kernel/utils/task_scheduler.h"

#include <random>

Corona::Kernal::Utils::TaskScheduler& Corona::Kernal::Utils::TaskScheduler::instance() {
    static TaskScheduler instance;
    return instance;
}

Corona::Kernal::Utils::TaskScheduler::TaskScheduler()
    : thread_count_(std::thread::hardware_concurrency()),
      task_queues_(thread_count_),
      threads_(thread_count_) {
    for (std::size_t i = 0; i < thread_count_; ++i) {
        threads_.emplace_back(&TaskScheduler::worker_loop, this, i, stop_source_.get_token());
    }
}

Corona::Kernal::Utils::TaskScheduler::~TaskScheduler() {
    stop_source_.request_stop();
    cv_.notify_all();
}

void Corona::Kernal::Utils::TaskScheduler::submit(Task task) {
    uint32_t index = submission_index_.fetch_add(1) % thread_count_;
    task_queues_[index].push(std::move(task));
    cv_.notify_one();
}

bool Corona::Kernal::Utils::TaskScheduler::try_execute_task() {
    return try_steal_and_execute();
}

void Corona::Kernal::Utils::TaskScheduler::worker_loop(std::uint32_t index, std::stop_token stop_token) {
    while (!stop_token.stop_requested()) {
        if (auto task = task_queues_[index].try_pop()) {
            (*task)();
        } else if (try_steal_and_execute(index)) {
            continue;
        } else {
            std::unique_lock lock(cv_mutex_);
            cv_.wait(lock, [this, &stop_token, index]() {
                return stop_token.stop_requested() || has_any_task();
            });
        }
    }
}

bool Corona::Kernal::Utils::TaskScheduler::try_steal_and_execute(std::optional<std::uint32_t> index) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<std::uint32_t> dis(0, thread_count_ - 1);
    std::uint32_t start_index = dis(gen);

    for (std::uint32_t i = 0; i < thread_count_; ++i) {
        std::uint32_t target_index = (start_index + i) % thread_count_;
        if (index.has_value() && target_index == index.value()) {
            continue;
        }
        if (auto task = task_queues_[target_index].try_steal()) {
            (*task)();
            return true;
        }
    }
    return false;
}

bool Corona::Kernal::Utils::TaskScheduler::has_any_task() const {
    return true;;  // 简化实现，始终返回 true
}