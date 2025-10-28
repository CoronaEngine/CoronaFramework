cmake_minimum_required(VERSION 4.0)
project(CoronaFramework)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(compiler_settings)
include(dependencies)

add_subdirectory(src)
add_subdirectory(src/pal)
add_subdirectory(src/kernel)

# 添加测试（可选）
option(BUILD_TESTS "Build test suite" ON)
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
