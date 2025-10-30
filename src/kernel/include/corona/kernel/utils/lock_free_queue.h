#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <utility>

namespace Corona::Kernel::Utils {

template <typename T>
class LockFreeQueue {
   public:
    LockFreeQueue() {
        auto dummy = std::make_shared<Node>();
        head_.store(dummy, std::memory_order_relaxed);
        tail_.store(std::move(dummy), std::memory_order_relaxed);
    }

    ~LockFreeQueue() {
        T value;
        while (dequeue(value)) {
        }
    }

    LockFreeQueue(const LockFreeQueue&) = delete;
    LockFreeQueue& operator=(const LockFreeQueue&) = delete;
    LockFreeQueue(LockFreeQueue&&) = delete;
    LockFreeQueue& operator=(LockFreeQueue&&) = delete;

    template <typename U>
    void enqueue(U&& value) {
        auto new_node = std::make_shared<Node>(std::forward<U>(value));
        std::shared_ptr<Node> tail;

        while (true) {
            tail = tail_.load(std::memory_order_acquire);
            auto next = tail->next.load(std::memory_order_acquire);

            if (tail != tail_.load(std::memory_order_acquire)) {
                continue;
            }

            if (!next) {
                if (tail->next.compare_exchange_weak(next, new_node,
                                                     std::memory_order_release,
                                                     std::memory_order_relaxed)) {
                    break;
                }
            } else {
                tail_.compare_exchange_weak(tail, next, std::memory_order_release,
                                            std::memory_order_relaxed);
            }
        }

        tail_.compare_exchange_strong(tail, std::move(new_node), std::memory_order_release,
                                      std::memory_order_relaxed);
    }

    bool dequeue(T& value) {
        std::shared_ptr<Node> head;

        while (true) {
            head = head_.load(std::memory_order_acquire);
            auto tail = tail_.load(std::memory_order_acquire);
            auto next = head->next.load(std::memory_order_acquire);

            if (head != head_.load(std::memory_order_acquire)) {
                continue;
            }

            if (!next) {
                return false;
            }

            if (head == tail) {
                tail_.compare_exchange_weak(tail, next, std::memory_order_release,
                                            std::memory_order_relaxed);
                continue;
            }

            if (head_.compare_exchange_weak(head, next, std::memory_order_acquire,
                                            std::memory_order_relaxed)) {
                value = std::move(next->value.value());
                next->value.reset();
                break;
            }
        }

        return true;
    }

    [[nodiscard]] bool empty() const {
        auto head = head_.load(std::memory_order_acquire);
        auto next = head->next.load(std::memory_order_acquire);
        return !next;
    }

   private:
    struct Node {
        Node() noexcept = default;

        template <typename U>
        explicit Node(U&& v) : value(std::forward<U>(v)) {}

        std::atomic<std::shared_ptr<Node>> next{nullptr};
        std::optional<T> value;
    };

    std::atomic<std::shared_ptr<Node>> head_{nullptr};
    std::atomic<std::shared_ptr<Node>> tail_{nullptr};
};

}  // namespace Corona::Kernel::Utils