cmake_minimum_required(VERSION 3.20)

# ========================================
# Corona Framework - Test Suite
# ========================================

project(CoronaTests CXX)

# 确保 C++20 标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加测试框架头文件目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# ========================================
# Kernel Tests
# ========================================

function(corona_add_test TARGET_NAME)
    set(_test_common_header "${CMAKE_CURRENT_SOURCE_DIR}/test_framework.h")
    add_executable(${TARGET_NAME} ${ARGN} ${_test_common_header})
    target_link_libraries(${TARGET_NAME}
        PRIVATE
            corona_kernel
            corona_pal
    )
    set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Tests")
    corona_group_sources(${TARGET_NAME} ${CMAKE_CURRENT_SOURCE_DIR})
endfunction()

# 线程安全测试
corona_add_test(kernel_thread_safety_test kernel/thread_safety_test.cpp)

# 系统测试
corona_add_test(kernel_system_test kernel/system_test.cpp)

# 性能监控测试
corona_add_test(kernel_performance_test kernel/performance_test.cpp)

# 事件流测试
corona_add_test(kernel_event_stream_test kernel/event_stream_test.cpp)

# EventBus 测试
corona_add_test(kernel_event_bus_test kernel/event_bus_test.cpp)

# Logger 测试
corona_add_test(kernel_logger_test kernel/logger_test.cpp)

# VFS 测试
corona_add_test(kernel_vfs_test kernel/vfs_test.cpp)

# LockFreeQueue 测试
corona_add_test(kernel_lockfree_queue_test kernel/lockfree_queue_test.cpp)

# 性能基准测试
corona_add_test(kernel_benchmark_test kernel/benchmark_test.cpp)

# CacheAlignedAllocator 测试
corona_add_test(kernel_cache_aligned_allocator_test kernel/cache_aligned_allocator_test.cpp)

# Platform 测试
corona_add_test(kernel_platform_test kernel/platform_test.cpp)

# ========================================
# Multi-threaded Stability Tests
# ========================================

# EventBus 多线程测试
corona_add_test(kernel_eventbus_mt_test kernel/eventbus_mt_test.cpp)

# EventStream 多线程测试
corona_add_test(kernel_eventstream_mt_test kernel/eventstream_mt_test.cpp)

# Logger 多线程测试
corona_add_test(kernel_logger_mt_test kernel/logger_mt_test.cpp)

# VFS 多线程测试
corona_add_test(kernel_vfs_mt_test kernel/vfs_mt_test.cpp)

# PluginManager 多线程测试
corona_add_test(kernel_pluginmanager_mt_test kernel/pluginmanager_mt_test.cpp)

# SystemManager 多线程测试
corona_add_test(kernel_systemmanager_mt_test kernel/systemmanager_mt_test.cpp)

# Storage 测试
corona_add_test(kernel_storage_test kernel/storage_test.cpp)

# TaskGroup 测试
corona_add_test(kernel_task_group_test kernel/task_group_test.cpp)

# ========================================
# 启用测试
# ========================================

enable_testing()

# 注册线程安全测试
add_test(
    NAME ThreadSafety
    COMMAND kernel_thread_safety_test
)

# 注册系统测试
add_test(
    NAME System
    COMMAND kernel_system_test
)

# 注册性能监控测试
add_test(
    NAME Performance
    COMMAND kernel_performance_test
)

add_test(
    NAME LockFreeQueue
    COMMAND kernel_lockfree_queue_test
)

# 注册事件流测试
add_test(
    NAME EventStream
    COMMAND kernel_event_stream_test
)

# 注册 EventBus 测试
add_test(
    NAME EventBus
    COMMAND kernel_event_bus_test
)

# 注册 Logger 测试
add_test(
    NAME Logger
    COMMAND kernel_logger_test
)

# 注册 VFS 测试
add_test(
    NAME VFS
    COMMAND kernel_vfs_test
)

# 注册性能基准测试
add_test(
    NAME Benchmark
    COMMAND kernel_benchmark_test
)

# 注册 CacheAlignedAllocator 测试
add_test(
    NAME CacheAlignedAllocator
    COMMAND kernel_cache_aligned_allocator_test
)

# 注册 Platform 测试
add_test(
    NAME Platform
    COMMAND kernel_platform_test
)

# 注册 EventBus 多线程测试
add_test(
    NAME EventBusMT
    COMMAND kernel_eventbus_mt_test
)

# 注册 EventStream 多线程测试
add_test(
    NAME EventStreamMT
    COMMAND kernel_eventstream_mt_test
)

