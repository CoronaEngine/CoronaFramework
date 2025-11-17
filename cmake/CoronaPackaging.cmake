include_guard(GLOBAL)

include(CMakePackageConfigHelpers)

install(EXPORT CoronaFrameworkTargets
    FILE CoronaFrameworkTargets.cmake
    NAMESPACE corona::
    DESTINATION lib/cmake/CoronaFramework
)

install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/corona"
    DESTINATION include
)

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
