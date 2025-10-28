#pragma once
#include <chrono>
#include <source_location>
#include <string_view>

namespace Corona::Kernel {

enum class LogLevel {
    trace,
    debug,
    info,
    warning,
    error,
    fatal
};

struct LogMessage {
    LogLevel level;
    std::string_view message;
    std::source_location location;
    std::chrono::system_clock::time_point timestamp;
};

class ISink {
   public:
    virtual ~ISink() = default;
    virtual void log(const LogMessage& msg) = 0;
    virtual void flush() = 0;
    virtual void set_level(LogLevel level) = 0;
    virtual LogLevel get_level() const = 0;
};

}  // namespace Corona::Kernel
