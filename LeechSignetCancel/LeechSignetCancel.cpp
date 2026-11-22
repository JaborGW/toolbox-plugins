#include "LeechSignetCancel.h"

#include <GWCA/Constants/Skills.h>
#include <GWCA/GameEntities/Agent.h>
#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/Managers/UIMgr.h>
#include <GWCA/Packets/StoC.h>
#include <GWCA/Utilities/Hook.h>
#include <GWCA/stdlib/GameThreadMgr.h>
#include <GWCA/stdlib/StoCMgr.h>

DLLAPI ToolboxPlugin* ToolboxPluginInstance()
{
    static LeechSignetCancel instance;
    return &instance;
}

namespace {
    GW::HookEntry skill_activated_entry;
}

void LeechSignetCancel::Initialize(ImGuiContext* ctx, const ImGuiAllocFns allocator_fns, const HMODULE toolbox_dll)
{
    ToolboxPlugin::Initialize(ctx, allocator_fns, toolbox_dll);

    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::GenericValueTarget>(&skill_activated_entry, [](GW::HookStatus*, const GW::Packet::StoC::GenericValueTarget* packet) {
        if (packet->Value_id != GW::Packet::StoC::GenericValueID::skill_activated) {
            return;
        }
        if (static_cast<GW::Constants::SkillID>(packet->value) != GW::Constants::SkillID::Leech_Signet) {
            return;
        }
        const auto player = GW::Agents::GetControlledCharacter();
        if (player && player->skill) {
            GW::GameThread::Enqueue([] {
                GW::UI::Keypress(GW::UI::ControlAction_CancelAction);
            });
        }
    });
}

void LeechSignetCancel::SignalTerminate()
{
    ToolboxPlugin::SignalTerminate();
    GW::StoC::RemoveCallback<GW::Packet::StoC::GenericValueTarget>(&skill_activated_entry);
}
