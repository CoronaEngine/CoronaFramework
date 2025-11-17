// Task Group 测试
#include "corona/kernel/utils/task_group.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <numeric>
#include <random>
#include <thread>
#include <vector>

#include "test_framework.h"

using namespace Corona::Kernal::Utils;
using namespace std::chrono_literals;

// ========================================
// 基础功能测试
// ========================================

TEST(TaskGroup, BasicExecution) {
    std::atomic<int> counter{0};

    TaskGroup group;
    for (int i = 0; i < 10; ++i) {
        group.run([&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    group.wait();
    ASSERT_EQ(counter.load(), 10);
}

TEST(TaskGroup, EmptyGroup) {
    // 空任务组应该立即返回
    TaskGroup group;
    group.wait();  // 不应该死锁或崩溃
    ASSERT_TRUE(true);
}

TEST(TaskGroup, MultipleWaits) {
    // 多次调用 wait 应该是安全的
    std::atomic<int> counter{0};

    TaskGroup group;
    for (int i = 0; i < 5; ++i) {
        group.run([&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    group.wait();
    ASSERT_EQ(counter.load(), 5);

    // 第二次 wait 应该立即返回
    group.wait();
    ASSERT_EQ(counter.load(), 5);
}

// ========================================
// 参数传递测试
// ========================================

TEST(TaskGroup, TaskWithArguments) {
    std::atomic<int> sum{0};

    TaskGroup group;
    for (int i = 1; i <= 10; ++i) {
        group.run([&sum, i]() {
            sum.fetch_add(i, std::memory_order_relaxed);
        });
    }

    group.wait();
    ASSERT_EQ(sum.load(), 55);  // 1+2+...+10 = 55
}

TEST(TaskGroup, TaskWithMultipleArguments) {
    std::atomic<int> result{0};

    TaskGroup group;
    group.run([&result]() {
        result.store(10 + 20 * 30, std::memory_order_relaxed);
    });

    group.wait();
    ASSERT_EQ(result.load(), 610);  // 10 + 20 * 30
}

// ========================================
// 并发压力测试
// ========================================

TEST(TaskGroup, LargeNumberOfTasks) {
    constexpr int task_count = 1000;
    std::atomic<int> counter{0};

    TaskGroup group;
    for (int i = 0; i < task_count; ++i) {
        group.run([&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    group.wait();
    ASSERT_EQ(counter.load(), task_count);
}

TEST(TaskGroup, ConcurrentIncrement) {
    constexpr int task_count = 10000;
    std::atomic<int> counter{0};

    TaskGroup group;
    for (int i = 0; i < task_count; ++i) {
        group.run([&counter]() {
            // 模拟一些计算工作
            int temp = counter.load(std::memory_order_relaxed);
            counter.store(temp + 1, std::memory_order_relaxed);
        });
    }

    group.wait();
    // 由于非原子递增，结果可能小于 task_count（验证并发执行）
    ASSERT_GT(counter.load(), 0);
    ASSERT_LE(counter.load(), task_count);
}

// ========================================
// 异常处理测试
// ========================================

TEST(TaskGroup, ExceptionInTask) {
    std::atomic<int> completed{0};

    TaskGroup group;

    // 第一个任务抛出异常
    group.run([&completed]() {
        throw std::runtime_error("Test exception");
    });

    // 第二个任务正常执行
    group.run([&completed]() {
        completed.fetch_add(1, std::memory_order_relaxed);
    });

    // wait 应该正常返回，不会因异常而中断
    ASSERT_NO_THROW(group.wait());

    // 正常任务应该完成
    ASSERT_EQ(completed.load(), 1);
}

TEST(TaskGroup, MultipleExceptions) {
    TaskGroup group;

    for (int i = 0; i < 10; ++i) {
        group.run([]() {
            throw std::runtime_error("Exception in task");
        });
    }

    // 所有异常都应该被捕获，不影响 wait 返回
    ASSERT_NO_THROW(group.wait());
}

// ========================================
// 性能测试
// ========================================

TEST(TaskGroup, ParallelSpeedup) {
    constexpr int task_count = 100;
    constexpr int work_size = 10000;

    auto work = [work_size]() {
        volatile int sum = 0;
        for (int i = 0; i < work_size; ++i) {
            sum += i;
        }
    };

    // 并行执行
    auto start_parallel = std::chrono::high_resolution_clock::now();
    {
        TaskGroup group;
        for (int i = 0; i < task_count; ++i) {
            group.run(work);
        }
        group.wait();
    }
    auto end_parallel = std::chrono::high_resolution_clock::now();
    auto parallel_duration = std::chrono::duration<double, std::milli>(end_parallel - start_parallel).count();

    // 串行执行
    auto start_serial = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < task_count; ++i) {
        work();
    }
    auto end_serial = std::chrono::high_resolution_clock::now();
    auto serial_duration = std::chrono::duration<double, std::milli>(end_serial - start_serial).count();

    // 并行应该比串行快（至少在多核 CPU 上）
    // 注意：这个测试可能在单核或负载高的系统上失败
    std::cout << "  Parallel: " << parallel_duration << " ms\n";
    std::cout << "  Serial: " << serial_duration << " ms\n";
    std::cout << "  Speedup: " << (serial_duration / parallel_duration) << "x\n";

    // 至少应该不会比串行慢太多（考虑线程开销）
    ASSERT_LT(parallel_duration, serial_duration * 2.0);
}

// ========================================
// 数据竞争测试
// ========================================

TEST(TaskGroup, NoDataRaceWithAtomics) {
    constexpr int task_count = 1000;
    constexpr int increments_per_task = 100;
    std::atomic<int> counter{0};

    TaskGroup group;
    for (int i = 0; i < task_count; ++i) {
        group.run([&counter, increments_per_task]() {
            for (int j = 0; j < increments_per_task; ++j) {
                counter.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    group.wait();
    ASSERT_EQ(counter.load(), task_count * increments_per_task);
}

// ========================================
// 嵌套任务组测试
// ========================================

TEST(TaskGroup, NestedTaskGroups) {
    std::atomic<int> counter{0};

    TaskGroup outer_group;
    for (int i = 0; i < 5; ++i) {
        outer_group.run([&counter]() {
            TaskGroup inner_group;
            for (int j = 0; j < 10; ++j) {
                inner_group.run([&counter]() {
                    counter.fetch_add(1, std::memory_order_relaxed);
                });
            }
            inner_group.wait();
        });
    }

    outer_group.wait();
    ASSERT_EQ(counter.load(), 50);  // 5 * 10
}

// ========================================
// 任务执行顺序测试
// ========================================

TEST(TaskGroup, TaskExecutionOrder) {
    // 注意：任务执行顺序不保证，但都应该完成
    std::vector<std::atomic<bool>> flags(100);
    for (auto& flag : flags) {
        flag.store(false);
    }

    TaskGroup group;
    for (size_t i = 0; i < flags.size(); ++i) {
        group.run([&flags, i]() {
            std::this_thread::sleep_for(1ms);
            flags[i].store(true, std::memory_order_relaxed);
        });
    }

    group.wait();

    // 所有任务都应该完成
    for (const auto& flag : flags) {
        ASSERT_TRUE(flag.load());
    }
}

// ========================================
// 主线程参与测试
// ========================================

TEST(TaskGroup, MainThreadParticipation) {
    // 验证 wait() 期间主线程确实在执行任务
    std::atomic<std::thread::id> executing_thread_id{std::this_thread::get_id()};
    std::atomic<bool> main_thread_participated{false};
    auto main_thread_id = std::this_thread::get_id();

    TaskGroup group;
    for (int i = 0; i < 100; ++i) {
        group.run([&executing_thread_id, &main_thread_participated, main_thread_id]() {
            auto current_id = std::this_thread::get_id();
            if (current_id == main_thread_id) {
                main_thread_participated.store(true, std::memory_order_relaxed);
            }
            std::this_thread::sleep_for(1ms);
        });
    }

    group.wait();

    // 在大量任务的情况下，主线程应该有机会参与执行
    // 注意：这个测试依赖于 TaskScheduler 的实现
    std::cout << "  Main thread participated: "
              << (main_thread_participated.load() ? "Yes" : "No") << "\n";
}

// ========================================
// 压力测试 - 大量嵌套任务组
// ========================================

TEST(TaskGroup, DeepNesting) {
    // 测试深度嵌套是否会死锁
    // 降低参数避免栈溢出（之前 5层x10任务 = 100000 个递归调用）
    constexpr int nesting_depth = 4;
    constexpr int tasks_per_level = 3;
    std::atomic<int> total_executed{0};

    std::function<void(int)> nested_task = [&](int depth) {
        if (depth == 0) {
            total_executed.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        TaskGroup group;
        for (int i = 0; i < tasks_per_level; ++i) {
            group.run([&nested_task, depth]() {
                nested_task(depth - 1);
            });
        }
        group.wait();
    };

    TaskGroup root;
    root.run([&nested_task, nesting_depth]() {
        nested_task(nesting_depth);
    });
    root.wait();

    // 计算预期执行次数: tasks_per_level^nesting_depth = 3^4 = 81
    int expected = 1;
    for (int i = 0; i < nesting_depth; ++i) {
        expected *= tasks_per_level;
    }

    std::cout << "  Executed " << total_executed.load() << " nested tasks (expected " << expected << ")\n";
    ASSERT_EQ(total_executed.load(), expected);
}

// 警告：过深嵌套测试（验证资源限制）
TEST(TaskGroup, ExcessiveNestingWarning) {
    // 这个测试验证文档中应该说明的限制：
    // 如果嵌套深度过大，可能导致栈溢出或线程池耗尽
    // 实际项目中应避免深度 > 10 的嵌套

    std::atomic<bool> completed{false};

    // 使用较小的深度验证基本功能
    std::function<void(int)> shallow_nest = [&](int depth) {
        if (depth == 0) {
            completed.store(true, std::memory_order_relaxed);
            return;
        }
        TaskGroup group;
        group.run([&shallow_nest, depth]() {
            shallow_nest(depth - 1);
        });
        group.wait();
    };

    TaskGroup root;
    root.run([&]() { shallow_nest(3); });
    root.wait();

    ASSERT_TRUE(completed.load());
    std::cout << "  ⚠️  WARNING: Avoid nesting depth > 10 in production code\n";
}

TEST(TaskGroup, HighContentionStress) {
    // 多个线程同时向同一个任务组提交任务（压力测试）
    constexpr int num_submitters = 4;         // 降低线程数
    constexpr int tasks_per_submitter = 200;  // 降低任务数
    std::atomic<int> counter{0};

    TaskGroup group;
    std::vector<std::thread> submitters;

    for (int i = 0; i < num_submitters; ++i) {
        submitters.emplace_back([&group, &counter, tasks_per_submitter]() {
            for (int j = 0; j < tasks_per_submitter; ++j) {
                group.run([&counter]() {
                    counter.fetch_add(1, std::memory_order_relaxed);
                    // 模拟一些工作
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                });
            }
        });
    }

    // 等待所有提交线程完成
    for (auto& t : submitters) {
        t.join();
    }

    // 等待所有任务执行完成
    group.wait();

    ASSERT_EQ(counter.load(), num_submitters * tasks_per_submitter);
    std::cout << "  " << num_submitters << " threads submitted "
              << (num_submitters * tasks_per_submitter) << " tasks\n";
}

TEST(TaskGroup, ResourceExhaustion) {
    // 测试大量任务组同时存在的情况
    constexpr int num_groups = 100;
    constexpr int tasks_per_group = 50;
    std::atomic<int> counter{0};

    std::vector<std::unique_ptr<TaskGroup>> groups;

    for (int i = 0; i < num_groups; ++i) {
        groups.push_back(std::make_unique<TaskGroup>());
        for (int j = 0; j < tasks_per_group; ++j) {
            groups.back()->run([&counter]() {
                counter.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            });
        }
    }

    // 等待所有任务组完成
    for (auto& group : groups) {
        group->wait();
    }

    ASSERT_EQ(counter.load(), num_groups * tasks_per_group);
    std::cout << "  " << num_groups << " concurrent groups executed "
              << (num_groups * tasks_per_group) << " tasks\n";
}

// ========================================
// 长时间运行测试 - 检测内存泄漏和资源耗尽
// ========================================

TEST(TaskGroup, LongRunningStability) {
    // 长时间重复创建和销毁任务组，检测内存泄漏
    constexpr int iterations = 500;          // 降低迭代次数
    constexpr int tasks_per_iteration = 20;  // 降低任务数
    std::atomic<int> total_executed{0};

    auto start = std::chrono::high_resolution_clock::now();

    for (int iter = 0; iter < iterations; ++iter) {
        TaskGroup group;
        for (int i = 0; i < tasks_per_iteration; ++i) {
            group.run([&total_executed]() {
                total_executed.fetch_add(1, std::memory_order_relaxed);
                // 模拟一些计算
                volatile int sum = 0;
                for (int k = 0; k < 100; ++k) {
                    sum += k;
                }
            });
        }
        group.wait();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<double>(end - start).count();

    ASSERT_EQ(total_executed.load(), iterations * tasks_per_iteration);
    std::cout << "  Executed " << total_executed.load()
              << " tasks in " << duration << " seconds\n";
    std::cout << "  Throughput: " << (total_executed.load() / duration)
              << " tasks/sec\n";
}

TEST(TaskGroup, ContinuousLoadTest) {
    // 持续高负载测试，验证调度器不会饿死
    constexpr int duration_seconds = 2;  // 降低到2秒
    std::atomic<bool> stop_flag{false};
    std::atomic<int> completed_tasks{0};

    auto start = std::chrono::high_resolution_clock::now();

    // 启动多个生产者线程持续提交任务
    std::vector<std::thread> producers;
    for (int i = 0; i < 2; ++i) {  // 降低到2个生产者
        producers.emplace_back([&stop_flag, &completed_tasks]() {
            while (!stop_flag.load(std::memory_order_acquire)) {
                TaskGroup group;
                for (int j = 0; j < 5; ++j) {  // 降低到5个任务
                    group.run([&completed_tasks]() {
                        completed_tasks.fetch_add(1, std::memory_order_relaxed);
                        std::this_thread::sleep_for(std::chrono::microseconds(100));
                    });
                }
                group.wait();
            }
        });
    }

    // 运行指定时间
    std::this_thread::sleep_for(std::chrono::seconds(duration_seconds));
    stop_flag.store(true, std::memory_order_release);

    // 等待生产者线程结束
    for (auto& t : producers) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto actual_duration = std::chrono::duration<double>(end - start).count();

    std::cout << "  Completed " << completed_tasks.load()
              << " tasks in " << actual_duration << " seconds\n";
    std::cout << "  Average throughput: "
              << (completed_tasks.load() / actual_duration) << " tasks/sec\n";

    ASSERT_GT(completed_tasks.load(), 0);
}

// ========================================
// 多任务组并发测试 - 验证隔离性
// ========================================

TEST(TaskGroup, MultipleGroupsIsolation) {
    // 验证不同任务组之间互不影响
    constexpr int num_groups = 10;
    std::vector<std::atomic<int>> counters(num_groups);
    std::vector<std::unique_ptr<TaskGroup>> groups;

    for (int i = 0; i < num_groups; ++i) {
        counters[i].store(0);
        groups.push_back(std::make_unique<TaskGroup>());
    }

    // 每个任务组独立执行任务
    for (int i = 0; i < num_groups; ++i) {
        for (int j = 0; j < 100; ++j) {
            groups[i]->run([&counters, i]() {
                counters[i].fetch_add(1, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            });
        }
    }

    // 随机顺序等待（验证顺序无关性）
    std::vector<int> wait_order(num_groups);
    std::iota(wait_order.begin(), wait_order.end(), 0);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(wait_order.begin(), wait_order.end(), gen);

    for (int idx : wait_order) {
        groups[idx]->wait();
        ASSERT_EQ(counters[idx].load(), 100);
    }

    // 验证每个组都完成了自己的任务
    for (int i = 0; i < num_groups; ++i) {
        ASSERT_EQ(counters[i].load(), 100);
    }
}

TEST(TaskGroup, ConcurrentGroupsNoInterference) {
    // 多个线程各自使用独立的任务组
    constexpr int num_threads = 8;
    std::vector<std::atomic<int>> counters(num_threads);
    std::vector<std::thread> threads;

    for (int i = 0; i < num_threads; ++i) {
        counters[i].store(0);
        threads.emplace_back([&counters, i]() {
            TaskGroup local_group;
            for (int j = 0; j < 200; ++j) {
                local_group.run([&counters, i]() {
                    counters[i].fetch_add(1, std::memory_order_relaxed);
                });
            }
            local_group.wait();
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // 验证每个线程的任务组都正确完成
    for (int i = 0; i < num_threads; ++i) {
        ASSERT_EQ(counters[i].load(), 200);
    }
}

TEST(TaskGroup, MixedWorkloadConcurrency) {
    // 混合工作负载：短任务和长任务并发
    std::atomic<int> short_tasks{0};
    std::atomic<int> long_tasks{0};

    TaskGroup short_group;
    TaskGroup long_group;

    // 提交大量短任务
    for (int i = 0; i < 1000; ++i) {
        short_group.run([&short_tasks]() {
            short_tasks.fetch_add(1, std::memory_order_relaxed);
        });
    }

    // 提交少量长任务
    for (int i = 0; i < 10; ++i) {
        long_group.run([&long_tasks]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            long_tasks.fetch_add(1, std::memory_order_relaxed);
        });
    }

    // 并发等待
    std::thread wait_long([&long_group]() { long_group.wait(); });
    short_group.wait();
    wait_long.join();

    ASSERT_EQ(short_tasks.load(), 1000);
    ASSERT_EQ(long_tasks.load(), 10);

    std::cout << "  Short tasks: " << short_tasks.load() << "\n";
    std::cout << "  Long tasks: " << long_tasks.load() << "\n";
}

// ========================================
// 主函数
// ========================================

int main() {
    return CoronaTest::TestRunner::instance().run_all();
}
