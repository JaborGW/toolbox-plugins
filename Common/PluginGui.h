#pragma once

#include <imgui.h>

namespace PluginGui {
    inline void ShowHelp(const char* help)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", "(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", help);
        }
    }
}
