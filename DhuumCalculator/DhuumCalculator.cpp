#include "DhuumCalculator.h"
#include "DhuumModel.h"
#include "DhuumDebugLog.h"

#include <InstanceProgress.h>
#include <PluginGui.h>

#include <GWCA/Constants/AgentIDs.h>
#include <GWCA/Constants/Constants.h>
#include <GWCA/Constants/Maps.h>
#include <GWCA/GameContainers/Array.h>
#include <GWCA/GameEntities/Agent.h>
#include <GWCA/Constants/Skills.h>
#include <GWCA/GameEntities/Skill.h>
#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/Managers/EffectMgr.h>
#include <GWCA/Managers/MapMgr.h>
#include <GWCA/Managers/PartyMgr.h>

#include <imgui.h>

#include <cmath>
#include <deque>
#include <format>
#include <mutex>
#include <optional>
#include <string>

DLLAPI ToolboxPlugin* ToolboxPluginInstance()
{
    static DhuumCalculator instance;
    return &instance;
}

namespace {
    constexpr double window_seconds = 50.0;
    constexpr double min_window_seconds = 5.0;
    constexpr double sample_interval_seconds = 0.25;

    struct Settings {
        bool show_dps = true;
        bool show_rest_rate = true;
        bool show_rest_finish = true;
        bool show_damage_finish = true;
        bool show_one_fury_switch = true;
        bool show_all_fury_switch = true;
        bool override_party = false;
        int ghosts = 7;
        bool quickening_zephyr = true;

        bool operator==(const Settings&) const = default;
    };
    Settings options;

    struct Snapshot {
        bool in_fight = false;
        double instance_time = 0;
        double rest = 0;
        std::optional<double> damage_left;
        std::optional<double> rest_rate;
        std::optional<double> dps;
        unsigned ghosts = 0;
        bool quickening_zephyr = false;
#ifndef NDEBUG
        unsigned party_players = 0;
        uint32_t dhuum_max_hp = 0;
        size_t rest_samples = 0;
        size_t hp_samples = 0;
#endif
    };
    std::mutex snapshot_mutex;
    Snapshot snapshot;

    std::deque<DhuumModel::Sample> rest_samples;
    std::deque<DhuumModel::Sample> hp_samples;

    bool quickening_zephyr_seen = false;

