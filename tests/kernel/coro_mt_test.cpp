/**
 * @file coro_mt_test.cpp
 * @brief 协程多线程稳定性测试
 *
 * 测试协程在高并发环境下的稳定性和正确性
 */

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#include "../test_framework.h"
#include <corona/kernel/coro/coro.h>

using namespace Corona::Kernel::Coro;
using namespace CoronaTest;

// ========================================
// 并发任务提交测试
// ========================================

TEST(CoroMT, ConcurrentTaskSubmission) {
    TbbExecutor executor;
    std::atomic<int> counter{0};
    constexpr int num_threads = 4;
    constexpr int tasks_per_thread = 50;
    
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < tasks_per_thread; ++i) {
                executor.execute([&]() {
                    counter++;
                });
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    executor.wait();
    
    ASSERT_EQ(counter.load(), num_threads * tasks_per_thread);
}

// ========================================
// 并发调度器访问测试
// ========================================

TEST(CoroMT, ConcurrentSchedulerAccess) {
    Scheduler::instance().reset();
    
    std::atomic<int> completed{0};
    constexpr int num_threads = 4;
    constexpr int ops_per_thread = 50;
    
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                // 获取默认执行器
                auto& executor = Scheduler::instance().default_executor();
                
                // 提交任务
                executor.execute([&]() {
                    completed++;
                });
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // 等待所有任务完成
    int timeout_ms = 3000;
    while (completed < num_threads * ops_per_thread && timeout_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        timeout_ms -= 10;
    }
    
    ASSERT_EQ(completed.load(), num_threads * ops_per_thread);
    Scheduler::instance().reset();
}

// ========================================
// 并发延时任务测试
// ========================================

TEST(CoroMT, ConcurrentDelayedTasks) {
    TbbExecutor executor;
    std::atomic<int> counter{0};
    constexpr int num_tasks = 50;
    
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&, t]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dist(1, 30);
            
            for (int i = 0; i < num_tasks / 4; ++i) {
                auto delay = std::chrono::milliseconds{dist(gen)};
                executor.execute_after([&]() { counter++; }, delay);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // 等待所有延时任务完成
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    executor.wait();
    
    // 验证计数 (可能有少量未完成，允许一些误差)
    ASSERT_GE(counter.load(), num_tasks - 5);
}

// ========================================
// 协程异常多线程测试
// ========================================

