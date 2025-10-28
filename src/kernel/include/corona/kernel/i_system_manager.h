#pragma once
#include <memory>
#include <string_view>
#include <vector>

#include "i_system.h"

namespace Corona::Kernel {

// 系统管理器接口
class ISystemManager {
   public:
    virtual ~ISystemManager() = default;

    // 注册系统
    virtual void register_system(std::shared_ptr<ISystem> system) = 0;

    // 获取系统
    virtual std::shared_ptr<ISystem> get_system(std::string_view name) = 0;

    // 初始化所有系统（按优先级）
    virtual bool initialize_all() = 0;

    // 启动所有系统线程
    virtual void start_all() = 0;

    // 暂停所有系统
    virtual void pause_all() = 0;

    // 恢复所有系统
    virtual void resume_all() = 0;

    // 停止所有系统
    virtual void stop_all() = 0;

    // 关闭所有系统
    virtual void shutdown_all() = 0;

    // 同步点：等待所有系统完成当前帧
    virtual void sync_all() = 0;

    // 获取所有系统
    virtual std::vector<std::shared_ptr<ISystem>> get_all_systems() = 0;
};

// Factory function
std::unique_ptr<ISystemManager> create_system_manager();

}  // namespace Corona::Kernel
