#include "RawDialogs.h"

#include "CtoS.h"

#include <DirectDialog.h>
#include <PluginChat.h>
#include <PluginGui.h>

#include <GWCA/GameEntities/Agent.h>
#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/Managers/ChatMgr.h>
#include <GWCA/Utilities/Hook.h>

#include <imgui.h>

#include <cwchar>

DLLAPI ToolboxPlugin* ToolboxPluginInstance()
{
    static RawDialogs instance;
    return &instance;
}

namespace {
    GW::HookEntry raw_dialog_entry;
    GW::HookEntry open_chest_entry;

    constexpr uint32_t signpost_open_chest = 0x2;

    RawDialogs& Plugin()
    {
        return *static_cast<RawDialogs*>(ToolboxPluginInstance());
    }

    void SendDialog(const uint32_t dialog_id)
    {
        if (DirectDialog::Send(dialog_id)) {
            return;
        }
        if (CtoS::Send({CtoS::GetOpcodes().send_dialog, dialog_id})) {
            return;
        }
        GW::Agents::SendDialog(dialog_id);
    }

    void OpenChest()
    {
        const auto target = GW::Agents::GetTarget();
        if (!target || !target->GetIsGadgetType()) {
            return;
        }
        const auto& opcodes = CtoS::GetOpcodes();
        CtoS::Send({opcodes.interact_gadget, target->agent_id, 0});
        CtoS::Send({opcodes.send_signpost_dialog, signpost_open_chest});
    }

    void OnRawDialog(GW::HookStatus* status, const wchar_t*, const int argc, const LPWSTR* argv)
    {
        if (argc < 2) {
            status->blocked = false;
            return;
        }
        wchar_t* end = nullptr;
        const auto dialog_id = std::wcstoul(argv[1], &end, 0);
        if (end == argv[1] || *end != L'\0') {
            PluginChat::Write(L"RawDialog", L"Invalid dialog id. Example: /rawdialog 0x806501");
            return;
        }
        SendDialog(static_cast<uint32_t>(dialog_id));
    }

    void OnOpenChest(GW::HookStatus*, const wchar_t*, int, const LPWSTR*)
    {
        if (!Plugin().use_ctos) {
            PluginChat::Write(L"RawDialog", L"/openchest is not active. Enable it in the RawDialog plugin settings.");
            return;
        }
        if (!CtoS::Initialize()) {
            PluginChat::Write(L"RawDialog", L"Packet sending was not found for this game client; /openchest does not work.");
#ifndef NDEBUG
            PluginChat::Write(L"RawDialog", CtoS::Describe());
#endif
            return;
        }
        OpenChest();
    }
}

void RawDialogs::LoadSettings(const wchar_t* folder)
{
    SpeedrunPlugin::LoadSettings(folder);
    LoadSetting("useCtos", use_ctos);
}

void RawDialogs::SaveSettings(const wchar_t* folder)
{
    SaveSetting("useCtos", use_ctos);
    SpeedrunPlugin::SaveSettings(folder);
}

void RawDialogs::DrawPluginSettings()
{
    ImGui::Text("Example usage:");
    ImGui::Bullet();
    ImGui::Text("Send dialog in decimal notation: /rawdialog 8416257");
    ImGui::Bullet();
    ImGui::Text("Send dialog in hexadecimal notation: /rawdialog 0x806501");
    ImGui::Bullet();
    ImGui::Text("Open chest at range: /openchest");

    ImGui::Checkbox("Enable /openchest", &use_ctos);
    PluginGui::ShowHelp("Flags your account, use at your own risk.");
    if (use_ctos && !CtoS::Initialize()) {
        ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "Packet sending was not found for this game client; /openchest does not work.");
    }
}

void RawDialogs::Initialize(ImGuiContext* ctx, const ImGuiAllocFns allocator_fns, const HMODULE toolbox_dll)
{
    SpeedrunPlugin::Initialize(ctx, allocator_fns, toolbox_dll);
    GW::Chat::CreateCommand(&raw_dialog_entry, L"rawdialog", OnRawDialog);
    GW::Chat::CreateCommand(&open_chest_entry, L"openchest", OnOpenChest);
}

void RawDialogs::SignalTerminate()
{
    GW::Chat::DeleteCommand(&raw_dialog_entry, L"rawdialog");
    GW::Chat::DeleteCommand(&open_chest_entry, L"openchest");
    SpeedrunPlugin::SignalTerminate();
}
