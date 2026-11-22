#include "Follow.h"

#include <GWCA/GameContainers/GamePos.h>
#include <GWCA/GameEntities/Agent.h>
#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/Managers/ChatMgr.h>
#include <GWCA/Managers/GameThreadMgr.h>
#include <GWCA/Managers/UIMgr.h>
#include <GWCA/Utilities/Hook.h>

#include <PluginChat.h>

#include <imgui.h>

DLLAPI ToolboxPlugin* ToolboxPluginInstance()
{
    static FollowPlugin instance;
    return &instance;
}

namespace {
    bool following = false;
    GW::HookEntry chat_command_entry;
}

void FollowPlugin::Update(float)
{
    const auto player = GW::Agents::GetControlledCharacter();
    const auto target = GW::Agents::GetTargetAsAgentLiving();
    if (!following || !player || !target || !player->GetIsIdle() || !target->GetIsLivingType()) {
        return;
    }
    if (GW::GetSquareDistance(player->pos, target->pos) < follow_distance * follow_distance) {
        return;
    }
    GW::GameThread::Enqueue([](void*) {
        GW::UI::Keydown(GW::UI::ControlAction::ControlAction_Interact);
    });
}

void FollowPlugin::LoadSettings(const wchar_t* folder)
{
    SpeedrunPlugin::LoadSettings(folder);
    LoadSetting("followDistance", follow_distance);
}

void FollowPlugin::SaveSettings(const wchar_t* folder)
{
    SaveSetting("followDistance", follow_distance);
    SpeedrunPlugin::SaveSettings(folder);
}

void FollowPlugin::DrawPluginSettings()
{
    ImGui::Text("Usage: Type /follow to activate or deactivate.");

    ImGui::PushItemWidth(100.f);
    ImGui::Text("Start following once target is further than");
    ImGui::SameLine();
    ImGui::InputFloat("inches away", &follow_distance);
    ImGui::PopItemWidth();
}

void FollowPlugin::Initialize(ImGuiContext* ctx, const ImGuiAllocFns allocator_fns, const HMODULE toolbox_dll)
{
    SpeedrunPlugin::Initialize(ctx, allocator_fns, toolbox_dll);
    GW::Chat::CreateCommand(&chat_command_entry, L"follow", [](GW::HookStatus*, const wchar_t*, int, const LPWSTR*) {
        following = !following;
        PluginChat::Write(L"Follow plugin", following ? L"Activated" : L"Deactivated");
    });
}

void FollowPlugin::SignalTerminate()
{
    SpeedrunPlugin::SignalTerminate();
    GW::Chat::DeleteCommand(&chat_command_entry, L"follow");
}
