#pragma once

#include <deque>
#include <functional>
#include <mutex>
#include <optional>

namespace Corona::Kernal::Utils {
using Task = std::function<void()>;  ///< 任务类型定义

class WorkStealingQueue {
   public:
    WorkStealingQueue() = default;
    ~WorkStealingQueue() = default;

    void push(Task task);

    [[nodiscard]] std::optional<Task> try_pop();
    [[nodiscard]] std::optional<Task> try_steal();

   private:
    std::deque<Task> tasks_;  ///< 任务队列
    std::mutex mutex_;        ///< 保护任务队列的互斥锁
};

}  // namespace Corona::Kernal::Utils