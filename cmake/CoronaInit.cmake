include_guard(GLOBAL)

include(CompilerSettings)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

function(corona_group_sources TARGET_NAME BASE_DIR)
    if(NOT TARGET ${TARGET_NAME})
        message(WARNING "corona_group_sources: target '${TARGET_NAME}' does not exist")
        return()
    endif()

    get_target_property(_corona_sources ${TARGET_NAME} SOURCES)
    if(NOT _corona_sources OR _corona_sources STREQUAL "NOTFOUND")
        return()
    endif()

    get_filename_component(_corona_base_dir "${BASE_DIR}" REALPATH)
    if(NOT EXISTS "${_corona_base_dir}")
        message(WARNING "corona_group_sources: base directory '${BASE_DIR}' not found for target '${TARGET_NAME}'")
        return()
    endif()

    source_group(TREE "${_corona_base_dir}" FILES ${_corona_sources})
endfunction()
