#pragma once
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace Corona::Kernel {

// ========================================
// Concepts
// ========================================

// Event concept: must be copyable and movable
template <typename T>
concept Event = std::is_copy_constructible_v<T> && std::is_move_constructible_v<T>;

// ========================================
// Event Stream Configuration
// ========================================

// Backpressure policy when queue is full
enum class BackpressurePolicy {
    Block,        // Block publisher until space available
    DropOldest,   // Remove oldest event and add new one
    DropNewest    // Drop new event if queue is full
};

struct EventStreamOptions {
    std::size_t max_queue_size = 256;
    BackpressurePolicy policy = BackpressurePolicy::Block;
};

// ========================================
// Event Subscription (RAII)
// ========================================

template <Event T>
class EventSubscription {
   public:
    EventSubscription() = default;
    EventSubscription(EventSubscription&&) noexcept = default;
    EventSubscription& operator=(EventSubscription&&) noexcept = default;
    ~EventSubscription();

    EventSubscription(const EventSubscription&) = delete;
    EventSubscription& operator=(const EventSubscription&) = delete;

    // Check if subscription is valid
    bool is_valid() const;

    // Try to pop an event (non-blocking)
    std::optional<T> try_pop();

    // Wait for an event with timeout
    std::optional<T> wait_for(std::chrono::milliseconds timeout);

    // Wait for an event indefinitely
    std::optional<T> wait();

    // Close the subscription
    void close();

   private:
    template <Event U>
    friend class EventStream;

    struct State;

    explicit EventSubscription(std::shared_ptr<State> state);

    void release();

    std::shared_ptr<State> state_;
};

// ========================================
// Event Stream (Queue-based Pub-Sub)
// ========================================

template <Event T>
class EventStream {
   public:
    using value_type = T;

    EventStream() = default;
    ~EventStream() = default;

    EventStream(const EventStream&) = delete;
    EventStream& operator=(const EventStream&) = delete;
    EventStream(EventStream&&) = delete;
    EventStream& operator=(EventStream&&) = delete;

    // Subscribe to the event stream
    EventSubscription<T> subscribe(EventStreamOptions options = {});

    // Publish an event (copy)
    void publish(const T& event);

    // Publish an event (move)
    void publish(T&& event);

    // Get number of active subscribers
    std::size_t subscriber_count() const;

   private:
    friend class EventSubscription<T>;
    
    struct SubscriberState {
        std::size_t id;
        EventStreamOptions options;
        std::deque<T> queue;
        std::mutex mutex;
        std::condition_variable cv;
        bool closed = false;
    };

    void publish_impl(const T& event);
    void unsubscribe(std::size_t id);

    mutable std::mutex subscribers_mutex_;
    std::unordered_map<std::size_t, std::shared_ptr<SubscriberState>> subscribers_;
    std::size_t next_id_ = 1;
};

// ========================================
// Event Bus with Stream Support
// ========================================

class IEventBusStream {
   public:
    virtual ~IEventBusStream() = default;

    // Create or get an event stream for a specific event type
    template <Event T>
    std::shared_ptr<EventStream<T>> get_stream();

   protected:
    virtual std::shared_ptr<void> get_stream_impl(std::type_index type) = 0;
    virtual std::shared_ptr<void> create_stream_impl(std::type_index type) = 0;
};

// ========================================
// EventSubscription Implementation
// ========================================

template <Event T>
struct EventSubscription<T>::State {
    std::shared_ptr<typename EventStream<T>::SubscriberState> subscriber;
    std::size_t stream_id;  // For unsubscribing
};

template <Event T>
EventSubscription<T>::EventSubscription(std::shared_ptr<State> state)
    : state_(std::move(state)) {}

template <Event T>
EventSubscription<T>::~EventSubscription() {
    release();
}

template <Event T>
bool EventSubscription<T>::is_valid() const {
    return state_ && state_->subscriber && !state_->subscriber->closed;
}

template <Event T>
void EventSubscription<T>::close() {
    release();
}

template <Event T>
void EventSubscription<T>::release() {
    if (!state_ || !state_->subscriber) {
        return;
    }

    auto sub = state_->subscriber;
    state_.reset();

    std::lock_guard<std::mutex> lock(sub->mutex);
    sub->closed = true;
    sub->queue.clear();
    sub->cv.notify_all();
}

template <Event T>
std::optional<T> EventSubscription<T>::try_pop() {
    if (!state_ || !state_->subscriber) {
        return std::nullopt;
    }

    auto sub = state_->subscriber;
    std::lock_guard<std::mutex> lock(sub->mutex);

    if (sub->queue.empty() || sub->closed) {
        return std::nullopt;
    }

    T event = std::move(sub->queue.front());
    sub->queue.pop_front();
    sub->cv.notify_all();  // Notify publisher if blocking
    return event;
}

template <Event T>
std::optional<T> EventSubscription<T>::wait_for(std::chrono::milliseconds timeout) {
    if (!state_ || !state_->subscriber) {
        return std::nullopt;
    }

    auto sub = state_->subscriber;
    std::unique_lock<std::mutex> lock(sub->mutex);

    if (!sub->cv.wait_for(lock, timeout, [&] { return sub->closed || !sub->queue.empty(); })) {
        return std::nullopt;  // Timeout
    }

    if (sub->closed || sub->queue.empty()) {
        return std::nullopt;
    }

    T event = std::move(sub->queue.front());
    sub->queue.pop_front();
    sub->cv.notify_all();
    return event;
}

template <Event T>
std::optional<T> EventSubscription<T>::wait() {
    if (!state_ || !state_->subscriber) {
        return std::nullopt;
    }

    auto sub = state_->subscriber;
    std::unique_lock<std::mutex> lock(sub->mutex);

    sub->cv.wait(lock, [&] { return sub->closed || !sub->queue.empty(); });

    if (sub->closed || sub->queue.empty()) {
        return std::nullopt;
    }

    T event = std::move(sub->queue.front());
    sub->queue.pop_front();
    sub->cv.notify_all();
    return event;
}

// ========================================
// EventStream Implementation
// ========================================

template <Event T>
EventSubscription<T> EventStream<T>::subscribe(EventStreamOptions options) {
    auto sub = std::make_shared<SubscriberState>();
    sub->id = next_id_++;
    sub->options = options;

    if (sub->options.max_queue_size == 0) {
        sub->options.max_queue_size = 1;
    }

    {
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        subscribers_.emplace(sub->id, sub);
    }

    auto state = std::make_shared<typename EventSubscription<T>::State>();
    state->subscriber = std::move(sub);

    return EventSubscription<T>(std::move(state));
}

template <Event T>
void EventStream<T>::publish(const T& event) {
    publish_impl(event);
}

template <Event T>
void EventStream<T>::publish(T&& event) {
    publish_impl(event);
}

template <Event T>
void EventStream<T>::publish_impl(const T& event) {
    // Snapshot subscribers to avoid holding lock while publishing
    std::vector<std::shared_ptr<SubscriberState>> snapshot;
    {
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        snapshot.reserve(subscribers_.size());
        for (const auto& [id, sub] : subscribers_) {
            snapshot.push_back(sub);
        }
    }

    // Publish to each subscriber
    for (auto& sub : snapshot) {
        std::unique_lock<std::mutex> lock(sub->mutex);

        if (sub->closed) {
            continue;
        }

        const auto max_size = sub->options.max_queue_size;

        switch (sub->options.policy) {
            case BackpressurePolicy::Block: {
                // Wait until queue has space
                sub->cv.wait(lock, [&] { return sub->closed || sub->queue.size() < max_size; });

                if (sub->closed) {
                    continue;
                }

                sub->queue.push_back(event);
                break;
            }

            case BackpressurePolicy::DropOldest: {
                // Remove oldest if queue is full
                if (sub->queue.size() >= max_size && !sub->queue.empty()) {
                    sub->queue.pop_front();
                }
                sub->queue.push_back(event);
                break;
            }

            case BackpressurePolicy::DropNewest: {
                // Drop new event if queue is full
                if (sub->queue.size() < max_size) {
                    sub->queue.push_back(event);
                }
                break;
            }
        }

        lock.unlock();
        sub->cv.notify_all();
    }
}

template <Event T>
void EventStream<T>::unsubscribe(std::size_t id) {
    std::shared_ptr<SubscriberState> removed;

    {
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        auto it = subscribers_.find(id);
        if (it != subscribers_.end()) {
            removed = it->second;
            subscribers_.erase(it);
        }
    }

    if (removed) {
        std::lock_guard<std::mutex> lock(removed->mutex);
        removed->closed = true;
        removed->queue.clear();
        removed->cv.notify_all();
    }
}

template <Event T>
std::size_t EventStream<T>::subscriber_count() const {
    std::lock_guard<std::mutex> lock(subscribers_mutex_);
    return subscribers_.size();
}

// ========================================
// IEventBusStream Implementation
// ========================================

template <Event T>
std::shared_ptr<EventStream<T>> IEventBusStream::get_stream() {
    auto type = std::type_index(typeid(T));
    auto stream = get_stream_impl(type);

    if (!stream) {
        stream = create_stream_impl(type);
    }

    return std::static_pointer_cast<EventStream<T>>(stream);
}

}  // namespace Corona::Kernel
