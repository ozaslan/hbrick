add_library(
    hbrick_tile
    tile_decomposition.cpp
    tile_boundary_order.cpp
    tile_closure_util.cpp
    base_tile_summary.cpp
    brick_tile_index.cpp
    port_index.cpp
    port_graph.cpp
    brick_index.cpp
    group_tile_slots.cpp
    external_boundary_ports.cpp
    hierarchy_tree.cpp
    super_tile_composer.cpp
    super_tile_oracle.cpp
    hbrick_index.cpp
    hbrick_index_builder.cpp
    hbrick_build_format.cpp
)

target_include_directories(
    hbrick_tile
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    hbrick_tile
    PUBLIC
        hbrick_core
        hbrick_grid
        hbrick_graph
        hbrick_bit
        hbrick_project_options
)
target_link_libraries(hbrick_tile PRIVATE hbrick_project_warnings)

add_library(hbrick::tile ALIAS hbrick_tile)
