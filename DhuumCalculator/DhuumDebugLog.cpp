#ifndef NDEBUG

#include "DhuumDebugLog.h"
#include "DhuumModel.h"

#include <InstanceProgress.h>

#include <GWCA/Constants/AgentIDs.h>
#include <GWCA/Constants/Constants.h>
#include <GWCA/Constants/Maps.h>
#include <GWCA/Constants/Skills.h>
#include <GWCA/GameContainers/Array.h>
#include <GWCA/GameEntities/Agent.h>
#include <GWCA/GameEntities/Skill.h>
#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/Managers/EffectMgr.h>
#include <GWCA/Managers/MapMgr.h>
#include <GWCA/Managers/PartyMgr.h>
#include <GWCA/Packets/StoC.h>
#include <GWCA/Utilities/Hook.h>
#include <GWCA/stdlib/StoCMgr.h>

#include <chrono>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_set>

namespace {
    using namespace GW::Packet::StoC;
    using SkillID = GW::Constants::SkillID;

    std::filesystem::path log_folder;
    std::ofstream file;
    GW::HookEntry value_entry;
    GW::HookEntry value_target_entry;
    GW::HookEntry modifier_entry;

    uint32_t dhuum_id = 0;
    std::optional<float> last_rest;
    std::optional<bool> last_quickening_zephyr;
    std::optional<uint32_t> last_players;
    std::optional<uint8_t> last_dhuum_allegiance;
    double last_hp_time = -1;
    std::unordered_set<uint32_t> described_agents;

    double Now()
    {
        return GW::Map::GetInstanceTime() / 1000.0;
    }

    template <typename... Args>
    void Write(std::format_string<Args...> format, Args&&... args)
    {
        if (!file.is_open()) {
            return;
        }
        file << std::format("{:.3f} ", Now()) << std::format(format, std::forward<Args>(args)...) << '\n';
        file.flush();
    }

    const char* SkillTag(const uint32_t skill)
    {
        switch (static_cast<SkillID>(skill)) {
            case SkillID::Dhuums_Rest:
                return "rest";
            case SkillID::Dhuums_Rest_Reaper_skill:
                return "reaper_rest";
            case SkillID::Ghostly_Fury:
                return "fury";
            case SkillID::Ghostly_Fury_Reaper_skill:
                return "reaper_fury";
            case SkillID::Quickening_Zephyr:
                return "qz";
            default:
                return "other";
        }
    }

    const char* ValueName(const uint32_t value_id)
    {
        switch (value_id) {
            case GenericValueID::skill_activated:
                return "skill_activated";
            case GenericValueID::instant_skill_activated:
                return "instant_skill_activated";
            case GenericValueID::skill_finished:
                return "skill_finished";
            case GenericValueID::skill_stopped:
                return "skill_stopped";
            case GenericValueID::interrupted:
                return "interrupted";
            case GenericValueID::casttime:
                return "casttime";
            case GenericValueID::skill_damage:
                return "skill_damage";
            case GenericValueID::damage:
                return "damage";
            case GenericValueID::critical:
                return "critical";
            case GenericValueID::armorignoring:
                return "armorignoring";
            default:
                return nullptr;
        }
    }

    void DescribeAgent(const uint32_t agent_id)
    {
        if (!file.is_open() || !described_agents.insert(agent_id).second) {
            return;
        }
        const auto agent = GW::Agents::GetAgentByID(agent_id);
        const auto living = agent ? agent->GetAsAgentLiving() : nullptr;
        if (!living) {
            Write("agent {} unknown", agent_id);
            return;
        }
        Write("agent {} model {} login {} allegiance {} primary {} type_map 0x{:X} player {}", agent_id, living->player_number, living->login_number, static_cast<int>(living->allegiance), static_cast<int>(living->primary), living->type_map, living->IsPlayer() ? 1 : 0);
    }

    void OnSkillValue(const uint32_t value_id, const uint32_t agent_id, const uint32_t value)
    {
        const auto name = ValueName(value_id);
        if (!name || !file.is_open()) {
            return;
        }
        DescribeAgent(agent_id);
        if (value_id == GenericValueID::skill_activated || value_id == GenericValueID::instant_skill_activated || value_id == GenericValueID::skill_damage) {
            Write("{} agent {} skill {} {}", name, agent_id, value, SkillTag(value));
        }
        else {
            Write("{} agent {} value {}", name, agent_id, value);
        }
    }

