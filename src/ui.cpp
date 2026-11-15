#include "ui.hpp"

#include "i18n.hpp"
#include "package.hpp"
#include "platform.hpp"
#include "settings.hpp"
#include "task.hpp"
#include "theme.hpp"

#include "imgui.h"

#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace {

TaskState task;

void pathRow(const char* label, std::string& value, bool directory)
{
    ImGui::TextUnformatted(tr(label));
    ImGui::SetNextItemWidth(-px(110));
    char buffer[1024];
    std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
    const std::string id = std::string("##") + label;
    if (ImGui::InputText(id.c_str(), buffer, sizeof(buffer))) {
        value = buffer;
        saveSettings();
    }
    ImGui::SameLine();
    if (ImGui::Button((std::string(tr("txr.browse")) + "##" + label).c_str(), ImVec2(px(100), 0))) {
        const std::string picked = pickPath(directory);
        if (!picked.empty()) {
            value = picked;
            saveSettings();
        }
    }
}

void checkbox(const char* label, bool& value)
{
    if (ImGui::Checkbox(tr(label), &value)) {
        saveSettings();
    }
}

} // namespace

void packageTab()
{
    if (!ImGui::BeginTabItem(tr("txr.tab.package"))) {
        return;
    }
    Settings& cfg = settings();
    pathRow("txr.archive", cfg.archive, false);
    pathRow("txr.toc", cfg.toc, false);
    pathRow("txr.output", cfg.output, true);
    pathRow("txr.image", cfg.image, false);
    checkbox("txr.raw", cfg.raw);
    ImGui::SameLine();
    checkbox("txr.models", cfg.models);
    ImGui::SameLine();
    checkbox("txr.textures", cfg.textures);
    checkbox("txr.combine", cfg.combine);

    bool running;
    std::string status;
    std::vector<std::string> lines;
    {
        std::lock_guard lock(task.mu);
        running = task.running;
        status = task.status;
        lines = task.logLines;
    }
    ImGui::BeginDisabled(running);
    if (ImGui::Button(tr("txr.export"))) {
        ExportRequest request{cfg.archive, cfg.toc, cfg.output, cfg.image, cfg.raw, cfg.models, cfg.textures, cfg.combine};
        startTask(task, "Exporting", [request] { exportPackage(request, task); });
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!running);
    if (ImGui::Button(tr("txr.stop"))) {
        task.stop = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextUnformatted(status.c_str());

    ImGui::BeginChild("log", ImVec2(0, 0), ImGuiChildFlags_Borders);
    for (const std::string& line : lines) {
        ImGui::TextUnformatted(line.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::EndTabItem();
}
