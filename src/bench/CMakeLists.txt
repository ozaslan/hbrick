add_library(
    hbrick_bench
    bench_timer.cpp
    bench_sample.cpp
    process_memory.cpp
    reachability_benchmark.cpp
    reachability_benchmark_format.cpp
    reachability_benchmark_util.cpp
    reachability_benchmark_runner.cpp
    benchmark_campaign.cpp
)

target_include_directories(
    hbrick_bench
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_definitions(
    hbrick_bench
    PRIVATE
        HBRICK_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        HBRICK_COMPILER_ID="${CMAKE_CXX_COMPILER_ID}"
        HBRICK_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
)

target_link_libraries(
    hbrick_bench
    PUBLIC
        hbrick_core
        hbrick_baselines
        hbrick_graph
        hbrick_grid
        hbrick_project_options
)
target_link_libraries(hbrick_bench PRIVATE hbrick_project_warnings)

add_library(hbrick::bench ALIAS hbrick_bench)

add_executable(hbrick_benchmark_campaign benchmark_campaign_main.cpp)
target_link_libraries(
    hbrick_benchmark_campaign
    PRIVATE
        hbrick_bench
        hbrick_project_warnings
)

add_executable(kleene_closure_benchmark kleene_closure_benchmark.cpp)
target_link_libraries(
    kleene_closure_benchmark
    PRIVATE
        hbrick_bench
        hbrick_bit
        hbrick_graph
        hbrick_tile
        hbrick_project_warnings
)

add_executable(port_closure_benchmark port_closure_benchmark.cpp)
target_link_libraries(
    port_closure_benchmark
    PRIVATE
        hbrick_bench
        hbrick_baselines
        hbrick_bit
        hbrick_graph
        hbrick_tile
        hbrick_project_warnings
)
