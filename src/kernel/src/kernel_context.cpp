#include "corona/kernel/kernel_context.h"

#include <memory>

namespace Corona::Kernel {

// Forward declare factory functions
std::unique_ptr<ILogger> create_logger();
std::unique_ptr<IEventBus> create_event_bus();
std::unique_ptr<IVirtualFileSystem> create_vfs();
std::unique_ptr<IPluginManager> create_plugin_manager();

KernelContext& KernelContext::instance() {
    static KernelContext instance;
    return instance;
}

bool KernelContext::initialize() {
    if (initialized_) {
        return true;
    }

    // Initialize services in order
    logger_ = create_logger();
    if (!logger_) {
        return false;
    }

    event_bus_ = create_event_bus();
    if (!event_bus_) {
        return false;
    }

    vfs_ = create_vfs();
    if (!vfs_) {
        return false;
    }

    plugin_manager_ = create_plugin_manager();
    if (!plugin_manager_) {
        return false;
    }

    initialized_ = true;
    logger_->info("Kernel initialized successfully");

    return true;
}

void KernelContext::shutdown() {
    if (!initialized_) {
        return;
    }

    if (logger_) {
        logger_->info("Shutting down kernel...");
    }

    // Shutdown services in reverse order
    plugin_manager_.reset();
    vfs_.reset();
    event_bus_.reset();
    logger_.reset();

    initialized_ = false;
}

}  // namespace Corona::Kernel
