#include "FlatBowRangeIndicator.h"

#include <GWCA/Constants/Constants.h>
#include <GWCA/GameContainers/GamePos.h>
#include <GWCA/GameEntities/Agent.h>
#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/Managers/MapMgr.h>

#include <imgui.h>

DLLAPI ToolboxPlugin* ToolboxPluginInstance()
{
    static FlatBowRangeIndicator instance;
    return &instance;
}

namespace {
    float GetAltitude(const GW::AgentLiving* agent)
    {
        return agent ? -GW::Map::QueryAltitude(&agent->pos, 10) : 0.f;
    }

    enum class Result { InRange, OutOfRange, NoValidTarget };

    Result TargetInBowRange()
    {
        const auto player = GW::Agents::GetControlledCharacter();
        const auto target = GW::Agents::GetTargetAsAgentLiving();
        if (!player || !target || target->allegiance != GW::Constants::Allegiance::Enemy) {
            return Result::NoValidTarget;
        }
        const auto altitude_difference = GetAltitude(player) - GetAltitude(target);
        const auto max_attack_range = std::min(2000., altitude_difference * (2e-4 * altitude_difference + 1) + 1498.);
        const auto distance = GW::GetDistance(player->pos, target->pos);
        return distance <= max_attack_range ? Result::InRange : Result::OutOfRange;
    }
}

FlatBowRangeIndicator::FlatBowRangeIndicator()
{
    show_closebutton = false;
    show_title = false;
    can_show_in_main_window = false;
    can_collapse = false;
    can_close = false;
    show_menubutton = false;
}

void FlatBowRangeIndicator::Draw(IDirect3DDevice9*)
{
    if (!GetVisiblePtr() || !*GetVisiblePtr()) {
        return;
    }

    const auto& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(200, 100), ImGuiCond_FirstUseEver);

    const auto result = TargetInBowRange();
    if (result == Result::NoValidTarget) {
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
    if (ImGui::Begin(Name(), nullptr, GetWinFlags())) {
        constexpr auto green = ImVec4{106.f / 255.f, 168.f / 255.f, 79.f / 255.f, 1.f};
        constexpr auto red = ImVec4{224.f / 255.f, 102.f / 255.f, 102.f / 255.f, 1.f};
        constexpr auto corner_rounding = 4.f;
        const auto pos = ImGui::GetWindowPos();
        const auto size = ImGui::GetWindowSize();
        ImGui::GetForegroundDrawList()->AddRectFilled(pos, {pos.x + size.x, pos.y + size.y}, ImGui::ColorConvertFloat4ToU32(result == Result::InRange ? green : red), corner_rounding);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}
