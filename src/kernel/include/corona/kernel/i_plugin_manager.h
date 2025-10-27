#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace Corona::Kernel {

struct PluginInfo {
    std::string name;
    std::string version;
    std::string description;
    std::vector<std::string> dependencies;
};

class IPlugin {
   public:
    virtual ~IPlugin() = default;
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual PluginInfo get_info() const = 0;
};

class IPluginManager {
   public:
    virtual ~IPluginManager() = default;

    // Load a plugin from a dynamic library
    virtual bool load_plugin(std::string_view path) = 0;

    // Unload a plugin by name
    virtual void unload_plugin(std::string_view name) = 0;

    // Get a plugin by name
    virtual IPlugin* get_plugin(std::string_view name) = 0;

    // Get all loaded plugins
    virtual std::vector<std::string> get_loaded_plugins() const = 0;

    // Initialize all loaded plugins
    virtual bool initialize_all() = 0;

    // Shutdown all plugins
    virtual void shutdown_all() = 0;
};

}  // namespace Corona::Kernel
