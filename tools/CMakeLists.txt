# Developer tools. Everything here is optional (HBRICK_BUILD_TOOLS) and must
# never become a dependency of the core library targets.

include(FetchContent)

# --- GLFW: prefer the system package, fall back to a vendored build ---------
find_package(glfw3 3.3 QUIET)

if(NOT glfw3_FOUND)
    message(STATUS "hbrick tools: system GLFW not found, vendoring via FetchContent")
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    # The X11 backend needs Xcursor headers that are often absent on desktop
    # installs; the Wayland backend builds from wayland + xkbcommon dev files.
    set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
    set(GLFW_BUILD_X11 OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        glfw
        URL https://github.com/glfw/glfw/archive/refs/tags/3.4.zip
    )
    FetchContent_MakeAvailable(glfw)
endif()

# --- Dear ImGui (docking branch), compiled as a small static library --------
FetchContent_Declare(
    imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.8-docking.zip
)
FetchContent_MakeAvailable(imgui)

add_library(
    hbrick_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(
    hbrick_imgui
    PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(hbrick_imgui PUBLIC glfw)

find_package(OpenGL REQUIRED)
target_link_libraries(hbrick_imgui PUBLIC OpenGL::GL)

add_subdirectory(dataset_browser)
