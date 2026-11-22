#include "Slowload.h"

#include <PluginChat.h>

#include <GWCA/Constants/Constants.h>
#include <GWCA/Managers/ChatMgr.h>
#include <GWCA/Managers/MapMgr.h>
#include <GWCA/Managers/MemoryMgr.h>
#include <GWCA/Packets/StoC.h>
#include <GWCA/Utilities/Hook.h>
#include <GWCA/stdlib/GameThreadMgr.h>
#include <GWCA/stdlib/StoCMgr.h>

#include <imgui.h>

#include <atomic>

DLLAPI ToolboxPlugin* ToolboxPluginInstance()
{
    static Slowload instance;
    return &instance;
}

namespace {
    enum class Status {
        Inactive,
        WaitingForLoadScreen,
        WaitingInLoadScreen,
        StoppingSlowload,
    };

    std::atomic<Status> status = Status::Inactive;
    GW::HookEntry instance_load_entry;
    GW::Packet::StoC::InstanceLoadFile held_packet;

    void StopSlowload()
    {
        if (status != Status::WaitingInLoadScreen) {
            return;
        }
        status = Status::StoppingSlowload;
        GW::GameThread::Enqueue([packet = held_packet]() mutable {
            GW::StoC::EmulatePacket(&packet);
            status = Status::Inactive;
        });
    }

    bool IsLoading()
    {
        return GW::Map::GetInstanceType() == GW::Constants::InstanceType::Loading;
    }
}

bool Slowload::WndProc(const UINT message, const WPARAM wparam, const LPARAM lparam)
{
    if (GW::Chat::GetIsTyping() || GW::MemoryMgr::GetGWWindowHandle() != GetActiveWindow()) {
        return false;
    }
    if (!Hotkeys::IsTriggered(shortcut, message, wparam, lparam)) {
        return false;
    }
    switch (status) {
        case Status::Inactive:
            if (IsLoading()) {
                return false;
            }
            status = Status::WaitingForLoadScreen;
            PluginChat::Write(L"Slowload plugin", L"Activated");
            break;
        case Status::WaitingForLoadScreen:
            if (IsLoading()) {
                return false;
            }
            status = Status::Inactive;
            PluginChat::Write(L"Slowload plugin", L"Deactivated");
            break;
        case Status::WaitingInLoadScreen:
            StopSlowload();
            break;
        case Status::StoppingSlowload:
            break;
    }
    return true;
}

void Slowload::LoadSettings(const wchar_t* folder)
{
    SpeedrunPlugin::LoadSettings(folder);
    LoadSetting("shortcutKey", shortcut.key);
    LoadSetting("shortcutMod", shortcut.modifier);
}

void Slowload::SaveSettings(const wchar_t* folder)
{
    SaveSetting("shortcutKey", shortcut.key);
    SaveSetting("shortcutMod", shortcut.modifier);
    SpeedrunPlugin::SaveSettings(folder);
}

void Slowload::DrawPluginSettings()
{
    ImGui::Text("Hotkey: ");
    ImGui::SameLine();
    Hotkeys::DrawSelector(shortcut, 80.f);
    if (shortcut.key) {
        ImGui::SameLine();
        if (ImGui::Button("Clear###0")) {
            shortcut = {};
        }
    }
}

void Slowload::Initialize(ImGuiContext* ctx, const ImGuiAllocFns allocator_fns, const HMODULE toolbox_dll)
{
    SpeedrunPlugin::Initialize(ctx, allocator_fns, toolbox_dll);
    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::InstanceLoadFile>(&instance_load_entry, [this](GW::HookStatus* hook_status, GW::Packet::StoC::InstanceLoadFile* packet) {
        if (shortcut.key && status == Status::WaitingForLoadScreen) {
            status = Status::WaitingInLoadScreen;
            held_packet = *packet;
            hook_status->blocked = true;
        }
        else {
            status = Status::Inactive;
        }
    });
}

void Slowload::SignalTerminate()
{
    GW::StoC::RemoveCallback<GW::Packet::StoC::InstanceLoadFile>(&instance_load_entry);
    SpeedrunPlugin::SignalTerminate();
}
