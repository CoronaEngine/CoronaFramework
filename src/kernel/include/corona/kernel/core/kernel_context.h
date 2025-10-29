#pragma once
#include <memory>

#include "../event/i_event_bus.h"
#include "../event/i_event_stream.h"
#include "../system/i_system_manager.h"
#include "i_logger.h"
#include "i_plugin_manager.h"
#include "i_vfs.h"

namespace Corona::Kernel {

class KernelContext {
   public:
    static KernelContext& instance();

    // Initialize all kernel services
    bool initialize();

    // Shutdown all kernel services
    void shutdown();

    // Service accessors
    ILogger* logger() const { return logger_.get(); }
    IEventBus* event_bus() const { return event_bus_.get(); }
    IEventBusStream* event_stream() const { return event_stream_.get(); }
    IVirtualFileSystem* vfs() const { return vfs_.get(); }
    IPluginManager* plugin_manager() const { return plugin_manager_.get(); }
    ISystemManager* system_manager() const { return system_manager_.get(); }

   private:
    KernelContext() = default;
    ~KernelContext() = default;

    // Delete copy and move constructors/operators
    KernelContext(const KernelContext&) = delete;
    KernelContext& operator=(const KernelContext&) = delete;
    KernelContext(KernelContext&&) = delete;
    KernelContext& operator=(KernelContext&&) = delete;

    std::unique_ptr<ILogger> logger_;
    std::unique_ptr<IEventBus> event_bus_;
    std::unique_ptr<IEventBusStream> event_stream_;
    std::unique_ptr<IVirtualFileSystem> vfs_;
    std::unique_ptr<IPluginManager> plugin_manager_;
    std::unique_ptr<ISystemManager> system_manager_;

    bool initialized_ = false;
};

}  // namespace Corona::Kernel
