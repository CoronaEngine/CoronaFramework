#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "corona/kernel/core/i_logger.h"

namespace Corona::Kernel {

namespace {
// Helper function to convert log level to string (shared across all sinks)
constexpr std::string_view level_to_string(LogLevel level) {
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

// Helper function to format timestamp (shared across all sinks)
void format_timestamp(const std::chrono::system_clock::time_point& timestamp, char* buffer, size_t buffer_size) {
    auto time_t_now = std::chrono::system_clock::to_time_t(timestamp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()) % 1000;

    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t_now);
#else
    localtime_r(&time_t_now, &tm_buf);
#endif

    std::snprintf(buffer, buffer_size,
                  "[%04d-%02d-%02d %02d:%02d:%02d.%03lld]",
                  tm_buf.tm_year + 1900,
                  tm_buf.tm_mon + 1,
                  tm_buf.tm_mday,
                  tm_buf.tm_hour,
                  tm_buf.tm_min,
                  tm_buf.tm_sec,
                  static_cast<long long>(ms.count()));
}
}  // namespace

// Console Sink using std::cout (同步实现，因为控制台输出通常很快)
class ConsoleSink : public ISink {
   public:
    ConsoleSink() : min_level_(LogLevel::info) {}

    void log(const LogMessage& msg) override {
        if (msg.level < min_level_) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        // Format timestamp
        char time_buffer[32];
        format_timestamp(msg.timestamp, time_buffer, sizeof(time_buffer));

        // Use std::cout to print
        std::cout << time_buffer
                  << " [" << level_to_string(msg.level) << "]"
                  << " [" << msg.location.file_name()
                  << ":" << msg.location.line()
                  << ":" << msg.location.column() << "]"
                  << " " << msg.message
                  << std::endl;
    }

    void flush() override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout.flush();
    }

    void set_level(LogLevel level) override {
        std::lock_guard<std::mutex> lock(mutex_);
        min_level_ = level;
    }

    LogLevel get_level() const override {
        return min_level_;
    }

   private:
    LogLevel min_level_;
    std::mutex mutex_;
};

// 异步文件 Sink - 使用后台线程处理文件写入
class AsyncFileSink : public ISink {
   public:
    explicit AsyncFileSink(std::string_view filename)
        : min_level_(LogLevel::trace),
          file_(std::string(filename), std::ios::out | std::ios::app),
          running_(true) {
        // 启动后台写入线程
        worker_thread_ = std::thread(&AsyncFileSink::process_queue, this);
    }

    ~AsyncFileSink() {
        // 停止后台线程
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            running_ = false;
        }
        cv_.notify_one();

        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }

        // 确保所有日志都已写入
        file_.flush();
        file_.close();
    }

    void log(const LogMessage& msg) override {
        if (msg.level < min_level_) {
            return;
        }

        // 将日志消息放入队列（不阻塞）
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            message_queue_.push(msg);
        }
        cv_.notify_one();
    }

    void flush() override {
        // 等待队列清空
        std::unique_lock<std::mutex> lock(queue_mutex_);
        cv_.wait(lock, [this] { return message_queue_.empty() || !running_; });

        // 刷新文件
        if (file_.is_open()) {
            file_.flush();
        }
    }

    void set_level(LogLevel level) override {
        std::lock_guard<std::mutex> lock(level_mutex_);
        min_level_ = level;
    }

    LogLevel get_level() const override {
        std::lock_guard<std::mutex> lock(level_mutex_);
        return min_level_;
    }

   private:
    void process_queue() {
        while (running_) {
            std::unique_lock<std::mutex> lock(queue_mutex_);

            // 等待新消息或停止信号
            cv_.wait(lock, [this] { return !message_queue_.empty() || !running_; });

            // 批量处理消息以提高性能
            while (!message_queue_.empty()) {
                LogMessage msg = message_queue_.front();
                message_queue_.pop();

                // 解锁后写入，避免阻塞新日志入队
                lock.unlock();
                write_to_file(msg);
                lock.lock();
            }

            // 通知 flush 等待者
            cv_.notify_all();
        }

        // 线程退出前处理剩余消息
        std::lock_guard<std::mutex> lock(queue_mutex_);
        while (!message_queue_.empty()) {
            write_to_file(message_queue_.front());
            message_queue_.pop();
        }
    }

    void write_to_file(const LogMessage& msg) {
        if (!file_.is_open()) {
            return;
        }

        // Format timestamp
        char time_buffer[32];
        format_timestamp(msg.timestamp, time_buffer, sizeof(time_buffer));

        // Write to file
        file_ << time_buffer
              << " [" << level_to_string(msg.level) << "]"
              << " [" << msg.location.file_name()
              << ":" << msg.location.line()
              << ":" << msg.location.column() << "]"
              << " " << msg.message
              << std::endl;
    }

    LogLevel min_level_;
    std::ofstream file_;

    std::queue<LogMessage> message_queue_;
    std::mutex queue_mutex_;
    mutable std::mutex level_mutex_;
    std::condition_variable cv_;

    std::atomic<bool> running_;
    std::thread worker_thread_;
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
    return std::make_shared<AsyncFileSink>(filename);
}

}  // namespace Corona::Kernel
