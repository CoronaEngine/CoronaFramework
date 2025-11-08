cmake_minimum_required(VERSION 4.0)
project(CoronaFramework VERSION 0.1.0 LANGUAGES CXX)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(compiler_settings)
include(dependencies)

function(corona_group_sources TARGET_NAME BASE_DIR)
    if(NOT TARGET ${TARGET_NAME})
        message(WARNING "corona_group_sources: target '${TARGET_NAME}' does not exist")
        return()
    endif()

    get_target_property(_corona_sources ${TARGET_NAME} SOURCES)
    if(NOT _corona_sources OR _corona_sources STREQUAL "NOTFOUND")
        return()
    endif()

    get_filename_component(_corona_base_dir "${BASE_DIR}" REALPATH)
    if(NOT EXISTS "${_corona_base_dir}")
        message(WARNING "corona_group_sources: base directory '${BASE_DIR}' not found for target '${TARGET_NAME}'")
        return()
    endif()

    source_group(TREE "${_corona_base_dir}" FILES ${_corona_sources})
endfunction()

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

