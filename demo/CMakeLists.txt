set(exec_name_prefix "stdarc")


foreach(name "dir" "pak" "tar" "vfs" "zip")
    set(EXEC_NAME "${exec_name_prefix}_${name}")
    set(Source_Files "demo_${name}.c")
    source_group("Source Files" FILES "${Source_Files}")

    add_library("${EXEC_NAME}" "${LIBRARY_TYPE_FLAG}" "${Source_Files}")

    target_include_directories(
            "${EXEC_NAME}"
            PUBLIC
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
            "$<INSTALL_INTERFACE:include>"
    )
    set_target_properties(
            "${EXEC_NAME}"
            PROPERTIES
            LINKER_LANGUAGE
            C
    )
    target_link_libraries("${EXEC_NAME}" PRIVATE "${LIBRARY_NAME}")

    # install rules

    set(installable_libs "${EXEC_NAME}" "${PROJECT_NAME}_compiler_flags")

    if (TARGET "${DEPENDANT_LIBRARY}")
        list(APPEND installable_libs "${DEPENDANT_LIBRARY}")
    endif ()
    install(TARGETS ${installable_libs}
            DESTINATION "bin"
            EXPORT "${EXEC_NAME}Targets")
endforeach()
