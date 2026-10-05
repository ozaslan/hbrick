add_library(hbrick_grid maze_layout.cpp)

target_include_directories(
    hbrick_grid
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(hbrick_grid PUBLIC hbrick_core hbrick_project_options)
target_link_libraries(hbrick_grid PRIVATE hbrick_project_warnings)

add_library(hbrick::grid ALIAS hbrick_grid)
