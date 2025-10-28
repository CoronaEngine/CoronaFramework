# ========================================
# Basic Example
# ========================================
add_executable(basic_example basic/main.cpp)
target_link_libraries(basic_example PRIVATE corona::kernel corona::pal)
target_include_directories(basic_example PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# ========================================
# 01 - EventBus Example
# ========================================
add_executable(01_event_bus 01_event_bus/main.cpp)
target_link_libraries(01_event_bus PRIVATE corona::kernel corona::pal)

# ========================================
# 02 - System Lifecycle Example
# ========================================
add_executable(02_system_lifecycle 02_system_lifecycle/main.cpp)
target_link_libraries(02_system_lifecycle PRIVATE corona::kernel corona::pal)

# ========================================
# 03 - EventStream Example
# ========================================
add_executable(03_event_stream 03_event_stream/main.cpp)
target_link_libraries(03_event_stream PRIVATE corona::kernel corona::pal)

# ========================================
# 04 - VFS Example
# ========================================
add_executable(04_vfs 04_vfs/main.cpp)
target_link_libraries(04_vfs PRIVATE corona::kernel corona::pal)

# ========================================
# 05 - Multithreading Example
# ========================================
add_executable(05_multithreading 05_multithreading/main.cpp)
target_link_libraries(05_multithreading PRIVATE corona::kernel corona::pal)
