#include "PathingVisualizer.h"

#include <WorldRenderer.h>

#include <GWCA/Constants/Constants.h>
#include <GWCA/Context/AgentContext.h>
#include <GWCA/GameContainers/Array.h>
#include <GWCA/GameEntities/Agent.h>
#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/Managers/MapMgr.h>

#include <imgui.h>

#include <cstring>

DLLAPI ToolboxPlugin* ToolboxPluginInstance()
{
    static PathingVisualizer instance;
    return &instance;
}

namespace {
    // Past the mapped part of GWCA's AgentMovement; the old plugin read the movement destination as raw floats here.
    constexpr size_t movement_destination_offset = 34 * sizeof(float);

    bool ShouldRender()
    {
        return GW::Map::GetInstanceType() == GW::Constants::InstanceType::Explorable && GW::Agents::GetControlledCharacter() && !GW::Agents::IsObserving();
    }

    const GW::AgentMovement* TargetMovement(const GW::AgentLiving* target)
    {
        const auto agent_context = GW::GetAgentContext();
        if (!target || !agent_context || agent_context->agent_movement.size() <= target->agent_id) {
            return nullptr;
        }
        return agent_context->agent_movement[target->agent_id];
    }
}

void PathingVisualizer::Update(float)
{
    const auto target = ShouldRender() ? GW::Agents::GetTargetAsAgentLiving() : nullptr;
    const auto movement = TargetMovement(target);
    if (!movement) {
        WorldRenderer::ClearPolyline();
        return;
    }
    float destination[2];
    std::memcpy(destination, reinterpret_cast<const uint8_t*>(movement) + movement_destination_offset, sizeof(destination));
    WorldRenderer::SetPolyline({target->pos, GW::GamePos{destination[0], destination[1], 0}}, ImGui::ColorConvertFloat4ToU32({1.f, 1.f, 1.f, 1.f}));
}

void PathingVisualizer::Draw(IDirect3DDevice9* device)
{
    if (ShouldRender()) {
        WorldRenderer::Draw(device);
    }
    else {
        WorldRenderer::ClearPolyline();
    }
}

void PathingVisualizer::Terminate()
{
    WorldRenderer::Terminate();
    SpeedrunPlugin::Terminate();
}

void PathingVisualizer::LoadSettings(const wchar_t* folder)
{
    SpeedrunPlugin::LoadSettings(folder);
    LoadSetting("visible", visible);
}

void PathingVisualizer::SaveSettings(const wchar_t* folder)
{
    SaveSetting("visible", visible);
    SpeedrunPlugin::SaveSettings(folder);
}
