find_package(GTest CONFIG QUIET)

if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

include(GoogleTest)

set(HBRICK_GTEST_TIMEOUT 20 CACHE STRING "Maximum seconds allowed for each Google Test case")
set(HBRICK_MOVINGAI_GTEST_TIMEOUT 180 CACHE STRING "Maximum seconds for each MovingAI map reachability test")

function(hbrick_register_gtests target_name)
    gtest_discover_tests(
        ${target_name}
        DISCOVERY_TIMEOUT 120
        PROPERTIES TIMEOUT ${HBRICK_GTEST_TIMEOUT}
    )
endfunction()

function(hbrick_register_slow_gtests target_name timeout_seconds)
    gtest_discover_tests(
        ${target_name}
        DISCOVERY_TIMEOUT 120
        PROPERTIES TIMEOUT ${timeout_seconds}
    )
endfunction()

add_library(
    hbrick_tools_data STATIC
    ${CMAKE_SOURCE_DIR}/tools/dataset_browser/recipe.cpp
    ${CMAKE_SOURCE_DIR}/tools/dataset_browser/dataset_index.cpp
)

target_include_directories(
    hbrick_tools_data
    PUBLIC
        ${CMAKE_SOURCE_DIR}/tools/dataset_browser
)

target_link_libraries(
    hbrick_tools_data
    PUBLIC
        hbrick_io
        hbrick_graph
        hbrick_project_warnings
)

add_library(
    hbrick_test_support
    support/maze_generator.cpp
    support/movingai_map_catalog.cpp
    support/reachability_oracle.cpp
    support/recipe_graph.cpp
)

target_include_directories(
    hbrick_test_support
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/support
)

