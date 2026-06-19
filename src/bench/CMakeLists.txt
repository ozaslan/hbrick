add_library(
    hbrick_bench
    bench_timer.cpp
    bench_sample.cpp
    process_memory.cpp
    reachability_benchmark.cpp
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
