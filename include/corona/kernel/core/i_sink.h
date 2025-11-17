#pragma once
#include <chrono>
#include <source_location>
#include <string_view>

namespace Corona::Kernel {

/**
 * @brief 日志级别枚举
 *
 * 从低到高的日志级别，用于过滤日志输出
 */
enum class LogLevel {
    trace,    ///< 跟踪级别，最详细的调试信息
    debug,    ///< 调试级别，用于开发调试
    info,     ///< 信息级别，常规运行信息
    warning,  ///< 警告级别，潜在问题
    error,    ///< 错误级别，错误但不致命
    fatal     ///< 致命级别，严重错误导致程序无法继续
};

/**
 * @brief 日志消息结构体
 *
 * 包含一条日志的完整信息，包括级别、内容、源位置和时间戳
 */
struct LogMessage {
    LogLevel level;                                   ///< 日志级别
    std::string_view message;                         ///< 日志内容
    std::source_location location;                    ///< 源代码位置（文件名、行号、函数名）
    std::chrono::system_clock::time_point timestamp;  ///< 时间戳
};

/**
 * @brief 日志输出目标接口
 *
 * ISink 定义日志输出的目标，如控制台、文件等。
 * 每个 Sink 可以独立设置最低日志级别，实现分级输出。
 *
 * 实现类需要考虑线程安全，因为日志可能来自多个线程
 */
class ISink {
   public:
    virtual ~ISink() = default;

    /**
     * @brief 输出一条日志消息
     * @param msg 日志消息，包含级别、内容、位置和时间戳
     *
     * 实现时应检查消息级别是否满足当前 Sink 的最低级别要求
     */
    virtual void log(const LogMessage& msg) = 0;

    /**
     * @brief 刷新缓冲区
     *
     * 对于异步或带缓冲的 Sink，确保所有待输出的日志立即写入
     */
    virtual void flush() = 0;

    /**
     * @brief 设置最低日志级别
     * @param level 新的最低日志级别
     *
     * 低于此级别的日志将被忽略
     */
    virtual void set_level(LogLevel level) = 0;

    /**
     * @brief 获取当前最低日志级别
     * @return 当前设置的最低日志级别
     */
    virtual LogLevel get_level() const = 0;
};

}  // namespace Corona::Kernel
