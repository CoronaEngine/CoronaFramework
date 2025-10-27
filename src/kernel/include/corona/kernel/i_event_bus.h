#pragma once
#include <any>
#include <functional>
#include <typeindex>

namespace Corona::Kernel {

using EventHandler = std::function<void(const std::any&)>;
using EventId = std::size_t;

class IEventBus {
   public:
    virtual ~IEventBus() = default;

    // Subscribe to an event by type
    virtual EventId subscribe(std::type_index type, EventHandler handler) = 0;

    // Unsubscribe from an event
    virtual void unsubscribe(EventId id) = 0;

    // Publish an event
    virtual void publish(std::type_index type, const std::any& event) = 0;

    // Template convenience methods
    template <typename T>
    EventId subscribe(std::function<void(const T&)> handler) {
        return subscribe(std::type_index(typeid(T)), [handler](const std::any& event) {
            if (event.type() == typeid(T)) {
                handler(std::any_cast<const T&>(event));
            }
        });
    }

    template <typename T>
    void publish(const T& event) {
        publish(std::type_index(typeid(T)), event);
    }
};

}  // namespace Corona::Kernel
