add_library(corona_kernel STATIC
    src/core/logger.cpp
    src/event/event_bus.cpp
    src/event/event_stream.cpp
    src/core/vfs.cpp
    src/core/plugin_manager.cpp
    src/core/kernel_context.cpp
    src/system/system_manager.cpp
    src/memory/cache_aligned_allocator.cpp
)

add_library(corona::kernel ALIAS corona_kernel)

target_include_directories(corona_kernel PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(corona_kernel PUBLIC cxx_std_20)
target_link_libraries(corona_kernel PUBLIC corona::pal)

# 安装规则
install(TARGETS corona_kernel
    EXPORT CoronaFrameworkTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(DIRECTORY include/corona
    DESTINATION include
)

