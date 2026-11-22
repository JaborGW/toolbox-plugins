#include "TargetEverything.h"

#include <GWCA/Constants/Constants.h>
#include <GWCA/GameEntities/Agent.h>
#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/Managers/ChatMgr.h>
#include <GWCA/Utilities/Hook.h>
#include <GWCA/Utilities/Hooker.h>

#include <PluginChat.h>

#include <atomic>
#include <format>

DLLAPI ToolboxPlugin* ToolboxPluginInstance()
{
    static TargetEverything instance;
    return &instance;
}

namespace {
    using GW::AgentTargetFlags;
    using GetAgentMatchesFlags_pt = bool(__cdecl*)(const GW::Agent* agent, AgentTargetFlags flags) noexcept;
    GetAgentMatchesFlags_pt GetAgentMatchesFlags_Func = nullptr;
    GetAgentMatchesFlags_pt GetAgentMatchesFlags_Ret = nullptr;
    int hook_status = -1;
    std::atomic<uint32_t> filter_calls = 0;
    std::atomic<uint32_t> overridden_rejections = 0;
    std::atomic<uint32_t> last_overridden_agent = 0;
    GW::HookEntry chat_command_entry;

    struct LivingCategory {
        AgentTargetFlags include;
        AgentTargetFlags exclude_dead;
        AgentTargetFlags exclude_alive;
    };

    LivingCategory CategoryOf(const GW::AgentLiving* living)
    {
        using enum AgentTargetFlags;
        if (living->IsPlayer()) {
            return {Accept_Player, Exclude_DeadAlly, Exclude_AliveAlly};
        }
        switch (living->allegiance) {
            case GW::Constants::Allegiance::Neutral:
                return {Include_Neutral, Exclude_DeadNeutral, Exclude_AliveNeutral};
            case GW::Constants::Allegiance::Enemy:
                return {Include_Enemy, Exclude_DeadEnemy, Exclude_AliveEnemy};
            case GW::Constants::Allegiance::Spirit_Pet:
                return {Include_SpiritPet, Exclude_DeadSpiritPet, Exclude_AliveSpiritPet};
            case GW::Constants::Allegiance::Minion:
                return {Include_Minion, Exclude_DeadMinion, Exclude_AliveMinion};
            case GW::Constants::Allegiance::Npc_Minipet:
                return {Include_NPCMinipet, Exclude_DeadNPCMinipet, Exclude_AliveNPCMinipet};
            case GW::Constants::Allegiance::Ally_NonAttackable:
                return {Include_Ally, Exclude_DeadAlly, Exclude_AliveAlly};
        }
        return {Include_Ally, Exclude_DeadAlly, Exclude_AliveAlly};
    }

    bool MatchesIgnoringTargetability(const GW::Agent* agent, const AgentTargetFlags flags)
    {
        if (const auto gadget = agent->GetAsAgentGadget()) {
            return (flags & AgentTargetFlags::Type_Gadget) && GW::Agents::GetAgentEncName(gadget);
        }
        const auto living = agent->GetAsAgentLiving();
        if (!living) {
            return false;
        }
        const auto category = CategoryOf(living);
        if (!(flags & category.include)) {
            return false;
        }
        return !(flags & (living->GetIsDead() ? category.exclude_dead : category.exclude_alive));
    }

    // The game's own filter says no for agents it deems untargetable; accept those as long as they fit the requested kind.
    bool __cdecl OnGetAgentMatchesFlags(const GW::Agent* agent, const AgentTargetFlags flags) noexcept
    {
        GW::Hook::EnterHook();
        ++filter_calls;
        auto matches = GetAgentMatchesFlags_Ret(agent, flags);
        if (!matches && agent && MatchesIgnoringTargetability(agent, flags)) {
            matches = true;
            ++overridden_rejections;
            last_overridden_agent = agent->agent_id;
        }
        GW::Hook::LeaveHook();
        return matches;
    }
}

void TargetEverything::Initialize(ImGuiContext* ctx, const ImGuiAllocFns allocator_fns, const HMODULE toolbox_dll)
{
    ToolboxPlugin::Initialize(ctx, allocator_fns, toolbox_dll);
    const auto gwca = GetModuleHandleW(L"gwca.dll");
    GetAgentMatchesFlags_Func = gwca ? reinterpret_cast<GetAgentMatchesFlags_pt>(GetProcAddress(gwca, "GW_Agents_GetAgentMatchesFlags")) : nullptr;
    if (GetAgentMatchesFlags_Func) {
        hook_status = GW::Hook::CreateHook(&GetAgentMatchesFlags_Func, &OnGetAgentMatchesFlags, &GetAgentMatchesFlags_Ret);
        GW::Hook::EnableHooks(GetAgentMatchesFlags_Func);
    }
    GW::Chat::CreateCommand(&chat_command_entry, L"targeteverything", [](GW::HookStatus*, const wchar_t*, int, const LPWSTR*) {
        PluginChat::Write(L"Target Everything", std::format(L"export found: {}, hook status: {}, filter calls: {}, overridden rejections: {}, last overridden agent: {}",
            GetAgentMatchesFlags_Func != nullptr, hook_status, filter_calls.load(), overridden_rejections.load(), last_overridden_agent.load()));
    });
}

void TargetEverything::SignalTerminate()
{
    ToolboxPlugin::SignalTerminate();
    GW::Chat::DeleteCommand(&chat_command_entry, L"targeteverything");
    if (GetAgentMatchesFlags_Func) {
        GW::Hook::DisableHooks(GetAgentMatchesFlags_Func);
        GW::Hook::RemoveHook(GetAgentMatchesFlags_Func);
    }
}
