cmake_minimum_required(VERSION 4.0)
project(CoronaFramework)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(compiler_settings)

add_subdirectory(src)
