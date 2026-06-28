add_library(
    hbrick_bench
    bench_timer.cpp
    bench_sample.cpp
    process_memory.cpp
    reachability_benchmark.cpp
    reachability_benchmark_format.cpp
    reachability_benchmark_util.cpp
    reachability_benchmark_runner.cpp
)

target_include_directories(
    hbrick_bench
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    hbrick_bench
    PUBLIC
        hbrick_core
        hbrick_baselines
        hbrick_project_options
)
target_link_libraries(hbrick_bench PRIVATE hbrick_project_warnings)

add_library(hbrick::bench ALIAS hbrick_bench)

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
