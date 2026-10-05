add_library(
    hbrick_baselines
    baseline_graph_utils.cpp
    closure_matrix_builder.cpp
    scc_dag_search_baseline.cpp
    scc_dag_closure_baseline.cpp
    csr_bfs_baseline.cpp
    csr_dfs_baseline.cpp
    full_closure_baseline.cpp
    two_hop_baseline.cpp
    grail_baseline.cpp
    brick_search_baseline.cpp
    brick_closure_baseline.cpp
    hbrick_query_scratch.cpp
    hbrick_batch_query.cpp
    hbrick_baseline.cpp
    hbrick_micro_bfs_baseline.cpp
    hbrick_fused_lift_baseline.cpp
    hbrick_fused_lift_cache_baseline.cpp
    hbrick_skip_lift_baseline.cpp
    hbrick_scc_label_baseline.cpp
    oreach_baseline.cpp
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
        hbrick_tile
        hbrick_project_options
    PRIVATE
        hbrick_oreach_vendor
        hbrick_grail_vendor
)
target_link_libraries(hbrick_baselines PRIVATE hbrick_project_warnings)

set_source_files_properties(grail_baseline.cpp PROPERTIES COMPILE_OPTIONS "-Wno-deprecated")

add_library(hbrick::baselines ALIAS hbrick_baselines)
