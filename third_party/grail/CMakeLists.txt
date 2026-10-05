add_library(
    hbrick_grail_vendor
    STATIC
    Grail.cpp
    Graph.cpp
    GraphUtil.cpp
    TCSEstimator.cpp
)

target_include_directories(
    hbrick_grail_vendor
    SYSTEM PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_compile_features(hbrick_grail_vendor PUBLIC cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        hbrick_grail_vendor
        PRIVATE
            -w
    )
endif()
