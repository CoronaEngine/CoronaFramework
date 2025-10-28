cmake_minimum_required(VERSION 4.0)
project(CoronaFramework VERSION 0.1.0 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(compiler_settings)
include(dependencies)

add_subdirectory(src/pal)
add_subdirectory(src/kernel)

# 添加示例（可选）
option(BUILD_EXAMPLES "Build examples" ${PROJECT_IS_TOP_LEVEL})
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# 添加测试（可选）
option(BUILD_TESTS "Build test suite" ${PROJECT_IS_TOP_LEVEL})
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

# 导出配置
install(EXPORT CoronaFrameworkTargets
    FILE CoronaFrameworkTargets.cmake
    NAMESPACE corona::
    DESTINATION lib/cmake/CoronaFramework
)

# 生成配置文件
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/CoronaFrameworkConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CoronaFrameworkConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/CoronaFrameworkConfig.cmake"
    INSTALL_DESTINATION lib/cmake/CoronaFramework
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/CoronaFrameworkConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/CoronaFrameworkConfigVersion.cmake"
    DESTINATION lib/cmake/CoronaFramework
)

