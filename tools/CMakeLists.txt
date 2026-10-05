# Command-line benchmarks. These targets are part of the default build.

add_executable(
    hbrick_quick_bench
    hbrick_quick_bench.cpp
)

target_link_libraries(
    hbrick_quick_bench
    PRIVATE
        hbrick_bench
        hbrick_io
        hbrick_grid
        hbrick_graph
        hbrick_project_warnings
)

add_executable(
    hbrick_batch_pilot
    hbrick_batch_pilot.cpp
)

target_link_libraries(
    hbrick_batch_pilot
    PRIVATE
        hbrick_tile
        hbrick_baselines
        hbrick_io
        hbrick_grid
        hbrick_graph
        hbrick_project_warnings
)
