#pragma once
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

class ILogger {
   public:
    virtual ~ILogger() = default;

    virtual void log(LogLevel level, std::string_view message,
                     const std::source_location& location) = 0;

    // Convenience methods
    void trace(std::string_view message, const std::source_location& location = std::source_location::current()) {
        log(LogLevel::trace, message, location);
    }

    void debug(std::string_view message, const std::source_location& location = std::source_location::current()) {
        log(LogLevel::debug, message, location);
    }

    void info(std::string_view message, const std::source_location& location = std::source_location::current()) {
        log(LogLevel::info, message, location);
    }

    void warning(std::string_view message, const std::source_location& location = std::source_location::current()) {
        log(LogLevel::warning, message, location);
    }

    void error(std::string_view message, const std::source_location& location = std::source_location::current()) {
        log(LogLevel::error, message, location);
    }

    void fatal(std::string_view message, const std::source_location& location = std::source_location::current()) {
        log(LogLevel::fatal, message, location);
    }

    virtual void set_level(LogLevel level) = 0;
    virtual LogLevel get_level() const = 0;
};

}  // namespace Corona::Kernel
