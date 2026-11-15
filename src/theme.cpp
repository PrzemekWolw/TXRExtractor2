#include "theme.hpp"

#include "platform.hpp"

#include <algorithm>
#include <cmath>

float uiScale = 1.0f;

float px(float value)
{
    if (value == 0.0f) {
        return 0.0f;
    }
    const float sign = value < 0.0f ? -1.0f : 1.0f;
    return sign * std::max(1.0f, std::round(std::fabs(value) * uiScale));
}

void applyTheme()
{
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    colors[ImGuiCol_Text] = colText;
    colors[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.12f, 0.16f, 0.96f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.08f, 0.10f, 0.14f, 0.25f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.11f, 0.15f, 0.20f, 1.00f);
    colors[ImGuiCol_Border] = ImVec4(0.00f, 0.00f, 0.00f, 0.30f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.19f, 0.25f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.24f, 0.31f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.00f, 0.00f, 0.00f, 0.24f);
    colors[ImGuiCol_TitleBg] = colBar;
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.20f, 0.24f, 0.31f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.07f, 0.08f, 0.12f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.15f, 0.18f, 0.24f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.81f, 0.40f, 0.13f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.81f, 0.40f, 0.13f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.81f, 0.40f, 0.13f, 0.71f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.40f, 0.00f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.15f, 0.18f, 0.24f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.20f, 0.24f, 0.31f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.81f, 0.40f, 0.12f, 0.78f);
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.24f, 0.31f, 0.63f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.20f, 0.24f, 0.31f, 0.78f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.24f, 0.31f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.24f, 0.31f, 1.00f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.81f, 0.40f, 0.13f, 1.00f);
    colors[ImGuiCol_Tab] = ImVec4(0.00f, 0.00f, 0.00f, 0.35f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.29f, 0.39f, 1.00f);
    colors[ImGuiCol_TabSelected] = ImVec4(0.67f, 0.33f, 0.10f, 1.00f);
    colors[ImGuiCol_TabSelectedOverline] = ImVec4(0.81f, 0.40f, 0.13f, 1.00f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(1.00f, 0.47f, 0.00f, 0.43f);

    style.WindowPadding = ImVec2(px(10), px(10));
    style.FramePadding = ImVec2(px(6), px(4));
    style.ItemSpacing = ImVec2(px(8), px(6));
    style.ItemInnerSpacing = ImVec2(px(6), px(4));
    style.ScrollbarSize = px(14);
    style.WindowRounding = px(2);
    style.FrameRounding = px(4);
    style.GrabRounding = px(4);
    style.TabRounding = px(2);
    style.FontSizeBase = 18.7f;
    style.FontScaleDpi = uiScale;
}

void loadFont()
{
    const fs::path candidates[] = {exeDir() / "fonts" / "OpenSans-Regular.ttf", fs::path("fonts") / "OpenSans-Regular.ttf"};
    for (const fs::path& path : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(path, ec)) {
            ImGui::GetIO().Fonts->AddFontFromFileTTF(u8str(path).c_str(), 18.7f);
            return;
        }
    }
}
