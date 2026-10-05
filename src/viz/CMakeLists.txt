add_library(
    hbrick_viz
    svg_canvas.cpp
    grid_graph_renderer.cpp
)

target_include_directories(
    hbrick_viz
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    hbrick_viz
    PUBLIC
        hbrick_core
        hbrick_grid
        hbrick_graph
        hbrick_project_options
)
target_link_libraries(hbrick_viz PRIVATE hbrick_project_warnings)

add_library(hbrick::viz ALIAS hbrick_viz)
