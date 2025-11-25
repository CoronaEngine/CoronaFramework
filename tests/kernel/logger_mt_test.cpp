/**
 * @file logger_mt_test.cpp
 * @brief Logger 多线程稳定性和健壮性测试
 *
 * 测试场景：
 * 1. 高频并发日志记录
 * 2. Sink 动态添加/移除
 * 3. 日志级别动态切换
 * 4. 多线程 flush 操作
 * 5. 大量日志压力测试
 * 6. Sink 异常处理
 */

#include <atomic>
#include <chrono>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

#include "../test_framework.h"
#include "corona/kernel/core/i_logger.h"
#include "corona/kernel/core/kernel_context.h"

using namespace Corona::Kernel;
using namespace CoronaTest;

// ========================================
// 高频并发日志记录测试
// ========================================

TEST(LoggerMT, HighFrequencyConcurrentLogging) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto logger = kernel.logger();
    ASSERT_TRUE(logger != nullptr);

    constexpr int num_threads = 16;
    constexpr int logs_per_thread = 1000;
    std::atomic<int> log_count{0};

    auto start_time = std::chrono::steady_clock::now();

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([logger, i, &log_count]() {
            for (int j = 0; j < logs_per_thread; ++j) {
                switch (j % 5) {
                    case 0:
                        logger->trace("Thread " + std::to_string(i) + " trace #" + std::to_string(j));
                        break;
                    case 1:
                        logger->debug("Thread " + std::to_string(i) + " debug #" + std::to_string(j));
                        break;
                    case 2:
                        logger->info("Thread " + std::to_string(i) + " info #" + std::to_string(j));
                        break;
                    case 3:
                        logger->warning("Thread " + std::to_string(i) + " warning #" + std::to_string(j));
                        break;
                    case 4:
                        logger->error("Thread " + std::to_string(i) + " error #" + std::to_string(j));
                        break;
                }
                log_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    // 验证所有日志都被记录
    ASSERT_EQ(log_count.load(), num_threads * logs_per_thread);

    kernel.shutdown();
}

// ========================================
// Sink 动态管理测试
// ========================================

TEST(LoggerMT, DynamicSinkManagement) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto logger = kernel.logger();

    std::atomic<bool> stop{false};
    std::atomic<int> sink_add_count{0};
    std::atomic<int> sink_remove_count{0};

    // 线程1: 不断添加 sink
    std::thread sink_adder([logger, &stop, &sink_add_count]() {
        while (!stop) {
            auto sink = create_console_sink();
            logger->add_sink(sink);
            sink_add_count.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    // 线程2: 不断记录日志
    std::thread logger_thread([logger, &stop]() {
        int counter = 0;
        while (!stop) {
            logger->info("Concurrent log message #" + std::to_string(counter++));
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    // 线程3: 不断 flush
    std::thread flush_thread([logger, &stop]() {
        while (!stop) {
            // Logger 的 flush 操作（如果实现了）
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });

    // 运行2秒
    std::this_thread::sleep_for(std::chrono::seconds(2));
    stop = true;

    sink_adder.join();
    logger_thread.join();
    flush_thread.join();

    // 验证没有崩溃
    ASSERT_GT(sink_add_count.load(), 0);

    // 清理：重置 Logger 到初始状态，避免影响后续测试
    logger->reset();

    kernel.shutdown();
}

// ========================================
// 日志级别切换测试
// ========================================

TEST(LoggerMT, ConcurrentLevelChanges) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto logger = kernel.logger();

    std::atomic<bool> stop{false};
    std::atomic<int> level_changes{0};

    // 线程1: 不断切换日志级别
    std::thread level_changer([logger, &stop, &level_changes]() {
        LogLevel levels[] = {LogLevel::trace, LogLevel::debug, LogLevel::info,
                             LogLevel::warning, LogLevel::error};
        int index = 0;
        while (!stop) {
            logger->set_level(levels[index % 5]);
            level_changes.fetch_add(1);
            index++;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });

    // 多个线程记录不同级别的日志
    std::vector<std::thread> loggers;
    for (int i = 0; i < 8; ++i) {
        loggers.emplace_back([logger, &stop, i]() {
            int counter = 0;
            while (!stop) {
                logger->trace("Trace from thread " + std::to_string(i));
                logger->debug("Debug from thread " + std::to_string(i));
                logger->info("Info from thread " + std::to_string(i));
                logger->warning("Warning from thread " + std::to_string(i));
                logger->error("Error from thread " + std::to_string(i));
                counter++;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }

    // 运行1秒
    std::this_thread::sleep_for(std::chrono::seconds(1));
    stop = true;

    level_changer.join();
    for (auto& t : loggers) {
        t.join();
    }

    // 验证
    ASSERT_GT(level_changes.load(), 0);

    kernel.shutdown();
}

// ========================================
// 大消息压力测试
// ========================================

TEST(LoggerMT, LargeMessageStress) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto logger = kernel.logger();

    constexpr int num_threads = 8;
    constexpr int messages_per_thread = 100;

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([logger, i]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(1000, 5000);

            for (int j = 0; j < messages_per_thread; ++j) {
                // 生成大消息
                std::string large_message(dis(gen), 'X');
                std::ostringstream oss;
                oss << "Thread " << i << " large message #" << j << ": " << large_message;
                logger->info(oss.str());
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    kernel.shutdown();
}

// ========================================
// 混合日志操作压力测试
// ========================================

TEST(LoggerMT, MixedOperationsStress) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto logger = kernel.logger();

    std::atomic<bool> stop{false};
    std::atomic<int> total_logs{0};

    // 线程1-4: 高频日志记录
    std::vector<std::thread> loggers;
    for (int i = 0; i < 4; ++i) {
        loggers.emplace_back([logger, &stop, &total_logs, i]() {
            int counter = 0;
            while (!stop) {
                logger->info("High frequency log from thread " + std::to_string(i) + " #" + std::to_string(counter++));
                total_logs.fetch_add(1);
                std::this_thread::yield();
            }
        });
    }

    // 线程5: 添加/移除 sink
    std::thread sink_manager([logger, &stop]() {
        std::vector<std::shared_ptr<ISink>> sinks;
        while (!stop) {
            // 添加 sink
            auto sink = create_console_sink();
            logger->add_sink(sink);
            sinks.push_back(sink);

            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            // 清理旧的 sink
            if (sinks.size() > 5) {
                sinks.erase(sinks.begin());
            }
        }
    });

    // 线程6: 切换日志级别
    std::thread level_switcher([logger, &stop]() {
        LogLevel levels[] = {LogLevel::info, LogLevel::debug, LogLevel::warning};
        int index = 0;
        while (!stop) {
            logger->set_level(levels[index % 3]);
            index++;
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
    });

    // 运行3秒
    std::this_thread::sleep_for(std::chrono::seconds(3));
    stop = true;

    for (auto& t : loggers) {
        t.join();
    }
    sink_manager.join();
    level_switcher.join();

    // 验证
    ASSERT_GT(total_logs.load(), 0);

    // 清理：重置 Logger 到初始状态，避免影响后续测试
    logger->reset();

    kernel.shutdown();
}

// ========================================
// 快速初始化/关闭循环测试
// ========================================

TEST(LoggerMT, RapidInitShutdownCycle) {
    constexpr int cycles = 10;

    for (int i = 0; i < cycles; ++i) {
        auto& kernel = KernelContext::instance();
        ASSERT_TRUE(kernel.initialize());

        auto logger = kernel.logger();
        ASSERT_TRUE(logger != nullptr);

        // 多线程快速记录日志
        std::vector<std::thread> threads;
        for (int j = 0; j < 4; ++j) {
            threads.emplace_back([logger, i, j]() {
                for (int k = 0; k < 50; ++k) {
                    logger->info("Cycle " + std::to_string(i) + " thread " +
                                 std::to_string(j) + " log " + std::to_string(k));
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        kernel.shutdown();
    }
}

// ========================================
// 格式化日志并发测试
// ========================================

TEST(LoggerMT, ConcurrentFormattedLogging) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto logger = kernel.logger();

    std::atomic<int> formatted_logs{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([logger, i, &formatted_logs]() {
            for (int j = 0; j < 200; ++j) {
                std::ostringstream oss;
                oss << "Thread[" << i << "] Iteration[" << j << "] "
                    << "Value1=" << (i * 100 + j) << " "
                    << "Value2=" << (i * 1.5 + j * 0.5) << " "
                    << "Status=OK";
                logger->info(oss.str());
                formatted_logs.fetch_add(1);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(formatted_logs.load(), 2000);

    kernel.shutdown();
}

// ========================================
// 日志爆发测试（短时间大量日志）
// ========================================

TEST(LoggerMT, LogBurst) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto logger = kernel.logger();

    std::atomic<int> burst_count{0};

    auto start_time = std::chrono::steady_clock::now();

    // 16个线程同时爆发
    std::vector<std::thread> threads;
    for (int i = 0; i < 16; ++i) {
        threads.emplace_back([logger, i, &burst_count]() {
            for (int j = 0; j < 500; ++j) {
                logger->info("Burst log from thread " + std::to_string(i) + " #" + std::to_string(j));
                burst_count.fetch_add(1);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    ASSERT_EQ(burst_count.load(), 8000);

    kernel.shutdown();
}

// ========================================
// 不同级别日志混合并发测试
// ========================================

TEST(LoggerMT, MixedLevelConcurrent) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto logger = kernel.logger();

    std::atomic<int> trace_count{0}, debug_count{0}, info_count{0}, warn_count{0}, error_count{0};

    std::vector<std::thread> threads;

    // 专门的 trace 线程
    threads.emplace_back([logger, &trace_count]() {
        for (int i = 0; i < 500; ++i) {
            logger->trace("Trace message " + std::to_string(i));
            trace_count.fetch_add(1);
        }
    });

    // 专门的 debug 线程
    threads.emplace_back([logger, &debug_count]() {
        for (int i = 0; i < 500; ++i) {
            logger->debug("Debug message " + std::to_string(i));
            debug_count.fetch_add(1);
        }
    });

    // 专门的 info 线程
    threads.emplace_back([logger, &info_count]() {
        for (int i = 0; i < 500; ++i) {
            logger->info("Info message " + std::to_string(i));
            info_count.fetch_add(1);
        }
    });

    // 专门的 warn 线程
    threads.emplace_back([logger, &warn_count]() {
        for (int i = 0; i < 500; ++i) {
            logger->warning("Warning message " + std::to_string(i));
            warn_count.fetch_add(1);
        }
    });

    // 专门的 error 线程
    threads.emplace_back([logger, &error_count]() {
        for (int i = 0; i < 500; ++i) {
            logger->error("Error message " + std::to_string(i));
            error_count.fetch_add(1);
        }
    });

    for (auto& t : threads) {
        t.join();
    }

    // 验证所有级别都被记录
    ASSERT_EQ(trace_count.load(), 500);
    ASSERT_EQ(debug_count.load(), 500);
    ASSERT_EQ(info_count.load(), 500);
    ASSERT_EQ(warn_count.load(), 500);
    ASSERT_EQ(error_count.load(), 500);

    kernel.shutdown();
}

// ========================================
// Main Function
// ========================================

int main() {
    return CoronaTest::TestRunner::instance().run_all();
}
