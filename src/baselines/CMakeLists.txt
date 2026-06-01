add_library(
    hbrick_baselines
    closure_matrix_builder.cpp
    scc_dag_search_baseline.cpp
    scc_dag_closure_baseline.cpp
)

target_include_directories(
    hbrick_baselines
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    hbrick_baselines
    PUBLIC
        hbrick_core
        hbrick_grid
        hbrick_graph
        hbrick_bit
        hbrick_project_options
)
target_link_libraries(hbrick_baselines PRIVATE hbrick_project_warnings)

add_library(hbrick::baselines ALIAS hbrick_baselines)
