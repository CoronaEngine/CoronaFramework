# Basic example
add_executable(basic_example basic/main.cpp)
target_link_libraries(basic_example PRIVATE corona::kernel corona::pal)

# Include dummy_system header
target_include_directories(basic_example PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
