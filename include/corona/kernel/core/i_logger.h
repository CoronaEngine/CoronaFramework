#pragma once
#include <memory>
#include <source_location>
#include <string_view>

#include "i_sink.h"

namespace Corona::Kernel {

/**
 * @brief 日志系统接口
 *
 * ILogger 是引擎的核心日志系统，支持多个输出目标（Sink）和不同级别的日志。
 *
 * 特性：
 * - 支持多个输出目标（控制台、文件等）
 * - 自动捕获源代码位置信息（C++20 source_location）
 * - 线程安全的日志输出
 * - 可配置的日志级别过滤
 *
 * 使用示例：
 * @code
 * auto logger = KernelContext::instance().logger();
 * logger->info("程序启动");
 * logger->warning("配置文件未找到，使用默认配置");
 * logger->error("网络连接失败");
 * @endcode
 */
class ILogger {
   public:
    virtual ~ILogger() = default;

    /**
     * @brief 记录一条日志
     * @param level 日志级别
     * @param message 日志内容
     * @param location 源代码位置，默认为调用点
     */
    virtual void log(LogLevel level, std::string_view message,
                     const std::source_location& location) = 0;

    // ========================================
    // Sink 管理
    // ========================================

    /**
     * @brief 添加日志输出目标
     * @param sink 新的输出目标（如控制台 Sink、文件 Sink）
     *
     * 可以添加多个 Sink，每条日志会分发到所有 Sink
     */
    virtual void add_sink(std::shared_ptr<ISink> sink) = 0;

    /**
     * @brief 移除所有日志输出目标
     *
     * 调用后日志将不再输出到任何位置
     */
    virtual void remove_all_sinks() = 0;

    /**
     * @brief 重置日志系统到初始状态
     *
     * 移除所有 Sink 并添加默认的控制台 Sink，用于测试清理或重新初始化
     */
    virtual void reset() = 0;

    // ========================================
    // 便捷方法
    // ========================================

    /**
     * @brief 记录跟踪级别日志
     * @param message 日志内容
     * @param location 源代码位置，自动捕获
     */
    void trace(std::string_view message, const std::source_location& location = std::source_location::current()) {
        log(LogLevel::trace, message, location);
    }

    /**
     * @brief 记录调试级别日志
     * @param message 日志内容
     * @param location 源代码位置，自动捕获
     */
    void debug(std::string_view message, const std::source_location& location = std::source_location::current()) {
        log(LogLevel::debug, message, location);
    }

    /**
     * @brief 记录信息级别日志
     * @param message 日志内容
     * @param location 源代码位置，自动捕获
     */
    void info(std::string_view message, const std::source_location& location = std::source_location::current()) {
        log(LogLevel::info, message, location);
    }

    /**
     * @brief 记录警告级别日志
     * @param message 日志内容
     * @param location 源代码位置，自动捕获
     */
    void warning(std::string_view message, const std::source_location& location = std::source_location::current()) {
        log(LogLevel::warning, message, location);
    }

    /**
     * @brief 记录错误级别日志
     * @param message 日志内容
     * @param location 源代码位置，自动捕获
     */
    void error(std::string_view message, const std::source_location& location = std::source_location::current()) {
        log(LogLevel::error, message, location);
    }

    /**
     * @brief 记录致命级别日志
     * @param message 日志内容
     * @param location 源代码位置，自动捕获
     */
    void fatal(std::string_view message, const std::source_location& location = std::source_location::current()) {
        log(LogLevel::fatal, message, location);
    }

    /**
     * @brief 设置全局最低日志级别
     * @param level 新的最低日志级别
     *
     * 低于此级别的日志不会被处理（在 Logger 层面过滤）
     */
    virtual void set_level(LogLevel level) = 0;

    /**
     * @brief 获取全局最低日志级别
     * @return 当前设置的最低日志级别
     */
    virtual LogLevel get_level() const = 0;
};

// ========================================
// 工厂函数
// ========================================

/**
 * @brief 创建日志系统实例
 * @return 日志系统的唯一指针
 */
std::unique_ptr<ILogger> create_logger();

/**
 * @brief 创建控制台输出 Sink
 * @return 控制台 Sink 的共享指针
 *
 * 输出到标准输出（stdout），同步输出
 */
std::shared_ptr<ISink> create_console_sink();

/**
 * @brief 创建文件输出 Sink
 * @param filename 日志文件路径
 * @return 文件 Sink 的共享指针
 *
 * 输出到文件，异步写入，带缓冲
 */
std::shared_ptr<ISink> create_file_sink(std::string_view filename);

// ========================================
// 静态全局日志器
// ========================================

/**
 * @brief 静态全局日志器封装类
 *
 * CoronaLogger 提供静态方法访问全局默认日志器，简化日志调用流程。
 * 内部拥有独立的日志器实例（懒加载单例），首次调用时自动创建。
 *
 * 特性：
 * - 线程安全：使用 Meyers 单例模式，首次访问时自动初始化
 * - 独立生命周期：日志器由 CoronaLogger 管理，无需外部设置
 * - 默认配置：自动添加控制台输出 Sink
 *
 * 使用示例：
 * @code
 * // 直接使用静态方法记录日志（无需初始化）
 * CoronaLogger::info("程序启动");
 * CoronaLogger::warning("配置文件未找到");
 * CoronaLogger::error("网络连接失败");
 *
 * // 也可以获取日志器进行高级配置
 * auto* logger = CoronaLogger::get_default();
 * logger->add_sink(create_file_sink("app.log"));
 * @endcode
 */
class CoronaLogger {
   public:
    /**
     * @brief 获取默认日志器
     * @return 默认日志器指针，永不为空
     *
     * 首次调用时自动创建日志器实例（线程安全）
     */
    static ILogger* get_default() noexcept;

    // ========================================
    // 便捷静态方法
    // ========================================

    /**
     * @brief 记录跟踪级别日志
     * @param message 日志内容
     * @param location 源代码位置，自动捕获
     */
    static void trace(std::string_view message,
                      const std::source_location& location = std::source_location::current()) {
        get_default()->log(LogLevel::trace, message, location);
    }

    /**
     * @brief 记录调试级别日志
     * @param message 日志内容
     * @param location 源代码位置，自动捕获
     */
    static void debug(std::string_view message,
                      const std::source_location& location = std::source_location::current()) {
        get_default()->log(LogLevel::debug, message, location);
    }

    /**
     * @brief 记录信息级别日志
     * @param message 日志内容
     * @param location 源代码位置，自动捕获
     */
    static void info(std::string_view message,
                     const std::source_location& location = std::source_location::current()) {
        get_default()->log(LogLevel::info, message, location);
    }

    /**
     * @brief 记录警告级别日志
     * @param message 日志内容
     * @param location 源代码位置，自动捕获
     */
    static void warning(std::string_view message,
                        const std::source_location& location = std::source_location::current()) {
        get_default()->log(LogLevel::warning, message, location);
    }

    /**
     * @brief 记录错误级别日志
     * @param message 日志内容
     * @param location 源代码位置，自动捕获
     */
    static void error(std::string_view message,
                      const std::source_location& location = std::source_location::current()) {
        get_default()->log(LogLevel::error, message, location);
    }

    /**
     * @brief 记录致命级别日志
     * @param message 日志内容
     * @param location 源代码位置，自动捕获
     */
    static void fatal(std::string_view message,
                      const std::source_location& location = std::source_location::current()) {
        get_default()->log(LogLevel::fatal, message, location);
    }

   private:
    CoronaLogger() = delete;
};

}  // namespace Corona::Kernel
