#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "corona/kernel/event/i_event_bus.h"

namespace Corona::Kernel {

class EventBus : public IEventBus {
   public:
    EventBus() : next_id_(0) {}

    void unsubscribe(EventId id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [type, handlers] : subscriptions_) {
            auto it = std::remove_if(handlers.begin(), handlers.end(),
                                     [id](const Subscription& sub) { return sub.id == id; });
            if (it != handlers.end()) {
                handlers.erase(it, handlers.end());
                break;
            }
        }
    }

   protected:
    EventId subscribe_impl(std::type_index type, TypeErasedHandler handler) override {
        std::lock_guard<std::mutex> lock(mutex_);
        EventId id = next_id_++;
        auto& handlers = subscriptions_[type];
        handlers.reserve(handlers.size() + 1);  // Optimize memory allocation
        handlers.push_back({id, std::move(handler)});
        return id;
    }

    void publish_impl(std::type_index type, const void* event_ptr) override {
        std::vector<TypeErasedHandler> handlers_copy;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = subscriptions_.find(type);
            if (it != subscriptions_.end()) {
                handlers_copy.reserve(it->second.size());  // Pre-allocate exact size
                for (const auto& sub : it->second) {
                    handlers_copy.push_back(sub.handler);
                }
            }
        }

        // Call handlers without holding the lock to avoid deadlocks
        // Catch exceptions to prevent one subscriber from crashing the entire system
        for (const auto& handler : handlers_copy) {
            try {
                handler(event_ptr);
            } catch (const std::exception& e) {
                // Log the exception but continue processing other handlers
                // In production, this could log to a proper logging system
                // For now, we silently catch to maintain system stability
                (void)e;  // Suppress unused variable warning
            } catch (...) {
                // Catch all other exceptions to ensure robustness
            }
        }
    }

   private:
    struct Subscription {
        EventId id;
        TypeErasedHandler handler;
    };

    std::map<std::type_index, std::vector<Subscription>> subscriptions_;
    std::mutex mutex_;
    EventId next_id_;
};

// Factory function
std::unique_ptr<IEventBus> create_event_bus() {
    return std::make_unique<EventBus>();
}

}  // namespace Corona::Kernel
