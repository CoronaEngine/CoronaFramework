#include <chrono>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <sstream>

#include "corona/kernel/core/i_logger.h"
#include "quill/Backend.h"
#include "quill/Frontend.h"
#include "quill/LogMacros.h"
#include "quill/core/PatternFormatterOptions.h"
#include "quill/sinks/ConsoleSink.h"
#include "quill/sinks/FileSink.h"

namespace Corona::Kernel {

namespace {
// 单例控制
static std::once_flag init_flag;
static quill::Logger* g_logger = nullptr;

// Corona LogLevel 映射到 Quill LogLevel
quill::LogLevel to_quill_level(LogLevel level) {
    switch (level) {
        case LogLevel::trace:
            return quill::LogLevel::TraceL3;
        case LogLevel::debug:
            return quill::LogLevel::Debug;
        case LogLevel::info:
            return quill::LogLevel::Info;
        case LogLevel::warning:
            return quill::LogLevel::Warning;
        case LogLevel::error:
            return quill::LogLevel::Error;
        case LogLevel::fatal:
            return quill::LogLevel::Critical;
        default:
            return quill::LogLevel::Info;
    }
}

// 生成日志文件名: YYYY-MM-DD_HH-MM-SS_corona.log
std::string generate_log_filename() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);

    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t_now);
#else
    localtime_r(&time_t_now, &tm_buf);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d_%H-%M-%S")
        << "_corona.log";

    return oss.str();
}

void initialize_impl() {
    // 启动 Quill Backend
    quill::Backend::start();

    // 配置日志格式: [时间戳][线程ID][级别][文件:行号] 消息
    quill::PatternFormatterOptions formatter_options;
    formatter_options.format_pattern = "[%(time)][%(thread_id)][%(log_level)][%(file_name):%(line_number)] %(message)";
    formatter_options.timestamp_pattern = "%Y-%m-%d %H:%M:%S.%Qms";
    formatter_options.timestamp_timezone = quill::Timezone::LocalTime;

    // 创建控制台 Sink
    auto console_sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>("corona_console");

    // 创建文件 Sink (带时间戳的文件名)
    std::string log_filename = generate_log_filename();
    auto file_sink = quill::Frontend::create_or_get_sink<quill::FileSink>(
        log_filename,
        []() {
            quill::FileSinkConfig cfg;
            cfg.set_open_mode('w');  // 每次运行创建新文件
            return cfg;
        }(),
        quill::FileEventNotifier{});

    // 创建 Logger，同时添加控制台和文件两个 sinks
    std::vector<std::shared_ptr<quill::Sink>> sinks;
    sinks.push_back(console_sink);
    sinks.push_back(file_sink);

    g_logger = quill::Frontend::create_or_get_logger(
        "corona_default",
        std::move(sinks),
        formatter_options);

    // 默认日志级别为 Info
    g_logger->set_log_level(quill::LogLevel::Info);
}

}  // namespace

// ========================================
// CoronaLogger 实现
// ========================================

void CoronaLogger::initialize() {
    std::call_once(init_flag, initialize_impl);
}

void CoronaLogger::set_log_level(LogLevel level) {
    initialize();
    if (g_logger) {
        g_logger->set_log_level(to_quill_level(level));
    }
}

void CoronaLogger::flush() {
    if (g_logger) {
        g_logger->flush_log();
    }
}

quill::Logger* CoronaLogger::get_logger() {
    initialize();
    return g_logger;
}

}  // namespace Corona::Kernel
