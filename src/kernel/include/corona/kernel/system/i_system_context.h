#pragma once
#include <cstdint>
#include <string_view>

namespace Corona::Kernel {

// 前向声明
class ILogger;
class IEventBus;
class IEventBusStream;
class IVirtualFileSystem;
class IPluginManager;
class ISystem;

// 系统上下文（提供对全局数据和服务的访问）
class ISystemContext {
   public:
    virtual ~ISystemContext() = default;

    // 访问内核服务
    virtual ILogger* logger() = 0;
    virtual IEventBus* event_bus() = 0;
    virtual IEventBusStream* event_stream() = 0;
    virtual IVirtualFileSystem* vfs() = 0;
    virtual IPluginManager* plugin_manager() = 0;

    // 访问其他系统（需谨慎使用，优先通过事件通信）
    virtual ISystem* get_system(std::string_view name) = 0;

    // 获取主线程帧时间信息
    virtual float get_delta_time() const = 0;
    virtual uint64_t get_frame_number() const = 0;
};

}  // namespace Corona::Kernel
