add_library(
    hbrick_oreach_vendor
    STATIC
    lib/reachability/oreach.cpp
    lib/algorithms/topological_sort.cpp
    lib/algorithms/bfs.cpp
    extern/KaHIP/lib/algorithms/strongly_connected_components.cpp
    extern/KaHIP/lib/tools/random_functions.cpp
    extern/murmur_hash/MurmurHash3.cpp
)

target_include_directories(
    hbrick_oreach_vendor
    SYSTEM PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/extern/KaHIP/lib>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/extern/KaHIP/lib/data_structure>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/extern/KaHIP/lib/tools>
)

target_link_libraries(
    hbrick_oreach_vendor
    PUBLIC
        Boost::boost
)

target_compile_features(hbrick_oreach_vendor PUBLIC cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        hbrick_oreach_vendor
        PRIVATE
            -w
    )
endif()
