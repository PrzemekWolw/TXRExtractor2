#include "i18n.hpp"
#include "package.hpp"
#include "selftest.hpp"
#include "settings.hpp"
#include "task.hpp"
#include "theme.hpp"
#include "ui.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <iostream>
#include <string>

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#include <windows.h>
#endif

namespace {

void setTitleBarColor(GLFWwindow* window)
{
#if defined(_WIN32)
    const HWND hwnd = glfwGetWin32Window(window);
    auto setColor = [&](DWORD attribute, const ImVec4& color) {
        const COLORREF value = RGB(int(color.x * 255), int(color.y * 255), int(color.z * 255));
        DwmSetWindowAttribute(hwnd, attribute, &value, sizeof(value));
    };
    setColor(35, colBar);
    setColor(36, colText);
#else
    (void)window;
#endif
}

void renderUI()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##main", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();
    if (ImGui::BeginTabBar("##tabs")) {
        packageTab();
        ImGui::EndTabBar();
    }
    ImGui::End();
}

int runCli(int argc, char** argv)
{
    bool combine = false;
    int i = 1;
    if (i < argc && std::string(argv[i]) == "--combine") {
        combine = true;
        ++i;
    }
    if (argc - i < 3) {
        std::cerr << "TXRExtractor2 [--combine] <archive> <toc> <output> [image]\n";
        return 2;
    }
    ExportRequest request;
    request.archive = argv[i];
    request.toc = argv[i + 1];
    request.output = argv[i + 2];
    if (argc - i >= 4) {
        request.image = argv[i + 3];
    }
    request.combine = combine;
    TaskState task;
    exportPackage(request, task);
    for (const std::string& line : task.logLines) {
        std::cout << line << "\n";
    }
    std::cout << task.status << "\n";
    return task.status == "Done" ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
#if defined(_WIN32)
    AttachConsole(ATTACH_PARENT_PROCESS);
    FILE* unused = nullptr;
    freopen_s(&unused, "CONOUT$", "w", stdout);
    freopen_s(&unused, "CONOUT$", "w", stderr);
#endif
    if (argc >= 2 && std::string(argv[1]) == "--selftest") {
        return selftest();
    }
    if (argc >= 2) {
        return runCli(argc, argv);
    }

    i18nInit();
    loadSettings();
    if (!glfwInit()) {
        return 1;
    }
    setRedrawCallback([] { glfwPostEmptyEvent(); });
#if defined(__APPLE__)
    const char* glslVersion = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    const char* glslVersion = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHintString(GLFW_WAYLAND_APP_ID, "txrextractor2");
    glfwWindowHintString(GLFW_X11_CLASS_NAME, "TXRExtractor2");
    glfwWindowHintString(GLFW_X11_INSTANCE_NAME, "TXRExtractor2");
#endif
    const std::string title = std::string("TXRExtractor2 v") + APP_VERSION;
    GLFWwindow* window = glfwCreateWindow(1100, 720, title.c_str(), nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    setTitleBarColor(window);
    uiScale = 1.0f;
#if !defined(__APPLE__)
    if (glfwGetPlatform() != GLFW_PLATFORM_WAYLAND) {
        glfwGetWindowContentScale(window, &uiScale, nullptr);
    }
#endif
    if (uiScale < 1.0f) {
        uiScale = 1.0f;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    applyTheme();
    loadFont();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);

    while (!glfwWindowShouldClose(window)) {
        glfwWaitEventsTimeout(0.1);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        renderUI();
        ImGui::Render();
        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(colBg.x, colBg.y, colBg.z, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    std::_Exit(0);
}
