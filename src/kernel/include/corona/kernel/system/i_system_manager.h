#pragma once
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "i_system.h"

namespace Corona::Kernel {

// 系统统计信息
struct SystemStats {
    std::string_view name;             // 系统名称
    SystemState state;                 // 系统状态
    int target_fps;                    // 目标帧率
    float actual_fps;                  // 实际帧率
    float average_frame_time_ms;       // 平均帧时间（毫秒）
    float max_frame_time_ms;           // 最大帧时间（毫秒）
    std::uint64_t total_frames;        // 总帧数
    std::uint64_t total_update_calls;  // 总更新次数
};

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

    // 获取系统统计信息
    virtual SystemStats get_system_stats(std::string_view name) = 0;

    // 获取所有系统的统计信息
    virtual std::vector<SystemStats> get_all_stats() = 0;
};

// Factory function
std::unique_ptr<ISystemManager> create_system_manager();

}  // namespace Corona::Kernel
