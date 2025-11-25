#include <oneapi/tbb/concurrent_queue.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
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
        if (msg.level < min_level_.load(std::memory_order_acquire)) {
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
        min_level_.store(level, std::memory_order_release);
    }

    LogLevel get_level() const override {
        return min_level_.load(std::memory_order_acquire);
    }

   private:
    std::atomic<LogLevel> min_level_;
    std::mutex mutex_;  // 仅保护 std::cout 输出
};

// 异步文件 Sink - 使用后台线程处理文件写入
// 使用 TBB concurrent_queue 实现无锁入队，提升高并发性能
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
        running_.store(false, std::memory_order_release);
        cv_.notify_one();

        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }

        // 确保所有日志都已写入
        file_.flush();
        file_.close();
    }

    void log(const LogMessage& msg) override {
        if (msg.level < min_level_.load(std::memory_order_acquire)) {
            return;
        }

        // 使用无锁队列入队（线程安全，无需加锁）
        message_queue_.push(msg);

        // 通知后台线程有新消息
        cv_.notify_one();
    }

    void flush() override {
        // 等待队列清空
        std::unique_lock<std::mutex> lock(cv_mutex_);
        cv_.wait(lock, [this] { return message_queue_.empty() || !running_.load(std::memory_order_acquire); });

        // 刷新文件
        if (file_.is_open()) {
            file_.flush();
        }
    }

    void set_level(LogLevel level) override {
        min_level_.store(level, std::memory_order_release);
    }

    LogLevel get_level() const override {
        return min_level_.load(std::memory_order_acquire);
    }

   private:
    void process_queue() {
        while (running_.load(std::memory_order_acquire)) {
            LogMessage msg;

            // 尝试从无锁队列中取出消息
            if (message_queue_.try_pop(msg)) {
                write_to_file(msg);

                // 如果队列为空，通知 flush 等待者
                if (message_queue_.empty()) {
                    cv_.notify_all();
                }
            } else {
                // 队列为空，等待新消息
                std::unique_lock<std::mutex> lock(cv_mutex_);
                cv_.wait_for(lock, std::chrono::milliseconds(10), [this] {
                    return !message_queue_.empty() || !running_.load(std::memory_order_acquire);
                });
            }
        }

        // 线程退出前处理剩余消息
        LogMessage msg;
        while (message_queue_.try_pop(msg)) {
            write_to_file(msg);
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

    std::atomic<LogLevel> min_level_;
    std::ofstream file_;

    tbb::concurrent_queue<LogMessage> message_queue_;  // TBB 无锁队列
    std::mutex cv_mutex_;                              // 仅用于条件变量等待
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
            std::string(message),  // 转换为 std::string，确保异步处理时数据安全
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

// ========================================
// CoronaLogger 静态实现
// ========================================

// 使用 Meyers 单例模式，线程安全的懒加载初始化
static std::unique_ptr<ILogger>& get_default_logger_instance() {
    static std::unique_ptr<ILogger> instance = create_logger();
    return instance;
}

ILogger* CoronaLogger::get_default() noexcept {
    return get_default_logger_instance().get();
}

std::shared_ptr<ISink> create_console_sink() {
    return std::make_shared<ConsoleSink>();
}

std::shared_ptr<ISink> create_file_sink(std::string_view filename) {
    return std::make_shared<AsyncFileSink>(filename);
}

}  // namespace Corona::Kernel
