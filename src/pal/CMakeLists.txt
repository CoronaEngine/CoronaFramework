add_library(corona_pal STATIC
    src/fast_io_file_system.cpp
    src/factory.cpp
)

add_library(corona::pal ALIAS corona_pal)

# Add platform-specific sources
if(WIN32)
    target_sources(corona_pal PRIVATE
        windows/win_dynamic_library.cpp
    )
elseif(UNIX AND NOT APPLE)
    # Linux support - to be implemented
    # target_sources(corona_pal PRIVATE
    #     linux/linux_dynamic_library.cpp
    # )
    message(WARNING "Linux platform support not yet implemented")
elseif(APPLE)
    # macOS support - to be implemented
    # target_sources(corona_pal PRIVATE
    #     macos/macos_dynamic_library.cpp
    # )
    message(WARNING "macOS platform support not yet implemented")
endif()

target_include_directories(corona_pal PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${fast_io_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(corona_pal PUBLIC cxx_std_20)

# 安装规则
install(TARGETS corona_pal
    EXPORT CoronaFrameworkTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(DIRECTORY include/corona
    DESTINATION include
)

