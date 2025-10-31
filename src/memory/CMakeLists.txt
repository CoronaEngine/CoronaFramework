# Corona Memory Allocator Library
# 高性能可扩展内存分配器

add_library(corona_memory STATIC
    src/frontend.cpp
    src/allocator_stats.cpp
    src/block.cpp
    src/bin.cpp
    src/thread_local.cpp
    src/backend.cpp
)

add_library(corona::memory ALIAS corona_memory)

target_include_directories(corona_memory PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(corona_memory PUBLIC cxx_std_20)

# 链接 PAL 层以使用操作系统抽象
target_link_libraries(corona_memory PUBLIC corona::pal)

# 编译选项
if(MSVC)
    target_compile_options(corona_memory PRIVATE
        /W4
        /WX
    )
else()
    target_compile_options(corona_memory PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endif()

# 安装规则
install(TARGETS corona_memory
    EXPORT CoronaFrameworkTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(DIRECTORY include/corona
    DESTINATION include
)
