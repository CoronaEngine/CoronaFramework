add_executable(CoronaFramework main.cpp)

target_link_libraries(CoronaFramework PRIVATE corona::pal corona::kernel)