target_compile_definitions(
    hbrick_test_support
    PUBLIC
        HBRICK_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(
    hbrick_test_support
    PUBLIC
        hbrick_baselines
        hbrick_tools_data
        GTest::gtest
        hbrick_project_warnings
)

add_executable(test_placeholder unit/test_placeholder.cpp)

target_link_libraries(
    test_placeholder
    PRIVATE
        hbrick_core
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_placeholder)

add_executable(test_core_types unit/test_core_types.cpp)

target_link_libraries(
    test_core_types
    PRIVATE
        hbrick_core
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_core_types)

add_executable(test_maze_layout unit/test_maze_layout.cpp)

target_link_libraries(
    test_maze_layout
    PRIVATE
        hbrick_grid
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_maze_layout)

add_executable(test_tile_size unit/test_tile_size.cpp)

target_link_libraries(
    test_tile_size
    PRIVATE
        hbrick_tile
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_tile_size)

add_executable(test_tile_decomposition unit/test_tile_decomposition.cpp)

target_link_libraries(
    test_tile_decomposition
    PRIVATE
        hbrick_tile
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_tile_decomposition)

add_executable(test_hierarchy_scaffold unit/test_hierarchy_scaffold.cpp)

target_link_libraries(
    test_hierarchy_scaffold
    PRIVATE
        hbrick_tile
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_hierarchy_scaffold)

add_executable(test_super_tile_compose unit/test_super_tile_compose.cpp)

target_link_libraries(
    test_super_tile_compose
    PRIVATE
        hbrick_tile
        hbrick_graph
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_super_tile_compose)

add_executable(test_super_tile_verify unit/test_super_tile_verify.cpp)

target_link_libraries(
    test_super_tile_verify
    PRIVATE
        hbrick_tile
        hbrick_graph
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_super_tile_verify)

add_executable(test_hierarchy_build unit/test_hierarchy_build.cpp)

target_link_libraries(
    test_hierarchy_build
    PRIVATE
        hbrick_tile
        hbrick_graph
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_hierarchy_build)

add_executable(test_hbrick_index_builder unit/test_hbrick_index_builder.cpp)

target_link_libraries(
    test_hbrick_index_builder
    PRIVATE
        hbrick_tile
        hbrick_graph
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_hbrick_index_builder)

add_executable(test_base_tile_summary unit/test_base_tile_summary.cpp)

target_link_libraries(
    test_base_tile_summary
    PRIVATE
        hbrick_tile
        hbrick_graph
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_base_tile_summary)

add_executable(test_preprocess_memory_ledger unit/test_preprocess_memory_ledger.cpp)

target_link_libraries(
    test_preprocess_memory_ledger
    PRIVATE
        hbrick_tile
        hbrick_graph
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_preprocess_memory_ledger)

add_executable(test_brick_index unit/test_brick_index.cpp)

target_link_libraries(
    test_brick_index
    PRIVATE
        hbrick_tile
        hbrick_graph
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_brick_index)

add_executable(test_port_graph unit/test_port_graph.cpp)

target_link_libraries(
    test_port_graph
    PRIVATE
        hbrick_tile
        hbrick_graph
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_port_graph)

add_executable(test_brick_search_baseline unit/test_brick_search_baseline.cpp)

target_link_libraries(
    test_brick_search_baseline
    PRIVATE
        hbrick_baselines
        hbrick_test_support
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_brick_search_baseline)

add_executable(test_brick_closure_baseline unit/test_brick_closure_baseline.cpp)

target_link_libraries(
    test_brick_closure_baseline
    PRIVATE
        hbrick_baselines
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_brick_closure_baseline)

add_executable(test_hbrick_baseline unit/test_hbrick_baseline.cpp)

target_link_libraries(
    test_hbrick_baseline
    PRIVATE
        hbrick_baselines
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_hbrick_baseline)

add_executable(test_csr_graph unit/test_csr_graph.cpp)

target_link_libraries(
    test_csr_graph
    PRIVATE
        hbrick_graph
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_csr_graph)

add_executable(test_directed_grid_graph unit/test_directed_grid_graph.cpp)

target_link_libraries(
    test_directed_grid_graph
    PRIVATE
        hbrick_graph
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_directed_grid_graph)

add_executable(test_bfs_dfs unit/test_bfs_dfs.cpp)

target_link_libraries(
    test_bfs_dfs
    PRIVATE
        hbrick_graph
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_bfs_dfs)

add_executable(test_reachability_density unit/test_reachability_density.cpp)

target_link_libraries(
    test_reachability_density
    PRIVATE
        hbrick_graph
        hbrick_test_support
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_reachability_density)

add_executable(test_bit_vector unit/test_bit_vector.cpp)
add_executable(test_bit_matrix unit/test_bit_matrix.cpp)
add_executable(test_boolean_closure unit/test_boolean_closure.cpp)
add_executable(test_connected_components unit/test_connected_components.cpp)

target_link_libraries(
    test_bit_vector
    PRIVATE
        hbrick_bit
        GTest::gtest_main
        hbrick_project_warnings
)

target_link_libraries(
    test_bit_matrix
    PRIVATE
        hbrick_bit
        GTest::gtest_main
        hbrick_project_warnings
)

target_link_libraries(
    test_boolean_closure
    PRIVATE
        hbrick_bit
        hbrick_graph
        hbrick_tile
        GTest::gtest_main
        hbrick_project_warnings
)

target_link_libraries(
    test_connected_components
    PRIVATE
        hbrick_graph
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_bit_vector)
hbrick_register_gtests(test_bit_matrix)
hbrick_register_gtests(test_boolean_closure)
hbrick_register_gtests(test_connected_components)

add_executable(test_closure_matrix_builder unit/test_closure_matrix_builder.cpp)

target_link_libraries(
    test_closure_matrix_builder
    PRIVATE
        hbrick_baselines
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_closure_matrix_builder)

add_executable(test_scc_dag unit/test_scc_dag.cpp)

target_link_libraries(
    test_scc_dag
    PRIVATE
        hbrick_baselines
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_scc_dag)

add_executable(test_csr_baselines unit/test_csr_baselines.cpp)

target_link_libraries(
    test_csr_baselines
    PRIVATE
        hbrick_baselines
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_csr_baselines)

add_executable(test_svg_canvas unit/test_svg_canvas.cpp)

target_link_libraries(
    test_svg_canvas
    PRIVATE
        hbrick_viz
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_svg_canvas)

add_executable(test_bench unit/test_bench.cpp)

target_link_libraries(
    test_bench
    PRIVATE
        hbrick_bench
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_bench)

add_executable(test_reachability_benchmark unit/test_reachability_benchmark.cpp)

target_link_libraries(
    test_reachability_benchmark
    PRIVATE
        hbrick_bench
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_reachability_benchmark)

add_executable(test_reachability_benchmark_format unit/test_reachability_benchmark_format.cpp)

target_link_libraries(
    test_reachability_benchmark_format
    PRIVATE
        hbrick_bench
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_reachability_benchmark_format)

add_executable(test_reachability_benchmark_runner unit/test_reachability_benchmark_runner.cpp)

target_link_libraries(
    test_reachability_benchmark_runner
    PRIVATE
        hbrick_bench
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_reachability_benchmark_runner)

add_executable(test_correctness_harness integration/test_correctness_harness.cpp)

target_link_libraries(
    test_correctness_harness
    PRIVATE
        hbrick_test_support
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_correctness_harness)

add_executable(test_maze_reachability integration/test_maze_reachability.cpp)

target_link_libraries(
    test_maze_reachability
    PRIVATE
        hbrick_test_support
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_maze_reachability)

add_executable(test_dag_reachability unit/test_dag_reachability.cpp)

target_link_libraries(
    test_dag_reachability
    PRIVATE
        hbrick_test_support
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_dag_reachability)

add_executable(test_hot_path_allocations unit/test_hot_path_allocations.cpp)

target_link_libraries(
    test_hot_path_allocations
    PRIVATE
        hbrick_test_support
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_hot_path_allocations)

add_executable(test_maze_generator unit/test_maze_generator.cpp)

target_link_libraries(
    test_maze_generator
    PRIVATE
        hbrick_test_support
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_maze_generator)

add_executable(test_movingai_loader unit/test_movingai_loader.cpp)

target_compile_definitions(
    test_movingai_loader
    PRIVATE
        HBRICK_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(
    test_movingai_loader
    PRIVATE
        hbrick_io
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_movingai_loader)

add_executable(test_scc_reachability integration/test_scc_reachability.cpp)

target_link_libraries(
    test_scc_reachability
    PRIVATE
        hbrick_test_support
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_scc_reachability)

add_executable(test_recipe_reachability integration/test_recipe_reachability.cpp)

target_compile_definitions(
    test_recipe_reachability
    PRIVATE
        HBRICK_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(
    test_recipe_reachability
    PRIVATE
        hbrick_test_support
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_gtests(test_recipe_reachability)

add_executable(test_movingai_reachability integration/test_movingai_reachability.cpp)

target_compile_definitions(
    test_movingai_reachability
    PRIVATE
        HBRICK_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(
    test_movingai_reachability
    PRIVATE
        hbrick_test_support
        GTest::gtest_main
        hbrick_project_warnings
)

hbrick_register_slow_gtests(
    test_movingai_reachability
    ${HBRICK_MOVINGAI_GTEST_TIMEOUT}
)
