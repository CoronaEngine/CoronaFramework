#include "corona/kernel/event/i_event_stream.h"
#include <cassert>

namespace Corona::Kernel {

// EventBusStream implementation
class EventBusStream : public IEventBusStream {
   public:
    EventBusStream() = default;
    ~EventBusStream() override = default;

    EventBusStream(const EventBusStream&) = delete;
    EventBusStream& operator=(const EventBusStream&) = delete;

   protected:
    std::shared_ptr<void> get_stream_impl(std::type_index type) override {
        std::lock_guard<std::mutex> lock(streams_mutex_);
        auto it = streams_.find(type);
        return it != streams_.end() ? it->second : nullptr;
    }

    std::shared_ptr<void> create_stream_impl(std::type_index type) override {
        std::lock_guard<std::mutex> lock(streams_mutex_);

        auto it = streams_.find(type);
        if (it != streams_.end()) {
            return it->second;
        }

        // This is a bit hacky, but we can't instantiate the template without the type
        // In practice, this should never be called directly - users should call get_stream<T>()
        assert(false && "create_stream_impl should not be called directly");
        return nullptr;
    }

   private:
    template <Event T>
    friend class EventStreamFactory;

    void register_stream_impl(std::type_index type, std::shared_ptr<void> stream) {
        std::lock_guard<std::mutex> lock(streams_mutex_);
        streams_[type] = std::move(stream);
    }

    std::mutex streams_mutex_;
    std::unordered_map<std::type_index, std::shared_ptr<void>> streams_;
};

// Helper to create event streams with proper type information
template <Event T>
class EventStreamFactory {
   public:
    static std::shared_ptr<EventStream<T>> create_or_get(EventBusStream& bus) {
        auto type = std::type_index(typeid(T));
        auto existing = bus.get_stream_impl(type);

        if (existing) {
            return std::static_pointer_cast<EventStream<T>>(existing);
        }

        auto stream = std::make_shared<EventStream<T>>();
        bus.register_stream_impl(type, stream);
        return stream;
    }
};

}  // namespace Corona::Kernel