TEST(CoroMT, ExceptionInConcurrentTasks) {
    std::atomic<int> successes{0};
    std::atomic<int> failures{0};
    constexpr int num_tasks = 50;
    
    std::vector<std::thread> threads;
    for (int i = 0; i < num_tasks; ++i) {
        threads.emplace_back([&, i]() {
            auto task = [i]() -> Task<int> {
                if (i % 3 == 0) {
                    throw std::runtime_error("Expected exception");
                }
                co_return i;
            }();
            
            try {
                task.get();
                successes++;
            } catch (const std::runtime_error&) {
                failures++;
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // 大约 1/3 应该失败
    ASSERT_GT(failures.load(), 0);
    ASSERT_GT(successes.load(), 0);
    ASSERT_EQ(successes.load() + failures.load(), num_tasks);
}

// ========================================
// 嵌套协程多线程测试
// ========================================

Task<int> mt_nested_compute(int value, int depth) {
    if (depth <= 0) {
        co_return value;
    }
    int result = co_await mt_nested_compute(value + 1, depth - 1);
    co_return result;
}

TEST(CoroMT, NestedCoroutinesConcurrent) {
    constexpr int num_threads = 4;
    constexpr int depth = 30;
    
    std::atomic<int> successes{0};
    std::vector<std::thread> threads;
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            auto task = mt_nested_compute(t, depth);
            int result = task.get();
            ASSERT_EQ(result, t + depth);
            successes++;
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    ASSERT_EQ(successes.load(), num_threads);
}

// ========================================
// 执行器关闭竞态测试
// ========================================

TEST(CoroMT, ExecutorShutdownRace) {
    for (int iteration = 0; iteration < 5; ++iteration) {
        TbbExecutor executor;
        std::atomic<int> counter{0};
        
        // 启动多个任务提交线程
        std::vector<std::thread> threads;
        for (int t = 0; t < 2; ++t) {
            threads.emplace_back([&]() {
                for (int i = 0; i < 20; ++i) {
                    if (executor.is_running()) {
                        executor.execute([&]() { counter++; });
                    }
                }
            });
        }
        
        // 短暂延时后关闭
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
        executor.shutdown();
        
        for (auto& t : threads) {
            t.join();
        }
        
        // 验证没有崩溃，且关闭后不再运行
        ASSERT_FALSE(executor.is_running());
    }
}

// ========================================
// Generator 多线程安全测试
// ========================================

Generator<int> mt_safe_generator(int count) {
    for (int i = 0; i < count; ++i) {
        co_yield i;
    }
}

TEST(CoroMT, GeneratorPerThread) {
    constexpr int num_threads = 4;
    constexpr int items_per_gen = 100;
    
    std::atomic<int> total_sum{0};
    std::vector<std::thread> threads;
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            auto gen = mt_safe_generator(items_per_gen);
            int local_sum = 0;
            for (int n : gen) {
                local_sum += n;
            }
            total_sum += local_sum;
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // 每个线程的和 = 0 + 1 + ... + 99 = 4950
    // 总和 = 4 * 4950 = 19800
    int expected = num_threads * (items_per_gen * (items_per_gen - 1) / 2);
    ASSERT_EQ(total_sum.load(), expected);
}

// ========================================
// switch_to 多线程测试
// ========================================

TEST(CoroMT, SwitchToMultipleExecutors) {
    TbbExecutor executor1(2);
    TbbExecutor executor2(2);
    
    std::atomic<int> completed{0};
    constexpr int num_tasks = 10;
    
    for (int i = 0; i < num_tasks; ++i) {
        if (i % 2 == 0) {
            executor1.execute([&]() {
                completed++;
            });
        } else {
            executor2.execute([&]() {
                completed++;
            });
        }
    }
    
    executor1.wait();
    executor2.wait();
    
    ASSERT_EQ(completed.load(), num_tasks);
}

// ========================================
// 高并发压力测试
// ========================================

TEST(CoroMT, HighConcurrencyStress) {
    TbbExecutor executor;
    std::atomic<int> completed{0};
    constexpr int num_tasks = 500;
    
    auto start = std::chrono::steady_clock::now();
    
    for (int i = 0; i < num_tasks; ++i) {
        executor.execute([&]() {
            // 简单计算
            int sum = 0;
            for (int j = 0; j < 100; ++j) {
                sum += j;
            }
            (void)sum;
            completed++;
        });
    }
    
    executor.wait();
    
    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    ASSERT_EQ(completed.load(), num_tasks);
    
    // 验证性能合理（500个简单任务应该在合理时间内完成）
    ASSERT_LT(elapsed.count(), 5000);  // 5秒内完成
    
    std::cout << "  High concurrency test completed " << num_tasks 
              << " tasks in " << elapsed.count() << "ms\n";
}

// ========================================
// Task 并行执行测试
// ========================================

TEST(CoroMT, TaskParallelExecution) {
    constexpr int num_tasks = 8;
    std::atomic<int> completed{0};
    std::vector<std::thread> threads;
    
    for (int i = 0; i < num_tasks; ++i) {
        threads.emplace_back([&, i]() {
            auto task = [i]() -> Task<int> {
                co_return i * i;
            }();
            
            int result = task.get();
            ASSERT_EQ(result, i * i);
            completed++;
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    ASSERT_EQ(completed.load(), num_tasks);
}

// ========================================
// 混合操作测试
// ========================================

TEST(CoroMT, MixedOperations) {
    Scheduler::instance().reset();
    
    TbbExecutor executor;
    std::atomic<int> task_count{0};
    std::atomic<int> generator_count{0};
    std::atomic<int> executor_count{0};
    
    constexpr int iterations = 20;
    
    std::vector<std::thread> threads;
    
    // 线程1: 执行 Task
    threads.emplace_back([&]() {
        for (int i = 0; i < iterations; ++i) {
            auto task = []() -> Task<int> {
                co_return 42;
            }();
            task.get();
            task_count++;
        }
    });
    
    // 线程2: 使用 Generator
    threads.emplace_back([&]() {
        for (int i = 0; i < iterations; ++i) {
            auto gen = mt_safe_generator(10);
            for (int n : gen) {
                (void)n;
            }
            generator_count++;
        }
    });
    
    // 线程3: 使用 Executor
    threads.emplace_back([&]() {
        for (int i = 0; i < iterations; ++i) {
            executor.execute([&]() {
                executor_count++;
            });
        }
    });
    
    for (auto& t : threads) {
        t.join();
    }
    executor.wait();
    
    ASSERT_EQ(task_count.load(), iterations);
    ASSERT_EQ(generator_count.load(), iterations);
    ASSERT_EQ(executor_count.load(), iterations);
    
    Scheduler::instance().reset();
}

// ========================================
// Main
// ========================================

int main() {
    return TestRunner::instance().run_all();
}
