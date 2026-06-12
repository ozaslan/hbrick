/**
 * @file main.cpp
 * @brief Entry point for the hbrick dataset browser (GLFW + Dear ImGui).
 *
 * Usage: hbrick_dataset_browser [dataset_root]
 * Defaults to the repository's datasets/movingai/extracted directory.
 */

#include <cstdio>
#include <filesystem>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "browser_app.hpp"

namespace {

void glfwErrorCallback(const int code, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, description);
}

}  // namespace

int main(const int argc, const char** argv) {
    const std::filesystem::path dataset_root =
        argc > 1 ? std::filesystem::path(argv[1])
                 : std::filesystem::path(HBRICK_DATASETS_DIR);

    glfwSetErrorCallback(glfwErrorCallback);
    if (glfwInit() == GLFW_FALSE) {
        std::fprintf(stderr, "failed to initialize GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    GLFWwindow* window = glfwCreateWindow(
        1480,
        920,
        "hbrick dataset browser - MovingAI benchmarks (read-only)",
        nullptr,
        nullptr
    );
    if (window == nullptr) {
        std::fprintf(stderr, "failed to create window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;  // Read-only tool: do not write imgui.ini anywhere.
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    {
        hbrick::tools::BrowserApp app(dataset_root);

        while (glfwWindowShouldClose(window) == GLFW_FALSE) {
            glfwPollEvents();

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            app.drawFrame();

            ImGui::Render();
            int display_w = 0;
            int display_h = 0;
            glfwGetFramebufferSize(window, &display_w, &display_h);
            glViewport(0, 0, display_w, display_h);
            glClearColor(0.06F, 0.06F, 0.08F, 1.0F);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            glfwSwapBuffers(window);
        }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
