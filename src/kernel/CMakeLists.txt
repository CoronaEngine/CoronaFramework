set(CORONA_KERNEL_PUBLIC_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/include/corona/kernel")

set(CORONA_KERNEL_SOURCES
    core/logger.cpp
    event/event_bus.cpp
    event/event_stream.cpp
    core/vfs.cpp
    core/plugin_manager.cpp
    core/kernel_context.cpp
    system/system_manager.cpp
    system/system_base.cpp
    memory/cache_aligned_allocator.cpp
    utils/work_stealing_queue.cpp
    utils/task_scheduler.cpp
    utils/task_group.cpp
)

file(GLOB_RECURSE CORONA_KERNEL_HEADERS CONFIGURE_DEPENDS
    "${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/*.h"
    "${CORONA_KERNEL_PUBLIC_INCLUDE_DIR}/*.hpp"
)

add_library(corona_kernel STATIC
    ${CORONA_KERNEL_SOURCES}
    ${CORONA_KERNEL_HEADERS})

add_library(corona::kernel ALIAS corona_kernel)

if(CORONA_KERNEL_HEADERS)
    target_sources(corona_kernel PRIVATE ${CORONA_KERNEL_HEADERS})
endif()

target_include_directories(corona_kernel PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
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
