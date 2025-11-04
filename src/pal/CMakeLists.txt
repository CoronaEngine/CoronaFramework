set(CORONA_PAL_SOURCES
    src/common/file_system.cpp
    src/common/factory.cpp
)

file(GLOB_RECURSE CORONA_PAL_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)

add_library(corona_pal STATIC ${CORONA_PAL_SOURCES})

add_library(corona::pal ALIAS corona_pal)

# Add platform-specific sources
if(WIN32)
    target_sources(corona_pal PRIVATE
        src/platform/windows/win_dynamic_library.cpp
    )
elseif(UNIX AND NOT APPLE)
    # Linux support - to be implemented
    # target_sources(corona_pal PRIVATE
    #     src/platform/linux/linux_dynamic_library.cpp
    # )
    message(WARNING "Linux platform support not yet implemented")
elseif(APPLE)
    # macOS support - to be implemented
    # target_sources(corona_pal PRIVATE
    #     src/platform/macos/macos_dynamic_library.cpp
    # )
    message(WARNING "macOS platform support not yet implemented")
endif()

if(CORONA_PAL_HEADERS)
    target_sources(corona_pal PRIVATE ${CORONA_PAL_HEADERS})
endif()

get_target_property(_corona_pal_all_sources corona_pal SOURCES)
if(_corona_pal_all_sources)
    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${_corona_pal_all_sources})
endif()

target_include_directories(corona_pal PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
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

