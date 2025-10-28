#pragma once
#include <concepts>
#include <functional>
#include <typeindex>

namespace Corona::Kernel {

using EventId = std::size_t;

// Event concept: must be copyable and movable
template <typename T>
concept Event = std::is_copy_constructible_v<T> && std::is_move_constructible_v<T>;

// EventHandler concept: callable with const T&
template <typename Handler, typename T>
concept EventHandler = Event<T> && std::invocable<Handler, const T&>;

class IEventBus {
   public:
    virtual ~IEventBus() = default;

    // Subscribe to an event type with compile-time type safety
    // Returns an EventId that can be used to unsubscribe
    template <Event T, EventHandler<T> Handler>
    EventId subscribe(Handler&& handler) {
        return subscribe_impl(std::type_index(typeid(T)),
                              [handler = std::forward<Handler>(handler)](const void* event_ptr) {
                                  handler(*static_cast<const T*>(event_ptr));
                              });
    }

    // Unsubscribe from an event using the EventId
    virtual void unsubscribe(EventId id) = 0;

    // Publish an event with compile-time type safety
    template <Event T>
    void publish(const T& event) {
        publish_impl(std::type_index(typeid(T)), &event);
    }

   protected:
    // Internal type-erased implementation
    using TypeErasedHandler = std::function<void(const void*)>;
    virtual EventId subscribe_impl(std::type_index type, TypeErasedHandler handler) = 0;
    virtual void publish_impl(std::type_index type, const void* event_ptr) = 0;
};

}  // namespace Corona::Kernel
