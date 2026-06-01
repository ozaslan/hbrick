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

add_executable(test_placeholder unit/test_placeholder.cpp)

target_link_libraries(
    test_placeholder
    PRIVATE
        hbrick_core
        GTest::gtest_main
        hbrick_project_warnings
)

add_test(NAME test_placeholder COMMAND test_placeholder)

add_executable(test_core_types unit/test_core_types.cpp)

target_link_libraries(
    test_core_types
    PRIVATE
        hbrick_core
        GTest::gtest_main
        hbrick_project_warnings
)

add_test(NAME test_core_types COMMAND test_core_types)

add_executable(test_passable_grid unit/test_passable_grid.cpp)

target_link_libraries(
    test_passable_grid
    PRIVATE
        hbrick_grid
        GTest::gtest_main
        hbrick_project_warnings
)

add_test(NAME test_passable_grid COMMAND test_passable_grid)

add_executable(test_csr_graph unit/test_csr_graph.cpp)

target_link_libraries(
    test_csr_graph
    PRIVATE
        hbrick_graph
        GTest::gtest_main
        hbrick_project_warnings
)

add_test(NAME test_csr_graph COMMAND test_csr_graph)

add_executable(test_directed_grid_graph unit/test_directed_grid_graph.cpp)

target_link_libraries(
    test_directed_grid_graph
    PRIVATE
        hbrick_graph
        GTest::gtest_main
        hbrick_project_warnings
)

add_test(NAME test_directed_grid_graph COMMAND test_directed_grid_graph)

add_executable(test_bfs_dfs unit/test_bfs_dfs.cpp)

target_link_libraries(
    test_bfs_dfs
    PRIVATE
        hbrick_graph
        GTest::gtest_main
        hbrick_project_warnings
)

add_test(NAME test_bfs_dfs COMMAND test_bfs_dfs)

add_executable(test_bit_vector unit/test_bit_vector.cpp)
add_executable(test_bit_matrix unit/test_bit_matrix.cpp)
add_executable(test_boolean_closure unit/test_boolean_closure.cpp)

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
        GTest::gtest_main
        hbrick_project_warnings
)

add_test(NAME test_bit_vector COMMAND test_bit_vector)
add_test(NAME test_bit_matrix COMMAND test_bit_matrix)
add_test(NAME test_boolean_closure COMMAND test_boolean_closure)

add_executable(test_closure_matrix_builder unit/test_closure_matrix_builder.cpp)

target_link_libraries(
    test_closure_matrix_builder
    PRIVATE
        hbrick_baselines
        GTest::gtest_main
        hbrick_project_warnings
)

add_test(NAME test_closure_matrix_builder COMMAND test_closure_matrix_builder)

add_executable(test_scc_dag unit/test_scc_dag.cpp)

target_link_libraries(
    test_scc_dag
    PRIVATE
        hbrick_baselines
        GTest::gtest_main
        hbrick_project_warnings
)

add_test(NAME test_scc_dag COMMAND test_scc_dag)

add_executable(test_csr_baselines unit/test_csr_baselines.cpp)

target_link_libraries(
    test_csr_baselines
    PRIVATE
        hbrick_baselines
        GTest::gtest_main
        hbrick_project_warnings
)

add_test(NAME test_csr_baselines COMMAND test_csr_baselines)

add_executable(test_svg_canvas unit/test_svg_canvas.cpp)

target_link_libraries(
    test_svg_canvas
    PRIVATE
        hbrick_viz
        GTest::gtest_main
        hbrick_project_warnings
)

add_test(NAME test_svg_canvas COMMAND test_svg_canvas)

add_executable(test_bench unit/test_bench.cpp)

target_link_libraries(
    test_bench
    PRIVATE
        hbrick_bench
        GTest::gtest_main
        hbrick_project_warnings
)

add_test(NAME test_bench COMMAND test_bench)

add_executable(test_correctness_harness integration/test_correctness_harness.cpp)

target_link_libraries(
    test_correctness_harness
    PRIVATE
        hbrick_baselines
        hbrick_bench
        GTest::gtest_main
        hbrick_project_warnings
)

add_test(NAME test_correctness_harness COMMAND test_correctness_harness)
