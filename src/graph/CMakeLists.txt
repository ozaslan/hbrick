add_library(
    hbrick_graph
    csr_graph.cpp
    csr_graph_builder.cpp
    directed_grid_graph.cpp
    directed_grid_graph_builder.cpp
    graph_search_scratch.cpp
    bfs.cpp
    dfs.cpp
    scc_decomposition.cpp
    condensation_graph.cpp
    dag_reachability.cpp
    reachability_density.cpp
    connected_components.cpp
)

target_include_directories(
    hbrick_graph
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(hbrick_graph PUBLIC hbrick_core hbrick_grid hbrick_project_options)
target_link_libraries(hbrick_graph PRIVATE hbrick_project_warnings)

find_package(Threads REQUIRED)
target_link_libraries(hbrick_graph PUBLIC Threads::Threads)

add_library(hbrick::graph ALIAS hbrick_graph)
