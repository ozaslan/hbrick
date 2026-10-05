find_package(Doxygen)

if(NOT DOXYGEN_FOUND)
    message(STATUS "Doxygen not found; hbrick_docs target will not be available")
    return()
endif()

string(REPLACE ";" " " HBRICK_DOXYGEN_INPUT_DIRS "${HBRICK_DOCS_INPUT_DIRS}")

foreach(_hbrick_doxy_flag
    HBRICK_DOCS_WARN_IF_UNDOCUMENTED
    HBRICK_DOCS_WARN_IF_DOC_ERROR
    HBRICK_DOCS_WARN_NO_PARAMDOC
    HBRICK_DOCS_QUIET
    HBRICK_DOCS_GENERATE_TREEVIEW
)
    if(${_hbrick_doxy_flag})
        set(${_hbrick_doxy_flag}_DOXY "YES")
    else()
        set(${_hbrick_doxy_flag}_DOXY "NO")
    endif()
endforeach()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in
    ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
    @ONLY
)

file(MAKE_DIRECTORY "${HBRICK_DOCS_OUTPUT_DIR}")

add_custom_target(hbrick_docs
    COMMAND ${CMAKE_COMMAND} -E make_directory "${HBRICK_DOCS_OUTPUT_DIR}"
    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Generating API documentation into ${HBRICK_DOCS_OUTPUT_DIR}/${HBRICK_DOCS_HTML_SUBDIR}"
    VERBATIM
)

message(STATUS "Doxygen output directory: ${HBRICK_DOCS_OUTPUT_DIR}/${HBRICK_DOCS_HTML_SUBDIR}")
