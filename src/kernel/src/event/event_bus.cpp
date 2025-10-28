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
        subscriptions_[type].push_back({id, std::move(handler)});
        return id;
    }

    void publish_impl(std::type_index type, const void* event_ptr) override {
        std::vector<TypeErasedHandler> handlers_copy;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = subscriptions_.find(type);
            if (it != subscriptions_.end()) {
                for (const auto& sub : it->second) {
                    handlers_copy.push_back(sub.handler);
                }
            }
        }

        // Call handlers without holding the lock to avoid deadlocks
        for (const auto& handler : handlers_copy) {
            handler(event_ptr);
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
