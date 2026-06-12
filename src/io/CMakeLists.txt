add_library(hbrick_io movingai_map.cpp movingai_loader.cpp)

target_include_directories(
    hbrick_io
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(hbrick_io PUBLIC hbrick_core hbrick_grid hbrick_project_options)
target_link_libraries(hbrick_io PRIVATE hbrick_project_warnings)

add_library(hbrick::io ALIAS hbrick_io)
