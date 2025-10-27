#include <fast_io.h>

#include <chrono>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>

#include "corona/kernel/i_logger.h"

namespace Corona::Kernel {

class Logger : public ILogger {
   public:
    Logger() : min_level_(LogLevel::info) {}

    void log(LogLevel level, std::string_view message, const std::source_location& location) override {
        if (level < min_level_) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        // Get current time
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        // Format time using stringstream for simplicity
        std::tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &time_t_now);
#else
        localtime_r(&time_t_now, &tm_buf);
#endif

        std::ostringstream time_str;
        time_str << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
                 << '.' << std::setfill('0') << std::setw(3) << ms.count();

        // Format log message as single string
        std::ostringstream log_stream;
        log_stream << "[" << time_str.str() << "] ["
                   << level_to_string(level) << "] ["
                   << location.file_name() << ":"
                   << location.line() << ":"
                   << location.column() << "] "
                   << message << "\n";

        fast_io::io::print(fast_io::mnp::os_c_str(log_stream.str().c_str()));
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
                return "INFO";
            case LogLevel::warning:
                return "WARN";
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

// Factory function
std::unique_ptr<ILogger> create_logger() {
    return std::make_unique<Logger>();
}

}  // namespace Corona::Kernel
