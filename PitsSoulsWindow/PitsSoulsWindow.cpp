#include "PitsSoulsWindow.h"

#include <GWCA/Constants/AgentIDs.h>
#include <GWCA/GameContainers/Array.h>
#include <GWCA/GameContainers/GamePos.h>
#include <GWCA/GameEntities/Agent.h>
#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/Packets/StoC.h>
#include <GWCA/Utilities/Hook.h>
#include <GWCA/stdlib/StoCMgr.h>

#include <imgui.h>

#include <chrono>
#include <string>
#include <vector>

DLLAPI ToolboxPlugin* ToolboxPluginInstance()
{
    static PitsSoulsWindow instance;
    return &instance;
}

namespace {
    GW::HookEntry instance_load_entry;

    class PitsSoul {
    public:
        enum class State { Alive, Dead, Unknown };

        PitsSoul(const GW::GamePos pos, std::string name) : pos{pos}, name{std::move(name)} {}

        [[nodiscard]] std::string Print() const
        {
            switch (current_state) {
                case State::Alive:
                    return name + ": Death in " + std::to_string(static_cast<int>(103.6 * calculated_hp_percent)) + "s";
                case State::Dead:
                    return name + ": Respawn in " + std::to_string(static_cast<int>(seconds_until_respawn)) + "s";
                case State::Unknown:
                    return name + ": Unknown";
            }
            return name;
        }

        void Update(const GW::AgentLiving* agent, const std::chrono::steady_clock::time_point now)
        {
            if (!agent->GetIsAlive()) {
                return;
            }
            last_update_time = now;
            read_hp_percent = agent->hp;
            current_state = State::Alive;
        }

        void Update(const std::chrono::steady_clock::time_point now)
        {
            switch (current_state) {
                case State::Unknown:
                    return;
                case State::Alive: {
                    const auto ms_since_update = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update_time).count());
                    calculated_hp_percent = read_hp_percent - ms_since_update / 103'600;
                    if (calculated_hp_percent < 0) {
                        current_state = State::Dead;
                        death_time = now;
                    }
                    return;
                }
                case State::Dead: {
                    const auto ms_since_death = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(now - death_time).count());
                    seconds_until_respawn = 120.0 - ms_since_death / 1000;
                    if (seconds_until_respawn < 0) {
                        current_state = State::Alive;
                        read_hp_percent = 1;
                        last_update_time = now;
                    }
                    return;
                }
            }
        }

        GW::GamePos pos;

    private:
        std::string name;
        State current_state = State::Unknown;
        double calculated_hp_percent = 0;
        double read_hp_percent = 0;
        std::chrono::steady_clock::time_point last_update_time;
        std::chrono::steady_clock::time_point death_time;
        double seconds_until_respawn = 0;
    };

    std::vector<PitsSoul> souls;

    void ResetSouls()
    {
        souls = {
            PitsSoul(GW::Vec2f{11427.f, 5079.f}, "Bottom"),
            PitsSoul(GW::Vec2f{9245.f, 4898.f}, "Double"),
            PitsSoul(GW::Vec2f{10130.f, 6616.f}, "Reaper"),
        };
    }
}

void PitsSoulsWindow::Update(float)
{
    const auto agents = GW::Agents::GetAgentArray();
    if (!agents) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    for (auto& soul : souls) {
        soul.Update(now);
    }

    for (const auto agent : *agents) {
        const auto living = agent ? agent->GetAsAgentLiving() : nullptr;
        if (!living || living->player_number != GW::Constants::ModelID::UW::ChainedSoul || !living->GetIsAlive()) {
            continue;
        }
        for (auto& soul : souls) {
            if (GW::GetDistance(living->pos, soul.pos) < 50) {
                soul.Update(living, now);
            }
        }
    }
}

void PitsSoulsWindow::Draw(IDirect3DDevice9*)
{
    if (!GetVisiblePtr() || !*GetVisiblePtr()) {
        return;
    }

    const auto& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(100, 0), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(Name(), GetVisiblePtr(), GetWinFlags())) {
        for (const auto& soul : souls) {
            ImGui::Text("%s", soul.Print().c_str());
        }
    }
    ImGui::End();
}

void PitsSoulsWindow::Initialize(ImGuiContext* ctx, const ImGuiAllocFns allocator_fns, const HMODULE toolbox_dll)
{
    SpeedrunPlugin::Initialize(ctx, allocator_fns, toolbox_dll);
    ResetSouls();
    GW::StoC::RegisterPostPacketCallback<GW::Packet::StoC::InstanceLoadFile>(&instance_load_entry, [](GW::HookStatus*, const GW::Packet::StoC::InstanceLoadFile*) {
        ResetSouls();
    });
}

void PitsSoulsWindow::SignalTerminate()
{
    GW::StoC::RemoveCallback<GW::Packet::StoC::InstanceLoadFile>(&instance_load_entry);
    SpeedrunPlugin::SignalTerminate();
}