    void ResetHistory()
    {
        rest_samples.clear();
        hp_samples.clear();
        quickening_zephyr_seen = false;
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

    double DhuumMaxHp(const GW::AgentLiving* dhuum)
    {
        return dhuum->max_hp > 0 ? static_cast<double>(dhuum->max_hp) : DhuumModel::dhuum_max_hp;
    }

    void Push(std::deque<DhuumModel::Sample>& samples, const double time, const double value)
    {
        if (!samples.empty() && time - samples.back().time < sample_interval_seconds) {
            return;
        }
        samples.push_back({time, value});
        while (samples.size() > 2 && time - samples.front().time > window_seconds) {
            samples.pop_front();
        }
    }

    std::string FormatTime(const double seconds)
    {
        const auto total = static_cast<long long>(std::max(0.0, std::round(seconds)));
        return std::format("{}:{:02}", total / 60, total % 60);
    }

    std::string AtTime(const Snapshot& now, const double seconds_from_now)
    {
        return std::format("{} (in {})", FormatTime(now.instance_time + seconds_from_now), FormatTime(seconds_from_now));
    }

    void DrawSwitch(const char* label, const Snapshot& now, const unsigned switchers)
    {
        if (now.rest >= 1.0) {
            ImGui::Text("%s: rest done", label);
            return;
        }
        if (!now.rest_rate || *now.rest_rate <= 0 || !now.dps || !now.damage_left) {
            ImGui::Text("%s: waiting for rest and damage data", label);
            return;
        }
        if (switchers == 0) {
            ImGui::Text("%s: no ghosts", label);
            return;
        }
        const auto damage_left = std::max(0.0, *now.damage_left);
        const auto plan = DhuumModel::PlanSwitch(1.0 - now.rest, damage_left, *now.rest_rate, std::max(0.0, *now.dps), switchers, now.quickening_zephyr);
        switch (plan.kind) {
            case DhuumModel::Switch::Kind::At:
                ImGui::Text("%s: switch at %s, both finish %s", label, AtTime(now, plan.seconds_from_now).c_str(), FormatTime(now.instance_time + plan.rest_finish_from_now).c_str());
                break;
            case DhuumModel::Switch::Kind::Now:
                ImGui::Text("%s: switch now, rest %s, damage %s", label, FormatTime(now.instance_time + plan.rest_finish_from_now).c_str(), FormatTime(now.instance_time + plan.damage_finish_from_now).c_str());
                break;
            case DhuumModel::Switch::Kind::NotNeeded:
                ImGui::Text("%s: not needed, damage finishes first", label);
                break;
            case DhuumModel::Switch::Kind::RestWouldStall:
                ImGui::Text("%s: rest would stop progressing", label);
                break;
        }
    }
}

void DhuumCalculator::Update(float)
{
#ifndef NDEBUG
    DhuumDebugLog::Update();
#endif
    Snapshot next;
    const auto progress = GW::Map::GetMapID() == GW::Constants::MapID::The_Underworld ? InstanceProgress() : std::nullopt;
    next.instance_time = GW::Map::GetInstanceTime() / 1000.0;
    if (!progress || *progress <= 0.f) {
        ResetHistory();
        std::scoped_lock lock{snapshot_mutex};
        snapshot = next;
        return;
    }
    next.in_fight = true;
    next.rest = std::min(1.0, static_cast<double>(*progress));
    if (!rest_samples.empty() && next.instance_time < rest_samples.back().time) {
        ResetHistory();
    }

    Push(rest_samples, next.instance_time, next.rest);
    next.rest_rate = DhuumModel::Slope(rest_samples, min_window_seconds);

    if (const auto dhuum = FindDhuum()) {
        const auto max_hp = DhuumMaxHp(dhuum);
        next.damage_left = (dhuum->hp - DhuumModel::damage_target_fraction) * max_hp;
        Push(hp_samples, next.instance_time, dhuum->hp * max_hp);
#ifndef NDEBUG
        next.dhuum_max_hp = dhuum->max_hp;
#endif
    }
    if (const auto hp_slope = DhuumModel::Slope(hp_samples, min_window_seconds)) {
        next.dps = -*hp_slope;
    }

    const auto players = GW::PartyMgr::GetPartyPlayerCount();
    quickening_zephyr_seen = quickening_zephyr_seen || GW::Effects::GetPlayerEffectBySkillId(GW::Constants::SkillID::Quickening_Zephyr);
    const bool quickening_zephyr = quickening_zephyr_seen;
    next.quickening_zephyr = options.override_party ? options.quickening_zephyr : quickening_zephyr;
    next.ghosts = options.override_party ? static_cast<unsigned>(options.ghosts) : DhuumModel::GhostsForPlayers(players, quickening_zephyr);
#ifndef NDEBUG
    next.party_players = players;
    next.rest_samples = rest_samples.size();
    next.hp_samples = hp_samples.size();
#endif

    std::scoped_lock lock{snapshot_mutex};
    snapshot = next;
}

void DhuumCalculator::Draw(IDirect3DDevice9*)
{
    if (!GetVisiblePtr() || !*GetVisiblePtr()) {
        return;
    }
    Snapshot now;
    {
        std::scoped_lock lock{snapshot_mutex};
        now = snapshot;
    }

    ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(Name(), GetVisiblePtr(), GetWinFlags())) {
        if (!now.in_fight) {
            ImGui::Text("Waiting for Dhuum's rest to start");
        }
        else {
            if (options.show_dps) {
                if (!now.damage_left) {
                    ImGui::Text("DPS: Dhuum not found");
                }
                else if (now.dps) {
                    ImGui::Text("DPS: %.0f", *now.dps);
                }
                else {
                    ImGui::Text("DPS: measuring");
                }
            }
            if (options.show_rest_rate) {
                if (now.rest_rate) {
                    ImGui::Text("Rests/s: %.2f player rests", *now.rest_rate / DhuumModel::rest_per_ghost_cast);
                }
                else {
                    ImGui::Text("Rests/s: measuring");
                }
            }
            if (options.show_rest_finish) {
                if (now.rest >= 1.0) {
                    ImGui::Text("Rest: done");
                }
                else if (now.rest_rate && *now.rest_rate > 0) {
                    ImGui::Text("Rest finishes: %s", AtTime(now, (1.0 - now.rest) / *now.rest_rate).c_str());
                }
                else {
                    ImGui::Text("Rest finishes: measuring");
                }
            }
            if (options.show_damage_finish) {
                if (now.damage_left && *now.damage_left <= 0) {
                    ImGui::Text("Damage: done");
                }
                else if (now.damage_left && now.dps && *now.dps > 0) {
                    ImGui::Text("Damage finishes: %s", AtTime(now, *now.damage_left / *now.dps).c_str());
                }
                else {
                    ImGui::Text("Damage finishes: %s", now.damage_left ? "measuring" : "Dhuum not found");
                }
            }
            if (options.show_one_fury_switch) {
                DrawSwitch("One to fury", now, std::min(1u, now.ghosts));
            }
            if (options.show_all_fury_switch) {
                DrawSwitch(std::format("All {} to fury", now.ghosts).c_str(), now, now.ghosts);
            }
        }
#ifndef NDEBUG
        ImGui::Separator();
        ImGui::TextDisabled("rest %.4f, samples %zu/%zu, players %u, ghosts %u, QZ %d, max_hp %u", now.rest, now.rest_samples, now.hp_samples, now.party_players, now.ghosts, now.quickening_zephyr, now.dhuum_max_hp);
#endif
    }
    ImGui::End();
}

