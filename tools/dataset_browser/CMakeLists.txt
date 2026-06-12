add_executable(
    hbrick_dataset_browser
    main.cpp
    dataset_index.cpp
    map_render.cpp
    browser_app.cpp
)

target_compile_definitions(
    hbrick_dataset_browser
    PRIVATE
        HBRICK_DATASETS_DIR="${CMAKE_SOURCE_DIR}/datasets/movingai/extracted"
)

target_link_libraries(
    hbrick_dataset_browser
    PRIVATE
        hbrick_io
        hbrick_imgui
        hbrick_project_options
)
