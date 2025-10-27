#pragma once
#include <memory>
#include <source_location>
#include <string_view>

#include "i_sink.h"

namespace Corona::Kernel {

class ILogger {
   public:
    virtual ~ILogger() = default;

    virtual void log(LogLevel level, std::string_view message,
                     const std::source_location& location) = 0;

    // Sink management
    virtual void add_sink(std::shared_ptr<ISink> sink) = 0;
    virtual void remove_all_sinks() = 0;

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

// Factory functions
std::unique_ptr<ILogger> create_logger();
std::shared_ptr<ISink> create_console_sink();
std::shared_ptr<ISink> create_file_sink(std::string_view filename);

}  // namespace Corona::Kernel
