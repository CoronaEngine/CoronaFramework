include_guard(GLOBAL)

# 示例
option(BUILD_EXAMPLES "Build examples" ${PROJECT_IS_TOP_LEVEL})
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# 测试
option(BUILD_TESTS "Build test suite" ${PROJECT_IS_TOP_LEVEL})
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
