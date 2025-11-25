/**
 * @file filesink_mt_test.cpp
 * @brief AsyncFileSink 多线程稳定性和正确性测试
 *
 * 测试场景：
 * 1. 高并发文件写入
 * 2. 文件内容完整性验证
 * 3. 无锁队列压力测试
 * 4. flush 操作正确性
 * 5. 快速创建/销毁循环
 * 6. 大消息文件写入
 */

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

#include "../test_framework.h"
#include "corona/kernel/core/i_logger.h"

using namespace Corona::Kernel;
using namespace CoronaTest;

namespace fs = std::filesystem;

// ========================================
// 辅助函数
// ========================================

/**
 * @brief 统计文件行数
 */
size_t count_file_lines(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return 0;
    }

    size_t count = 0;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            ++count;
        }
    }
    return count;
}

/**
 * @brief 清理测试文件
 */
void cleanup_test_file(const std::string& filename) {
    if (fs::exists(filename)) {
        fs::remove(filename);
    }
}

// ========================================
// 高并发文件写入测试
// ========================================

TEST(FileSinkMT, HighConcurrencyWrite) {
    const std::string test_file = "test_filesink_highconcurrency.log";
    cleanup_test_file(test_file);

    {
        auto file_sink = create_file_sink(test_file);
        file_sink->set_level(LogLevel::trace);

        constexpr int num_threads = 16;
        constexpr int logs_per_thread = 500;
        std::atomic<int> log_count{0};

        std::vector<std::thread> threads;
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&file_sink, i, &log_count]() {
                for (int j = 0; j < logs_per_thread; ++j) {
                    LogMessage msg{
                        LogLevel::info,
                        "Thread " + std::to_string(i) + " message #" + std::to_string(j),
                        std::source_location::current(),
                        std::chrono::system_clock::now()};
                    file_sink->log(msg);
                    log_count.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        // 等待所有日志写入
        file_sink->flush();

        // 验证计数
        ASSERT_EQ(log_count.load(), num_threads * logs_per_thread);
    }

    // 验证文件内容行数
    size_t line_count = count_file_lines(test_file);
    ASSERT_EQ(line_count, 16 * 500);

    cleanup_test_file(test_file);
}

// ========================================
// 文件内容完整性验证测试
// ========================================

TEST(FileSinkMT, ContentIntegrity) {
    const std::string test_file = "test_filesink_integrity.log";
    cleanup_test_file(test_file);

    constexpr int num_threads = 8;
    constexpr int logs_per_thread = 100;

    {
        auto file_sink = create_file_sink(test_file);
        file_sink->set_level(LogLevel::trace);

        std::vector<std::thread> threads;
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&file_sink, i]() {
                for (int j = 0; j < logs_per_thread; ++j) {
                    // 使用唯一标识符便于验证
                    std::string unique_id = "MARKER_" + std::to_string(i) + "_" + std::to_string(j) + "_END";
                    LogMessage msg{
                        LogLevel::info,
                        unique_id,
                        std::source_location::current(),
                        std::chrono::system_clock::now()};
                    file_sink->log(msg);
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        file_sink->flush();
    }

    // 验证每个标识符都完整出现
    int total_found = 0;
    {
        std::ifstream file(test_file);
        ASSERT_TRUE(file.is_open());

        std::vector<std::vector<bool>> found(num_threads, std::vector<bool>(logs_per_thread, false));
        std::string line;

        while (std::getline(file, line)) {
            for (int i = 0; i < num_threads; ++i) {
                for (int j = 0; j < logs_per_thread; ++j) {
                    std::string marker = "MARKER_" + std::to_string(i) + "_" + std::to_string(j) + "_END";
                    if (line.find(marker) != std::string::npos && !found[i][j]) {
                        found[i][j] = true;
                        ++total_found;
                    }
                }
            }
        }
    }  // 文件在此处自动关闭

    // 验证所有消息都被写入
    ASSERT_EQ(total_found, num_threads * logs_per_thread);

    cleanup_test_file(test_file);
}

// ========================================
// 无锁队列压力测试
// ========================================

TEST(FileSinkMT, LockfreeQueueStress) {
    const std::string test_file = "test_filesink_lockfree.log";
    cleanup_test_file(test_file);

    {
        auto file_sink = create_file_sink(test_file);
        file_sink->set_level(LogLevel::trace);

        std::atomic<bool> stop{false};
        std::atomic<int> total_logs{0};

        auto start_time = std::chrono::steady_clock::now();

        // 32 个线程持续写入
        std::vector<std::thread> threads;
        for (int i = 0; i < 32; ++i) {
            threads.emplace_back([&file_sink, &stop, &total_logs, i]() {
                int counter = 0;
                while (!stop.load(std::memory_order_acquire)) {
                    LogMessage msg{
                        LogLevel::info,
                        "Stress test thread " + std::to_string(i) + " #" + std::to_string(counter++),
                        std::source_location::current(),
                        std::chrono::system_clock::now()};
                    file_sink->log(msg);
                    total_logs.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        // 运行 2 秒
        std::this_thread::sleep_for(std::chrono::seconds(2));
        stop.store(true, std::memory_order_release);

        for (auto& t : threads) {
            t.join();
        }

        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        file_sink->flush();

        // 验证有日志被记录（降低阈值以适应不同机器性能）
        ASSERT_GT(total_logs.load(), 1000);
    }

    // 验证文件内容
    size_t line_count = count_file_lines(test_file);
    ASSERT_GT(line_count, 1000);

    cleanup_test_file(test_file);
}

// ========================================
// Flush 操作正确性测试
// ========================================

TEST(FileSinkMT, FlushCorrectness) {
    const std::string test_file = "test_filesink_flush.log";
    cleanup_test_file(test_file);

    {
        auto file_sink = create_file_sink(test_file);
        file_sink->set_level(LogLevel::trace);

        std::atomic<bool> stop{false};
        std::atomic<int> flush_count{0};

        // 写入线程
        std::thread writer([&file_sink, &stop]() {
            int counter = 0;
            while (!stop.load(std::memory_order_acquire)) {
                LogMessage msg{
                    LogLevel::info,
                    "Flush test message #" + std::to_string(counter++),
                    std::source_location::current(),
                    std::chrono::system_clock::now()};
                file_sink->log(msg);
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });

        // 多个 flush 线程
        std::vector<std::thread> flushers;
        for (int i = 0; i < 4; ++i) {
            flushers.emplace_back([&file_sink, &stop, &flush_count]() {
                while (!stop.load(std::memory_order_acquire)) {
                    file_sink->flush();
                    flush_count.fetch_add(1, std::memory_order_relaxed);
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            });
        }

        // 运行 1 秒
        std::this_thread::sleep_for(std::chrono::seconds(1));
        stop.store(true, std::memory_order_release);

        writer.join();
        for (auto& t : flushers) {
            t.join();
        }

        // 最终 flush
        file_sink->flush();

        ASSERT_GT(flush_count.load(), 0);
    }

    // 验证文件存在且有内容
    ASSERT_TRUE(fs::exists(test_file));
    size_t line_count = count_file_lines(test_file);
    ASSERT_GT(line_count, 0);

    cleanup_test_file(test_file);
}

// ========================================
// 快速创建/销毁循环测试
// ========================================

TEST(FileSinkMT, RapidCreateDestroy) {
    constexpr int cycles = 20;

    for (int cycle = 0; cycle < cycles; ++cycle) {
        std::string test_file = "test_filesink_cycle_" + std::to_string(cycle) + ".log";
        cleanup_test_file(test_file);

        {
            auto file_sink = create_file_sink(test_file);
            file_sink->set_level(LogLevel::trace);

            // 多线程快速写入
            std::vector<std::thread> threads;
            for (int i = 0; i < 8; ++i) {
                threads.emplace_back([&file_sink, i, cycle]() {
                    for (int j = 0; j < 50; ++j) {
                        LogMessage msg{
                            LogLevel::info,
                            "Cycle " + std::to_string(cycle) + " thread " + std::to_string(i) + " #" + std::to_string(j),
                            std::source_location::current(),
                            std::chrono::system_clock::now()};
                        file_sink->log(msg);
                    }
                });
            }

            for (auto& t : threads) {
                t.join();
            }

            file_sink->flush();
        }

        // 验证文件
        size_t line_count = count_file_lines(test_file);
        ASSERT_EQ(line_count, 8 * 50);

        cleanup_test_file(test_file);
    }
}

// ========================================
// 大消息文件写入测试
// ========================================

TEST(FileSinkMT, LargeMessageWrite) {
    const std::string test_file = "test_filesink_large.log";
    cleanup_test_file(test_file);

    {
        auto file_sink = create_file_sink(test_file);
        file_sink->set_level(LogLevel::trace);

        constexpr int num_threads = 8;
        constexpr int messages_per_thread = 50;

        std::vector<std::thread> threads;
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&file_sink, i]() {
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> dis(1000, 5000);

                for (int j = 0; j < messages_per_thread; ++j) {
                    // 生成大消息
                    std::string large_content(dis(gen), 'X');
                    std::string message = "Thread " + std::to_string(i) + " large #" + std::to_string(j) + ": " + large_content;

                    LogMessage msg{
                        LogLevel::info,
                        message,
                        std::source_location::current(),
                        std::chrono::system_clock::now()};
                    file_sink->log(msg);
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        file_sink->flush();
    }

    // 验证文件
    size_t line_count = count_file_lines(test_file);
    ASSERT_EQ(line_count, 8 * 50);

    // 验证文件大小（每条消息至少 1000 字节）
    auto file_size = fs::file_size(test_file);
    ASSERT_GT(file_size, 8 * 50 * 1000);

    cleanup_test_file(test_file);
}

// ========================================
// 日志级别并发切换测试
// ========================================

TEST(FileSinkMT, ConcurrentLevelChange) {
    const std::string test_file = "test_filesink_level.log";
    cleanup_test_file(test_file);

    {
        auto file_sink = create_file_sink(test_file);
        file_sink->set_level(LogLevel::trace);

        std::atomic<bool> stop{false};
        std::atomic<int> level_changes{0};

        // 写入线程
        std::vector<std::thread> writers;
        for (int i = 0; i < 8; ++i) {
            writers.emplace_back([&file_sink, &stop, i]() {
                int counter = 0;
                while (!stop.load(std::memory_order_acquire)) {
                    LogLevel levels[] = {LogLevel::trace, LogLevel::debug, LogLevel::info,
                                         LogLevel::warning, LogLevel::error};
                    LogMessage msg{
                        levels[counter % 5],
                        "Level test thread " + std::to_string(i) + " #" + std::to_string(counter),
                        std::source_location::current(),
                        std::chrono::system_clock::now()};
                    file_sink->log(msg);
                    ++counter;
                    std::this_thread::yield();
                }
            });
        }

        // 级别切换线程
        std::thread level_changer([&file_sink, &stop, &level_changes]() {
            LogLevel levels[] = {LogLevel::trace, LogLevel::debug, LogLevel::info,
                                 LogLevel::warning, LogLevel::error};
            int index = 0;
            while (!stop.load(std::memory_order_acquire)) {
                file_sink->set_level(levels[index % 5]);
                level_changes.fetch_add(1, std::memory_order_relaxed);
                ++index;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });

        // 运行 1 秒
        std::this_thread::sleep_for(std::chrono::seconds(1));
        stop.store(true, std::memory_order_release);

        for (auto& t : writers) {
            t.join();
        }
        level_changer.join();

        file_sink->flush();

        ASSERT_GT(level_changes.load(), 0);
    }

    // 验证文件存在且有内容
    ASSERT_TRUE(fs::exists(test_file));

    cleanup_test_file(test_file);
}

// ========================================
// 多 FileSink 并发测试
// ========================================

TEST(FileSinkMT, MultipleFileSinks) {
    constexpr int num_sinks = 4;
    constexpr int threads_per_sink = 4;
    constexpr int logs_per_thread = 100;

    std::vector<std::string> test_files;
    std::vector<std::shared_ptr<ISink>> sinks;

    for (int i = 0; i < num_sinks; ++i) {
        std::string filename = "test_filesink_multi_" + std::to_string(i) + ".log";
        cleanup_test_file(filename);
        test_files.push_back(filename);
        sinks.push_back(create_file_sink(filename));
        sinks.back()->set_level(LogLevel::trace);
    }

    std::vector<std::thread> threads;
    for (int s = 0; s < num_sinks; ++s) {
        for (int t = 0; t < threads_per_sink; ++t) {
            threads.emplace_back([&sinks, s, t]() {
                for (int j = 0; j < logs_per_thread; ++j) {
                    LogMessage msg{
                        LogLevel::info,
                        "Sink " + std::to_string(s) + " thread " + std::to_string(t) + " #" + std::to_string(j),
                        std::source_location::current(),
                        std::chrono::system_clock::now()};
                    sinks[s]->log(msg);
                }
            });
        }
    }

    for (auto& t : threads) {
        t.join();
    }

    // Flush 所有 sinks
    for (auto& sink : sinks) {
        sink->flush();
    }

    // 清理 sinks
    sinks.clear();

    // 验证每个文件
    for (int i = 0; i < num_sinks; ++i) {
        size_t line_count = count_file_lines(test_files[i]);
        ASSERT_EQ(line_count, threads_per_sink * logs_per_thread);
        cleanup_test_file(test_files[i]);
    }
}

// ========================================
// Main Function
// ========================================

int main() {
    return CoronaTest::TestRunner::instance().run_all();
}
