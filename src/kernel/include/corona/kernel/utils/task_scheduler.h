#pragma once

#include <corona/kernel/utils/work_stealing_queue.h>

namespace Corona::Kernal::Utils {
class TaskScheduler final {
   public:
    static TaskScheduler& instance();

    TaskScheduler(const TaskScheduler&) = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;

    void submit(Task task);
    bool try_execute_task();

   private:
    TaskScheduler();
    ~TaskScheduler();

    void worker_loop(std::uint32_t index, std::stop_token stop_token);
    bool try_steal_and_execute(std::optional<std::uint32_t> index = std::nullopt);
    bool has_any_task() const;

   private:
    const uint32_t thread_count_;
    std::vector<std::jthread> threads_;
    std::vector<WorkStealingQueue> task_queues_;
    std::atomic<uint32_t> submission_index_{0};

    std::stop_source stop_source_;

    // 用于唤醒休眠线程的条件变量
    std::condition_variable cv_;
    std::mutex cv_mutex_;
};
}  // namespace Corona::Kernal::Utils