set(CORONA_KERNEL_SOURCES
    src/core/logger.cpp
    src/event/event_bus.cpp
    src/event/event_stream.cpp
    src/core/vfs.cpp
    src/core/plugin_manager.cpp
    src/core/kernel_context.cpp
    src/system/system_manager.cpp
    src/system/system_base.cpp
    src/memory/cache_aligned_allocator.cpp
)

file(GLOB_RECURSE CORONA_KERNEL_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)

add_library(corona_kernel STATIC ${CORONA_KERNEL_SOURCES})

add_library(corona::kernel ALIAS corona_kernel)

if(CORONA_KERNEL_HEADERS)
    target_sources(corona_kernel PRIVATE ${CORONA_KERNEL_HEADERS})
endif()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${CORONA_KERNEL_SOURCES} ${CORONA_KERNEL_HEADERS})

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

