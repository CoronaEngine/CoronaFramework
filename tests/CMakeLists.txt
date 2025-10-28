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

# 线程安全测试
add_executable(kernel_thread_safety_test
    kernel/thread_safety_test.cpp
)

target_link_libraries(kernel_thread_safety_test
    PRIVATE
        corona_kernel
        corona_pal
)

# 系统测试
add_executable(kernel_system_test
    kernel/system_test.cpp
)

target_link_libraries(kernel_system_test
    PRIVATE
        corona_kernel
        corona_pal
)

# 性能监控测试
add_executable(kernel_performance_test
    kernel/performance_test.cpp
)

target_link_libraries(kernel_performance_test
    PRIVATE
        corona_kernel
        corona_pal
)

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
message(STATUS "")
message(STATUS "Run tests:")
message(STATUS "  cd build && ctest")
message(STATUS "  or")
message(STATUS "  cd build && ./tests/kernel_thread_safety_test")
message(STATUS "  cd build && ./tests/kernel_system_test")
message(STATUS "")
message(STATUS "Run with ThreadSanitizer:")
message(STATUS "  cmake -DENABLE_THREAD_SANITIZER=ON ..")
message(STATUS "  make")
message(STATUS "  ctest")
message(STATUS "")