    void Open()
    {
        std::error_code error;
        std::filesystem::create_directories(log_folder, error);
        const auto stamp = std::format("{:%Y%m%d_%H%M%S}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
        file.open(log_folder / std::format("dhuum_{}.log", stamp));
        Write("open map {} dhuum {} model_dhuum {}", static_cast<uint32_t>(GW::Map::GetMapID()), dhuum_id, GW::Constants::ModelID::UW::Dhuum);
    }

    void Close()
    {
        if (file.is_open()) {
            Write("close");
            file.close();
        }
        last_rest.reset();
        last_quickening_zephyr.reset();
        last_players.reset();
        last_dhuum_allegiance.reset();
        last_hp_time = -1;
        described_agents.clear();
        dhuum_id = 0;
    }

    const GW::AgentLiving* FindDhuum()
    {
        const auto agents = GW::Agents::GetAgentArray();
        if (!agents) {
            return nullptr;
        }
        for (const auto agent : *agents) {
            const auto living = agent ? agent->GetAsAgentLiving() : nullptr;
            if (living && living->player_number == GW::Constants::ModelID::UW::Dhuum) {
                return living;
            }
        }
        return nullptr;
    }
}

namespace DhuumDebugLog {
    void Initialize()
    {
        GW::StoC::RegisterPacketCallback<GenericValue>(&value_entry, [](GW::HookStatus*, const GenericValue* packet) {
            OnSkillValue(packet->value_id, packet->agent_id, packet->value);
        });
        GW::StoC::RegisterPacketCallback<GenericValueTarget>(&value_target_entry, [](GW::HookStatus*, const GenericValueTarget* packet) {
            if (file.is_open() && ValueName(packet->Value_id)) {
                DescribeAgent(packet->caster);
                DescribeAgent(packet->target);
                Write("{}_target target {} caster {} skill {} {}", ValueName(packet->Value_id), packet->target, packet->caster, packet->value, SkillTag(packet->value));
            }
        });
        GW::StoC::RegisterPacketCallback<GenericModifier>(&modifier_entry, [](GW::HookStatus*, const GenericModifier* packet) {
            const auto name = ValueName(packet->type);
            if (!file.is_open() || !name || packet->target_id != dhuum_id) {
                return;
            }
            DescribeAgent(packet->cause_id);
            Write("{} target {} cause {} fraction {:.6f} hp {:.1f}", name, packet->target_id, packet->cause_id, packet->value, packet->value * DhuumModel::dhuum_max_hp);
        });
    }

    void SetFolder(const std::filesystem::path& folder)
    {
        log_folder = folder / "DhuumCalculator logs";
    }

    void Terminate()
    {
        GW::StoC::RemoveCallbacks(&value_entry);
        GW::StoC::RemoveCallbacks(&value_target_entry);
        GW::StoC::RemoveCallbacks(&modifier_entry);
        Close();
    }

    void Update()
    {
        if (GW::Map::GetMapID() != GW::Constants::MapID::The_Underworld || GW::Map::GetInstanceType() != GW::Constants::InstanceType::Explorable) {
            Close();
            return;
        }
        const auto dhuum = FindDhuum();
        if (!file.is_open()) {
            if (!dhuum || log_folder.empty()) {
                return;
            }
            dhuum_id = dhuum->agent_id;
            Open();
        }

        if (const auto rest = InstanceProgress(); rest != last_rest) {
            Write("rest {:.6f} delta {:.6f}", rest.value_or(-1.f), rest && last_rest ? *rest - *last_rest : 0.f);
            last_rest = rest;
        }
        const bool quickening_zephyr = GW::Effects::GetPlayerEffectBySkillId(SkillID::Quickening_Zephyr) != nullptr;
        if (quickening_zephyr != last_quickening_zephyr) {
            Write("qz_effect {}", quickening_zephyr ? 1 : 0);
            last_quickening_zephyr = quickening_zephyr;
        }
        const auto players = GW::PartyMgr::GetPartyPlayerCount();
        if (players != last_players) {
            Write("players {}", players);
            last_players = players;
        }
        if (dhuum) {
            dhuum_id = dhuum->agent_id;
            const auto allegiance = static_cast<uint8_t>(dhuum->allegiance);
            if (allegiance != last_dhuum_allegiance) {
                Write("dhuum_allegiance {}", allegiance);
                last_dhuum_allegiance = allegiance;
            }
            if (Now() - last_hp_time >= 1.0) {
                Write("dhuum_hp fraction {:.6f} max_hp {} hp {:.1f}", dhuum->hp, dhuum->max_hp, dhuum->hp * DhuumModel::dhuum_max_hp);
                last_hp_time = Now();
            }
        }
    }
}

#endif
