cmake_minimum_required(VERSION 3.20)

project(hbrick VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(HBRICK_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

add_library(hbrick_project_options INTERFACE)
target_compile_features(hbrick_project_options INTERFACE cxx_std_20)

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