void DhuumCalculator::DrawPluginSettings()
{
    ImGui::Checkbox("Damage per second", &options.show_dps);
    ImGui::Checkbox("Rests per second", &options.show_rest_rate);
    PluginGui::ShowHelp("Reaper and ghost rests together, counted in player (ghost) rests.");
    ImGui::Checkbox("Rest finish time", &options.show_rest_finish);
    ImGui::Checkbox("Damage finish time", &options.show_damage_finish);
    ImGui::Checkbox("When one ghost should switch to Ghostly Fury", &options.show_one_fury_switch);
    ImGui::Checkbox("When all ghosts should switch to Ghostly Fury", &options.show_all_fury_switch);
    PluginGui::ShowHelp("Rates are measured over the last 50 seconds. Times are instance times.");
    ImGui::Checkbox("Set ghosts and Quickening Zephyr manually", &options.override_party);
    PluginGui::ShowHelp("Otherwise Quickening Zephyr counts as active from the first time your character has its effect until the fight ends. Ghosts are players - 1 with it, all players without it.");
    if (options.override_party) {
        ImGui::SliderInt("Ghosts", &options.ghosts, 0, 8);
        ImGui::Checkbox("Quickening Zephyr", &options.quickening_zephyr);
    }
}

bool DhuumCalculator::HasEmptySettings() const
{
    return HasDefaultWindowSettings() && options == Settings{};
}

void DhuumCalculator::LoadSettings(const wchar_t* folder)
{
    SpeedrunPlugin::LoadSettings(folder);
#ifndef NDEBUG
    DhuumDebugLog::SetFolder(folder);
#endif
    LoadSetting("show_dps", options.show_dps);
    LoadSetting("show_rest_rate", options.show_rest_rate);
    LoadSetting("show_rest_finish", options.show_rest_finish);
    LoadSetting("show_damage_finish", options.show_damage_finish);
    LoadSetting("show_one_fury_switch", options.show_one_fury_switch);
    LoadSetting("show_all_fury_switch", options.show_all_fury_switch);
    LoadSetting("override_party", options.override_party);
    LoadSetting("ghosts", options.ghosts);
    LoadSetting("quickening_zephyr", options.quickening_zephyr);
    options.ghosts = std::clamp(options.ghosts, 0, 8);
}

void DhuumCalculator::SaveSettings(const wchar_t* folder)
{
    SaveSetting("show_dps", options.show_dps);
    SaveSetting("show_rest_rate", options.show_rest_rate);
    SaveSetting("show_rest_finish", options.show_rest_finish);
    SaveSetting("show_damage_finish", options.show_damage_finish);
    SaveSetting("show_one_fury_switch", options.show_one_fury_switch);
    SaveSetting("show_all_fury_switch", options.show_all_fury_switch);
    SaveSetting("override_party", options.override_party);
    SaveSetting("ghosts", options.ghosts);
    SaveSetting("quickening_zephyr", options.quickening_zephyr);
    SpeedrunPlugin::SaveSettings(folder);
}

#ifndef NDEBUG
void DhuumCalculator::Initialize(ImGuiContext* ctx, const ImGuiAllocFns allocator_fns, const HMODULE toolbox_dll)
{
    SpeedrunPlugin::Initialize(ctx, allocator_fns, toolbox_dll);
    DhuumDebugLog::Initialize();
}

void DhuumCalculator::SignalTerminate()
{
    DhuumDebugLog::Terminate();
    SpeedrunPlugin::SignalTerminate();
}
#endif
