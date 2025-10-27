add_library(corona_kernel INTERFACE)
add_library(corona::kernel ALIAS corona_kernel)

target_include_directories(corona_kernel INTERFACE 
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_sources(corona_kernel INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/logger.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/event_bus.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/vfs.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin_manager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/kernel_context.cpp"
)

target_link_libraries(corona_kernel INTERFACE corona::pal)
