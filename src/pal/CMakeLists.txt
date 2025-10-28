add_library(corona_pal INTERFACE)
add_library(corona::pal ALIAS corona_pal)

target_include_directories(corona_pal INTERFACE 
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${fast_io_SOURCE_DIR}/include"
)

# Add platform-specific sources
if(WIN32)
    target_sources(corona_pal INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/windows/win_dynamic_library.cpp"
    )
endif()

# Add common sources
target_sources(corona_pal INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/fast_io_file_system.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/factory.cpp"
)
