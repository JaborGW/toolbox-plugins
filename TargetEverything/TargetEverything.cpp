#include "TargetEverything.h"

#include <GWCA/Constants/Constants.h>
#include <GWCA/GameEntities/Agent.h>
#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/GameContainers/Array.h>
#include <GWCA/GameContainers/GamePos.h>
#include <GWCA/Managers/ChatMgr.h>
#include <GWCA/Managers/GameThreadMgr.h>
#include <GWCA/Utilities/Hook.h>
#include <GWCA/Utilities/Hooker.h>

#include <PluginChat.h>

#include <atomic>
#include <cstring>
#include <format>
#include <string>

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
    bool hooked_implementation = false;
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

    // GWCA's own change-target check calls the implementation behind the export directly, so hook that instead of the export stub.
    GetAgentMatchesFlags_pt ResolveImplementation(const GetAgentMatchesFlags_pt export_stub)
    {
        constexpr uint8_t stub_prologue[] = {0x55, 0x89, 0xe5, 0x83, 0xec, 0x18, 0x8b, 0x45, 0x0c, 0x89, 0x44, 0x24, 0x04, 0x8b, 0x45, 0x08, 0x89, 0x04, 0x24, 0xe8};
        const auto bytes = reinterpret_cast<const uint8_t*>(export_stub);
        if (std::memcmp(bytes, stub_prologue, sizeof(stub_prologue)) != 0) {
            return export_stub;
        }
        int32_t relative;
        std::memcpy(&relative, bytes + sizeof(stub_prologue), sizeof(relative));
        hooked_implementation = true;
        const auto call_end = reinterpret_cast<uintptr_t>(bytes) + sizeof(stub_prologue) + sizeof(relative);
        return reinterpret_cast<GetAgentMatchesFlags_pt>(call_end + relative);
    }

    const GW::AgentLiving* NearestRejectedAgent()
    {
        const auto agents = GW::Agents::GetAgentArray();
        const auto me = GW::Agents::GetControlledCharacter();
        if (!agents || !me) {
            return nullptr;
        }
        const GW::AgentLiving* nearest = nullptr;
        auto nearest_distance = GW::Constants::SqrRange::Compass;
        for (const auto agent : *agents) {
            const auto living = agent ? agent->GetAsAgentLiving() : nullptr;
            if (!living || living == me || GetAgentMatchesFlags_Ret(living, GW::TargetFilter::AnyLiving)) {
                continue;
            }
            if (const auto distance = GW::GetSquareDistance(me->pos, living->pos); distance < nearest_distance) {
                nearest = living;
                nearest_distance = distance;
            }
        }
        return nearest;
    }

    void TryTargetNearestRejected()
    {
        GW::GameThread::Enqueue([](void*) {
            const auto agent = NearestRejectedAgent();
            if (!agent) {
                PluginChat::Write(L"Target Everything", L"No living agent in compass range that the game's filter rejects.");
                return;
            }
            const auto agent_id = agent->agent_id;
            const auto sent = GW::Agents::ChangeTarget(agent_id);
            PluginChat::Write(L"Target Everything", std::format(L"Nearest rejected agent: id {}, model {}, allegiance {}. ChangeTarget sent: {}, target now: {}",
                agent_id, agent->player_number, static_cast<int>(agent->allegiance), sent, GW::Agents::GetTargetId()));
        });
    }
}

void TargetEverything::Initialize(ImGuiContext* ctx, const ImGuiAllocFns allocator_fns, const HMODULE toolbox_dll)
{
    ToolboxPlugin::Initialize(ctx, allocator_fns, toolbox_dll);
    const auto gwca = GetModuleHandleW(L"gwca.dll");
    const auto export_stub = gwca ? reinterpret_cast<GetAgentMatchesFlags_pt>(GetProcAddress(gwca, "GW_Agents_GetAgentMatchesFlags")) : nullptr;
    GetAgentMatchesFlags_Func = export_stub ? ResolveImplementation(export_stub) : nullptr;
    if (GetAgentMatchesFlags_Func) {
        hook_status = GW::Hook::CreateHook(&GetAgentMatchesFlags_Func, &OnGetAgentMatchesFlags, &GetAgentMatchesFlags_Ret);
        GW::Hook::EnableHooks(GetAgentMatchesFlags_Func);
    }
    GW::Chat::CreateCommand(&chat_command_entry, L"targeteverything", [](GW::HookStatus*, const wchar_t*, const int argc, const LPWSTR* argv) {
        if (argc >= 2 && std::wstring(argv[1]) == L"nearest") {
            TryTargetNearestRejected();
            return;
        }
        PluginChat::Write(L"Target Everything", std::format(L"filter hook: {}, hooked implementation: {}, filter calls: {}, overridden rejections: {}, last overridden agent: {}",
            hook_status, hooked_implementation, filter_calls.load(), overridden_rejections.load(), last_overridden_agent.load()));
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
