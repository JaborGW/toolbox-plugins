#pragma once

#include <Windows.h>
#include <shellapi.h>

#include <imgui.h>

namespace PluginFooter {
    inline void Draw(const char* version)
    {
        constexpr auto discord_invite_link = "https://discord.gg/ZpKzer4dK9";
        ImGui::Text("Version %s. For new releases, feature requests and bug reports check out", version);
        ImGui::SameLine();
        ImGui::TextColored(ImColor{102, 187, 238, 255}, "%s", discord_invite_link);
        if (ImGui::IsItemClicked()) {
            ShellExecuteA(nullptr, "open", discord_invite_link, nullptr, nullptr, SW_SHOWNORMAL);
        }
    }
}