# 注册 Logger 多线程测试
add_test(
    NAME LoggerMT
    COMMAND kernel_logger_mt_test
)

# 注册 VFS 多线程测试
add_test(
    NAME VFSMT
    COMMAND kernel_vfs_mt_test
)

# 注册 PluginManager 多线程测试
add_test(
    NAME PluginManagerMT
    COMMAND kernel_pluginmanager_mt_test
)

# 注册 SystemManager 多线程测试
add_test(
    NAME SystemManagerMT
    COMMAND kernel_systemmanager_mt_test
)

# 注册 Storage 测试
add_test(
    NAME Storage
    COMMAND kernel_storage_test
)

# 注册 TaskGroup 测试
add_test(
    NAME TaskGroup
    COMMAND kernel_task_group_test
)

# ========================================
# ThreadSanitizer 配置
# ========================================

# 可选：添加 ThreadSanitizer 目标
# 使用方法：cmake -DENABLE_THREAD_SANITIZER=ON ..
option(ENABLE_THREAD_SANITIZER "Enable ThreadSanitizer for tests" OFF)

if(ENABLE_THREAD_SANITIZER)
    message(STATUS "ThreadSanitizer enabled for tests")
    
    # 为所有测试目标添加 ThreadSanitizer
    target_compile_options(kernel_thread_safety_test PRIVATE
        -fsanitize=thread
        -g
        -O1
    )
    target_link_options(kernel_thread_safety_test PRIVATE
        -fsanitize=thread
    )
    
    target_compile_options(kernel_system_test PRIVATE
        -fsanitize=thread
        -g
        -O1
    )
    target_link_options(kernel_system_test PRIVATE
        -fsanitize=thread
    )
    
    target_compile_options(kernel_performance_test PRIVATE
        -fsanitize=thread
        -g
        -O1
    )
    target_link_options(kernel_performance_test PRIVATE
        -fsanitize=thread
    )
    
    target_compile_options(kernel_event_stream_test PRIVATE
        -fsanitize=thread
        -g
        -O1
    )
    target_link_options(kernel_event_stream_test PRIVATE
        -fsanitize=thread
    )
endif()

# ========================================
# 测试输出配置
# ========================================

# 详细输出测试结果
set(CTEST_OUTPUT_ON_FAILURE ON)

# 显示测试运行进度
set(CMAKE_CTEST_ARGUMENTS "--output-on-failure;--verbose")

# ========================================
# 帮助信息
# ========================================

message(STATUS "")
message(STATUS "Corona Framework Test Suite")
message(STATUS "============================")
message(STATUS "Tests added:")
message(STATUS "  - kernel_thread_safety_test: Thread safety stress tests")
message(STATUS "  - kernel_system_test: System architecture tests")
message(STATUS "  - kernel_performance_test: Performance monitoring tests")
message(STATUS "  - kernel_event_stream_test: Event stream functionality tests")
message(STATUS "  - kernel_event_bus_test: EventBus publish/subscribe tests")
message(STATUS "  - kernel_logger_test: Logger functionality tests")
message(STATUS "  - kernel_vfs_test: Virtual file system tests")
message(STATUS "  - kernel_benchmark_test: Performance benchmark tests")
message(STATUS "  - kernel_task_group_test: Task group parallel execution tests")
message(STATUS "")
message(STATUS "Multi-threaded Stability Tests:")
message(STATUS "  - kernel_eventbus_mt_test: EventBus concurrent operations")
message(STATUS "  - kernel_eventstream_mt_test: EventStream producer-consumer patterns")
message(STATUS "  - kernel_logger_mt_test: Logger high-frequency concurrent logging")
message(STATUS "  - kernel_vfs_mt_test: VFS concurrent mount/unmount/resolve")
message(STATUS "  - kernel_pluginmanager_mt_test: PluginManager concurrent queries")
message(STATUS "  - kernel_systemmanager_mt_test: SystemManager state transitions")
message(STATUS "")
message(STATUS "Run tests:")
message(STATUS "  cd build && ctest")
message(STATUS "  or")
message(STATUS "  cd build && ./tests/kernel_thread_safety_test")
message(STATUS "  cd build && ./tests/kernel_system_test")
message(STATUS "  cd build && ./tests/kernel_event_stream_test")
message(STATUS "")
message(STATUS "Run with ThreadSanitizer:")
message(STATUS "  cmake -DENABLE_THREAD_SANITIZER=ON ..")
message(STATUS "  make")
message(STATUS "  ctest")
message(STATUS "")
