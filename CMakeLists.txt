cmake_minimum_required(VERSION 3.20)

project(hbrick VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(HBRICK_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(HBRICK_ENABLE_LTO "Enable link-time optimization for non-Debug builds" ON)
option(HBRICK_BUILD_TOOLS "Build developer tools (dataset browser GUI)" OFF)

# ---------------------------------------------------------------------------
# API documentation (Doxygen)
#
# Generated HTML is written to ${HBRICK_DOCS_OUTPUT_DIR}/${HBRICK_DOCS_HTML_SUBDIR}.
# Defaults place output under the source-tree docs/ folder (docs/html/).
# ---------------------------------------------------------------------------
option(HBRICK_BUILD_DOCS "Add the hbrick_docs Doxygen generation target" OFF)

set(HBRICK_DOCS_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/docs"
    CACHE PATH "Directory that receives generated Doxygen output")

set(HBRICK_DOCS_HTML_SUBDIR "html"
    CACHE STRING "Subdirectory under HBRICK_DOCS_OUTPUT_DIR for HTML output")

set(HBRICK_DOCS_INPUT_DIRS
    "${CMAKE_SOURCE_DIR}/include/hbrick;${CMAKE_SOURCE_DIR}/tests/support"
    CACHE STRING "Semicolon-separated Doxygen INPUT directories")

option(HBRICK_DOCS_WARN_IF_UNDOCUMENTED "Doxygen WARN_IF_UNDOCUMENTED" ON)
option(HBRICK_DOCS_WARN_IF_DOC_ERROR "Doxygen WARN_IF_DOC_ERROR" ON)
option(HBRICK_DOCS_WARN_NO_PARAMDOC "Doxygen WARN_NO_PARAMDOC" ON)
option(HBRICK_DOCS_QUIET "Doxygen QUIET (suppress progress messages)" OFF)
option(HBRICK_DOCS_GENERATE_TREEVIEW "Doxygen GENERATE_TREEVIEW" ON)

add_library(hbrick_project_options INTERFACE)
target_compile_features(hbrick_project_options INTERFACE cxx_std_20)

if(MSVC)
    target_compile_options(
        hbrick_project_options
        INTERFACE
            $<$<NOT:$<CONFIG:Debug>>:/O2>
            $<$<NOT:$<CONFIG:Debug>>:/Ob2>
            $<$<NOT:$<CONFIG:Debug>>:/Oi>
            $<$<NOT:$<CONFIG:Debug>>:/Ot>
    )
    if(HBRICK_ENABLE_LTO)
        target_compile_options(
            hbrick_project_options
            INTERFACE
                $<$<NOT:$<CONFIG:Debug>>:/GL>
        )
        target_link_options(
            hbrick_project_options
            INTERFACE
                $<$<NOT:$<CONFIG:Debug>>:/LTCG>
        )
    endif()
else()
    target_compile_options(
        hbrick_project_options
        INTERFACE
            $<$<NOT:$<CONFIG:Debug>>:-O3>
            $<$<NOT:$<CONFIG:Debug>>:-march=native>
            $<$<NOT:$<CONFIG:Debug>>:-ffunction-sections>
            $<$<NOT:$<CONFIG:Debug>>:-fdata-sections>
    )
    if(HBRICK_ENABLE_LTO)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            set(_hbrick_lto_flag "-flto=thin")
        else()
            set(_hbrick_lto_flag "-flto")
        endif()
        target_compile_options(
            hbrick_project_options
            INTERFACE
                $<$<NOT:$<CONFIG:Debug>>:${_hbrick_lto_flag}>
        )
        target_link_options(
            hbrick_project_options
            INTERFACE
                $<$<NOT:$<CONFIG:Debug>>:${_hbrick_lto_flag}>
        )
    endif()
    target_link_options(
        hbrick_project_options
        INTERFACE
            $<$<NOT:$<CONFIG:Debug>>:-Wl,--gc-sections>
    )
endif()

add_library(hbrick_project_warnings INTERFACE)
if(MSVC)
    target_compile_options(hbrick_project_warnings INTERFACE /W4)
    if(HBRICK_WARNINGS_AS_ERRORS)
        target_compile_options(hbrick_project_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(
        hbrick_project_warnings
        INTERFACE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
    )
    if(HBRICK_WARNINGS_AS_ERRORS)
        target_compile_options(hbrick_project_warnings INTERFACE -Werror)
    endif()
endif()

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)

if(HBRICK_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(HBRICK_BUILD_DOCS)
    add_subdirectory(docs)
endif()
