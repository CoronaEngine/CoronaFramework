#pragma once
#include <cstdint>
#include <string_view>

namespace Corona::Kernel {

enum class SystemState {
    idle,      // 未启动
    running,   // 正在运行
    paused,    // 已暂停
    stopping,  // 正在停止
    stopped    // 已停止
};

class ISystemContext;

// 系统接口
class ISystem {
   public:
    virtual ~ISystem() = default;

    // 获取系统名称
    virtual std::string_view get_name() const = 0;

    // 获取系统优先级（用于初始化顺序，越大越优先）
    virtual int get_priority() const = 0;

    // 系统初始化（在主线程调用，传入系统上下文）
    virtual bool initialize(ISystemContext* context) = 0;

    // 系统关闭（在主线程调用）
    virtual void shutdown() = 0;

    // 系统更新（在独立线程循环调用，无参数）
    virtual void update() = 0;

    // 帧同步点（可选，用于需要与主线程同步的系统）
    virtual void sync() {}

    // 获取系统目标帧率（0 表示不限制）
    virtual int get_target_fps() const = 0;

    // 获取当前状态
    virtual SystemState get_state() const = 0;

    // 线程控制
    virtual void start() = 0;    // 启动系统线程
    virtual void pause() = 0;    // 暂停系统
    virtual void resume() = 0;   // 恢复系统
    virtual void stop() = 0;     // 停止系统线程

    // 性能统计
    virtual float get_actual_fps() const = 0;           // 获取实际 FPS
    virtual float get_average_frame_time() const = 0;   // 获取平均帧时间（毫秒）
    virtual float get_max_frame_time() const = 0;       // 获取最大帧时间（毫秒）
    virtual std::uint64_t get_total_frames() const = 0; // 获取总帧数
    virtual void reset_stats() = 0;                     // 重置统计信息
};

}  // namespace Corona::Kernel
