add_library(
    hbrick_bit
    bit_vector.cpp
    bit_matrix.cpp
    boolean_closure.cpp
)

target_include_directories(
    hbrick_bit
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(hbrick_bit PUBLIC hbrick_core hbrick_project_options)
target_link_libraries(hbrick_bit PRIVATE hbrick_project_warnings)

add_library(hbrick::bit ALIAS hbrick_bit)
