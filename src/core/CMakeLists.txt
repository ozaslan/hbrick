add_library(
    hbrick_core
    version.cpp
    status_reporting.cpp
)

target_include_directories(
    hbrick_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(hbrick_core PUBLIC hbrick_project_options)
target_link_libraries(hbrick_core PRIVATE hbrick_project_warnings)

add_library(hbrick::core ALIAS hbrick_core)
