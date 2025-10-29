/**
 * @file vfs_mt_test.cpp
 * @brief VFS (Virtual File System) 多线程稳定性和健壮性测试
 *
 * 测试场景：
 * 1. 并发挂载/卸载
 * 2. 路径解析竞争
 * 3. 多线程文件访问
 * 4. 挂载点冲突处理
 * 5. 符号链接和路径规范化
 */

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "../test_framework.h"
#include "corona/kernel/core/i_vfs.h"
#include "corona/kernel/core/kernel_context.h"

using namespace Corona::Kernel;
using namespace CoronaTest;

// ========================================
// 并发挂载测试
// ========================================

TEST(VFSMT, ConcurrentMount) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto vfs = kernel.vfs();
    ASSERT_TRUE(vfs != nullptr);

    constexpr int num_threads = 10;
    std::atomic<int> mount_success_count{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([vfs, i, &mount_success_count]() {
            std::string virtual_path = "/mount" + std::to_string(i) + "/";
            std::string physical_path = "./test_mount" + std::to_string(i) + "/";

            try {
                vfs->mount(virtual_path, physical_path);
                mount_success_count.fetch_add(1, std::memory_order_relaxed);
            } catch (...) {
                // 挂载可能失败
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // 验证至少有一些挂载成功
    ASSERT_GT(mount_success_count.load(), 0);

    kernel.shutdown();
}

// ========================================
// 并发路径解析测试
// ========================================

TEST(VFSMT, ConcurrentResolve) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto vfs = kernel.vfs();

    // 设置一些挂载点
    vfs->mount("/data/", "./data/");
    vfs->mount("/config/", "./config/");
    vfs->mount("/assets/", "./assets/");

    constexpr int num_threads = 16;
    constexpr int resolves_per_thread = 500;
    std::atomic<int> resolve_count{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([vfs, i, &resolve_count]() {
            std::string paths[] = {
                "/data/test.txt",
                "/config/settings.json",
                "/assets/image.png",
                "/data/subdir/file.dat",
                "/config/game.ini"};

            for (int j = 0; j < resolves_per_thread; ++j) {
                std::string path = paths[j % 5];
                auto resolved = vfs->resolve(path);
                resolve_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(resolve_count.load(), num_threads * resolves_per_thread);

    kernel.shutdown();
}

// ========================================
// 挂载和解析并发测试
// ========================================

TEST(VFSMT, ConcurrentMountAndResolve) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto vfs = kernel.vfs();

    std::atomic<bool> stop{false};
    std::atomic<int> mount_operations{0};
    std::atomic<int> resolve_operations{0};

    // 线程1-3: 不断挂载
    std::vector<std::thread> mount_threads;
    for (int i = 0; i < 3; ++i) {
        mount_threads.emplace_back([vfs, i, &stop, &mount_operations]() {
            int counter = 0;
            while (!stop) {
                std::string virtual_path = "/dynamic" + std::to_string(i) + "_" + std::to_string(counter) + "/";
                std::string physical_path = "./dynamic" + std::to_string(i) + "_" + std::to_string(counter) + "/";

                try {
                    vfs->mount(virtual_path, physical_path);
                    mount_operations.fetch_add(1);
                } catch (...) {
                    // 可能会有挂载冲突
                }

                counter++;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }

    // 线程4-8: 不断解析路径
    std::vector<std::thread> resolve_threads;
    for (int i = 0; i < 5; ++i) {
        resolve_threads.emplace_back([vfs, i, &stop, &resolve_operations]() {
            while (!stop) {
                std::string path = "/dynamic" + std::to_string(i % 3) + "_0/test.txt";
                auto resolved = vfs->resolve(path);
                resolve_operations.fetch_add(1);
                std::this_thread::yield();
            }
        });
    }

    // 运行1秒
    std::this_thread::sleep_for(std::chrono::seconds(1));
    stop = true;

    for (auto& t : mount_threads) {
        t.join();
    }
    for (auto& t : resolve_threads) {
        t.join();
    }

    // 验证操作都执行了
    ASSERT_GT(mount_operations.load(), 0);
    ASSERT_GT(resolve_operations.load(), 0);

    kernel.shutdown();
}

// ========================================
// 并发卸载测试
// ========================================

TEST(VFSMT, ConcurrentUnmount) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto vfs = kernel.vfs();

    // 先挂载多个路径
    for (int i = 0; i < 10; ++i) {
        std::string virtual_path = "/unmount_test" + std::to_string(i) + "/";
        std::string physical_path = "./unmount_test" + std::to_string(i) + "/";
        vfs->mount(virtual_path, physical_path);
    }

    std::atomic<int> unmount_count{0};

    // 多个线程同时卸载
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([vfs, i, &unmount_count]() {
            std::string virtual_path = "/unmount_test" + std::to_string(i) + "/";
            try {
                vfs->unmount(virtual_path);
                unmount_count.fetch_add(1);
            } catch (...) {
                // 卸载可能失败
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // 验证
    ASSERT_GT(unmount_count.load(), 0);

    kernel.shutdown();
}

// ========================================
// 挂载点冲突测试
// ========================================

TEST(VFSMT, MountPointConflicts) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto vfs = kernel.vfs();

    std::atomic<int> conflict_count{0};
    constexpr int num_threads = 10;

    // 多个线程尝试挂载到相同的虚拟路径
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([vfs, i, &conflict_count]() {
            std::string virtual_path = "/conflict/";  // 相同的虚拟路径
            std::string physical_path = "./conflict" + std::to_string(i) + "/";

            try {
                vfs->mount(virtual_path, physical_path);
            } catch (...) {
                // 预期会有冲突异常
                conflict_count.fetch_add(1);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // 应该只有一个成功，其他的都冲突
    // conflict_count 应该接近 num_threads - 1

    kernel.shutdown();
}

// ========================================
// 路径规范化并发测试
// ========================================

TEST(VFSMT, ConcurrentPathNormalization) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto vfs = kernel.vfs();
    vfs->mount("/root/", "./root/");

    constexpr int num_threads = 8;
    constexpr int normalizations_per_thread = 500;

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([vfs, i]() {
            std::string test_paths[] = {
                "/root/./test.txt",
                "/root/../root/test.txt",
                "/root/subdir/../test.txt",
                "/root/a/b/c/../../test.txt",
                "/root/./././test.txt"};

            for (int j = 0; j < normalizations_per_thread; ++j) {
                std::string path = test_paths[j % 5];
                auto resolved = vfs->resolve(path);
                // 只验证不崩溃
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    kernel.shutdown();
}

// ========================================
// 混合操作压力测试
// ========================================

TEST(VFSMT, MixedOperationsStress) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto vfs = kernel.vfs();

    std::atomic<bool> stop{false};
    std::atomic<int> total_operations{0};

    // 线程1-2: 挂载操作
    std::vector<std::thread> mount_threads;
    for (int i = 0; i < 2; ++i) {
        mount_threads.emplace_back([vfs, i, &stop, &total_operations]() {
            int counter = 0;
            while (!stop) {
                std::string virtual_path = "/stress_mount" + std::to_string(i) + "_" + std::to_string(counter % 5) + "/";
                std::string physical_path = "./stress_mount" + std::to_string(i) + "_" + std::to_string(counter % 5) + "/";

                try {
                    vfs->mount(virtual_path, physical_path);
                    total_operations.fetch_add(1);
                } catch (...) {
                }

                counter++;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        });
    }

    // 线程3-6: 解析操作
    std::vector<std::thread> resolve_threads;
    for (int i = 0; i < 4; ++i) {
        resolve_threads.emplace_back([vfs, i, &stop, &total_operations]() {
            while (!stop) {
                std::string path = "/stress_mount" + std::to_string(i % 2) + "_" + std::to_string(i % 5) + "/file.txt";
                auto resolved = vfs->resolve(path);
                total_operations.fetch_add(1);
                std::this_thread::yield();
            }
        });
    }

    // 线程7-8: 卸载操作
    std::vector<std::thread> unmount_threads;
    for (int i = 0; i < 2; ++i) {
        unmount_threads.emplace_back([vfs, i, &stop, &total_operations]() {
            int counter = 0;
            while (!stop) {
                std::string virtual_path = "/stress_mount" + std::to_string(i) + "_" + std::to_string(counter % 5) + "/";

                try {
                    vfs->unmount(virtual_path);
                    total_operations.fetch_add(1);
                } catch (...) {
                }

                counter++;
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
        });
    }

    // 运行2秒
    std::this_thread::sleep_for(std::chrono::seconds(2));
    stop = true;

    for (auto& t : mount_threads) {
        t.join();
    }
    for (auto& t : resolve_threads) {
        t.join();
    }
    for (auto& t : unmount_threads) {
        t.join();
    }

    // 验证
    ASSERT_GT(total_operations.load(), 0);

    kernel.shutdown();
}

// ========================================
// 长路径并发测试
// ========================================

TEST(VFSMT, LongPathConcurrent) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto vfs = kernel.vfs();
    vfs->mount("/long/", "./long/");

    constexpr int num_threads = 8;

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([vfs, i]() {
            // 构造长路径
            std::string long_path = "/long/";
            for (int j = 0; j < 20; ++j) {
                long_path += "subdir" + std::to_string(j) + "/";
            }
            long_path += "file.txt";

            for (int j = 0; j < 100; ++j) {
                auto resolved = vfs->resolve(long_path);
                std::this_thread::yield();
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    kernel.shutdown();
}

// ========================================
// 快速挂载/卸载循环测试
// ========================================

TEST(VFSMT, RapidMountUnmountCycle) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto vfs = kernel.vfs();

    constexpr int cycles = 100;
    std::atomic<int> cycle_count{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([vfs, i, &cycle_count]() {
            for (int j = 0; j < cycles; ++j) {
                std::string virtual_path = "/cycle" + std::to_string(i) + "/";
                std::string physical_path = "./cycle" + std::to_string(i) + "/";

                try {
                    // 挂载
                    vfs->mount(virtual_path, physical_path);

                    // 解析
                    auto resolved = vfs->resolve(virtual_path + "test.txt");

                    // 卸载
                    vfs->unmount(virtual_path);

                    cycle_count.fetch_add(1);
                } catch (...) {
                    // 可能有竞态
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // 验证至少完成了一些循环
    ASSERT_GT(cycle_count.load(), 0);

    kernel.shutdown();
}

// ========================================
// 多挂载点解析测试
// ========================================

TEST(VFSMT, MultiMountPointResolve) {
    auto& kernel = KernelContext::instance();
    ASSERT_TRUE(kernel.initialize());

    auto vfs = kernel.vfs();

    // 设置多级挂载点
    for (int i = 0; i < 20; ++i) {
        std::string virtual_path = "/level" + std::to_string(i) + "/";
        std::string physical_path = "./level" + std::to_string(i) + "/";
        vfs->mount(virtual_path, physical_path);
    }

    constexpr int num_threads = 10;
    std::atomic<int> resolve_count{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([vfs, i, &resolve_count]() {
            for (int j = 0; j < 100; ++j) {
                std::string path = "/level" + std::to_string(j % 20) + "/file.txt";
                auto resolved = vfs->resolve(path);
                resolve_count.fetch_add(1);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(resolve_count.load(), num_threads * 100);

    kernel.shutdown();
}

// ========================================
// Main Function
// ========================================

int main() {
    return CoronaTest::TestRunner::instance().run_all();
}
