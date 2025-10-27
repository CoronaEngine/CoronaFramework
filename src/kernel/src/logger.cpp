#include <fast_io.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <vector>

#include "corona/kernel/i_logger.h"

namespace Corona::Kernel {

// Console Sink using fast_io
class ConsoleSink : public ISink {
   public:
    ConsoleSink() : min_level_(LogLevel::info) {}

    void log(const LogMessage& msg) override {
        if (msg.level < min_level_) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        // Format timestamp
        auto time_t_now = std::chrono::system_clock::to_time_t(msg.timestamp);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(msg.timestamp.time_since_epoch()) % 1000;

        std::tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &time_t_now);
#else
        localtime_r(&time_t_now, &tm_buf);
#endif

        // Use fast_io to format and print - simple approach
        fast_io::io::print("[",
                           tm_buf.tm_year + 1900, "-",
                           tm_buf.tm_mon + 1, "-",
                           tm_buf.tm_mday, " ",
                           tm_buf.tm_hour, ":",
                           tm_buf.tm_min, ":",
                           tm_buf.tm_sec, ".",
                           ms.count(),
                           "] [",
                           level_to_string(msg.level),
                           "] [",
                           fast_io::mnp::os_c_str(msg.location.file_name()), ":",
                           msg.location.line(), ":",
                           msg.location.column(),
                           "] ",
                           fast_io::mnp::os_c_str(msg.message.data()),
                           "\n");
    }

    void flush() override {
        // Flush stdout
        fast_io::flush(fast_io::out());
    }

    void set_level(LogLevel level) override {
        std::lock_guard<std::mutex> lock(mutex_);
        min_level_ = level;
    }

    LogLevel get_level() const override {
        return min_level_;
    }

   private:
    static std::string_view level_to_string(LogLevel level) {
        switch (level) {
            case LogLevel::trace:
                return "TRACE";
            case LogLevel::debug:
                return "DEBUG";
            case LogLevel::info:
                return "INFO ";
            case LogLevel::warning:
                return "WARN ";
            case LogLevel::error:
                return "ERROR";
            case LogLevel::fatal:
                return "FATAL";
            default:
                return "UNKNOWN";
        }
    }

    LogLevel min_level_;
    std::mutex mutex_;
};

// File Sink using fast_io
class FileSink : public ISink {
   public:
    explicit FileSink(std::string_view filename)
        : min_level_(LogLevel::trace),
          file_(filename, fast_io::open_mode::out | fast_io::open_mode::app) {}

    void log(const LogMessage& msg) override {
        if (msg.level < min_level_) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        // Format timestamp
        auto time_t_now = std::chrono::system_clock::to_time_t(msg.timestamp);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(msg.timestamp.time_since_epoch()) % 1000;

        std::tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &time_t_now);
#else
        localtime_r(&time_t_now, &tm_buf);
#endif

        // Write to file using fast_io
        fast_io::io::print(file_,
                           "[",
                           tm_buf.tm_year + 1900, "-",
                           tm_buf.tm_mon + 1, "-",
                           tm_buf.tm_mday, " ",
                           tm_buf.tm_hour, ":",
                           tm_buf.tm_min, ":",
                           tm_buf.tm_sec, ".",
                           ms.count(),
                           "] [",
                           level_to_string(msg.level),
                           "] [",
                           fast_io::mnp::os_c_str(msg.location.file_name()), ":",
                           msg.location.line(), ":",
                           msg.location.column(),
                           "] ",
                           fast_io::mnp::os_c_str(msg.message.data()),
                           "\n");
    }

    void flush() override {
        std::lock_guard<std::mutex> lock(mutex_);
        fast_io::flush(file_);
    }

    void set_level(LogLevel level) override {
        std::lock_guard<std::mutex> lock(mutex_);
        min_level_ = level;
    }

    LogLevel get_level() const override {
        return min_level_;
    }

   private:
    static std::string_view level_to_string(LogLevel level) {
        switch (level) {
            case LogLevel::trace:
                return "TRACE";
            case LogLevel::debug:
                return "DEBUG";
            case LogLevel::info:
                return "INFO ";
            case LogLevel::warning:
                return "WARN ";
            case LogLevel::error:
                return "ERROR";
            case LogLevel::fatal:
                return "FATAL";
            default:
                return "UNKNOWN";
        }
    }

    LogLevel min_level_;
    fast_io::native_file file_;
    std::mutex mutex_;
};

// Logger implementation with multiple sinks
class Logger : public ILogger {
   public:
    Logger() {}

    void log(LogLevel level, std::string_view message, const std::source_location& location) override {
        LogMessage msg{
            level,
            message,
            location,
            std::chrono::system_clock::now()};

        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& sink : sinks_) {
            sink->log(msg);
        }
    }

    void add_sink(std::shared_ptr<ISink> sink) override {
        std::lock_guard<std::mutex> lock(mutex_);
        sinks_.push_back(std::move(sink));
    }

    void remove_all_sinks() override {
        std::lock_guard<std::mutex> lock(mutex_);
        sinks_.clear();
    }

    void set_level(LogLevel level) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& sink : sinks_) {
            sink->set_level(level);
        }
    }

    LogLevel get_level() const override {
        return LogLevel::info;  // Return a default value
    }

   private:
    std::vector<std::shared_ptr<ISink>> sinks_;
    std::mutex mutex_;
};

// Factory functions
std::unique_ptr<ILogger> create_logger() {
    auto logger = std::make_unique<Logger>();
    // Add default console sink
    logger->add_sink(std::make_shared<ConsoleSink>());
    return logger;
}

std::shared_ptr<ISink> create_console_sink() {
    return std::make_shared<ConsoleSink>();
}

std::shared_ptr<ISink> create_file_sink(std::string_view filename) {
    return std::make_shared<FileSink>(filename);
}

}  // namespace Corona::Kernel
