#include "ProjectileIndicator.h"

#include <GameIds.h>
#include <ModelNames.h>
#include <PluginUtils.h>
#include <WorldRenderer.h>

#include <GWCA/Constants/Constants.h>
#include <GWCA/GameContainers/GamePos.h>
#include <GWCA/GameEntities/Agent.h>
#include <GWCA/GameEntities/Skill.h>
#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/Managers/MapMgr.h>
#include <GWCA/Managers/SkillbarMgr.h>
#include <GWCA/Managers/UIMgr.h>
#include <GWCA/Packets/StoC.h>
#include <GWCA/Utilities/Hook.h>
#include <GWCA/stdlib/StoCMgr.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <sstream>
#include <string>
#include <unordered_map>

DLLAPI ToolboxPlugin* ToolboxPluginInstance()
{
    static ProjectileIndicator instance;
    return &instance;
}

namespace {
    GW::HookEntry projectile_entry;

    constexpr auto ellipse_a = 125.f;
    constexpr auto ellipse_b = 50.f;
    constexpr ImVec4 default_color = {.557f, 0.f, .627f, .353f};
    constexpr auto default_projectile_timer = 1000;
    constexpr auto default_tracked_skills = "3075 3074";
    const auto default_suppressed_sources = std::to_string(GameIds::ModelID::UW::DeadThresher);

    bool ShouldRender()
    {
        return GW::Map::GetInstanceType() == GW::Constants::InstanceType::Explorable && GW::Agents::GetControlledCharacter() && !GW::Agents::IsObserving();
    }

    std::string SkillName(const GW::Constants::SkillID id)
    {
        if (id == GW::Constants::SkillID::No_Skill) {
            return "Auto-Attack";
        }
        static std::unordered_map<GW::Constants::SkillID, std::array<wchar_t, 256>> decoded_names;
        if (const auto found = decoded_names.find(id); found != decoded_names.end()) {
            return PluginUtils::WStringToString(found->second.data());
        }
        const auto skill = GW::SkillbarMgr::GetSkillConstantData(id);
        if (!skill) {
            return "";
        }
        wchar_t encoded[8] = {};
        if (GW::UI::UInt32ToEncStr(skill->name, encoded, std::size(encoded))) {
            auto& buffer = decoded_names[id];
            buffer.fill(L'\0');
            GW::UI::AsyncDecodeStr(encoded, buffer.data(), buffer.size());
        }
        return "";
    }

    template <typename T>
    std::vector<T> ParseIds(const std::string& text)
    {
        std::vector<T> ids;
        std::istringstream stream(text);
        int id = 0;
        while (stream >> id) {
            ids.push_back(static_cast<T>(id));
        }
        return ids;
    }

    template <typename T>
    std::string FormatIds(const std::vector<T>& ids)
    {
        std::string text;
        for (const auto id : ids) {
            text += std::to_string(static_cast<int>(id)) + " ";
        }
        return text;
    }

    template <typename T, typename Label>
    void DrawIdList(std::vector<T>& ids, int& draw_id, Label&& label)
    {
        auto to_erase = ids.end();
        for (auto it = ids.begin(); it != ids.end(); ++it) {
            ImGui::PushID(draw_id++);
            ImGui::Bullet();
            if (ImGui::Button("X", ImVec2(20, 20))) {
                to_erase = it;
            }
            ImGui::SameLine();
            if (const auto text = label(*it); !text.empty()) {
                ImGui::TextUnformatted(text.data(), text.data() + text.size());
                ImGui::SameLine();
            }
            ImGui::PushItemWidth(100.f);
            ImGui::InputInt("", reinterpret_cast<int*>(&*it), 0);
            ImGui::PopItemWidth();
            ImGui::PopID();
        }
        if (to_erase != ids.end()) {
            ids.erase(to_erase);
        }
        ImGui::PushID(draw_id++);
        if (ImGui::Button("+")) {
            ids.push_back(T{});
        }
        ImGui::PopID();
    }

    std::string_view ModelName(const int model_id)
    {
        const auto& names = GetModelNames();
        const auto found = names.find(static_cast<uint16_t>(model_id));
        return found != names.end() ? found->second : std::string_view{};
    }
}

void ProjectileIndicator::Initialize(ImGuiContext* ctx, const ImGuiAllocFns allocator_fns, const HMODULE toolbox_dll)
{
    SpeedrunPlugin::Initialize(ctx, allocator_fns, toolbox_dll);

    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::AgentProjectileLaunched>(&projectile_entry, [this](GW::HookStatus* status, const GW::Packet::StoC::AgentProjectileLaunched* packet) {
        if (!ShouldRender()) {
            return;
        }
        const auto agent = GW::Agents::GetAgentByID(packet->agent_id);
        const auto living = agent ? agent->GetAsAgentLiving() : nullptr;
        if (!living) {
            return;
        }

        status->blocked = std::ranges::contains(suppressed_projectile_sources, living->player_number);

        if (living->allegiance != GW::Constants::Allegiance::Enemy || !std::ranges::contains(tracked_skills, static_cast<GW::Constants::SkillID>(living->skill))) {
            return;
        }
        if (!tracked_enemy_models.empty() && !std::ranges::contains(tracked_enemy_models, static_cast<int>(living->player_number))) {
            return;
        }
        const auto player = GW::Agents::GetControlledCharacter();
        const auto a_direction = GW::Normalize(GW::Vec2f{player->pos} - GW::Vec2f{living->pos});
        WorldRenderer::AddEllipse(GW::GamePos{packet->destination.x, packet->destination.y, 0}, a_direction, ellipse_a, ellipse_b, ImGui::ColorConvertFloat4ToU32(color), filled, projectile_timer);
    });
}

void ProjectileIndicator::SignalTerminate()
{
    GW::StoC::RemoveCallback<GW::Packet::StoC::AgentProjectileLaunched>(&projectile_entry);
    SpeedrunPlugin::SignalTerminate();
}

void ProjectileIndicator::Terminate()
{
    WorldRenderer::Terminate();
    SpeedrunPlugin::Terminate();
}

void ProjectileIndicator::Draw(IDirect3DDevice9* device)
{
    if (ShouldRender()) {
        WorldRenderer::Draw(device);
    }
    else {
        WorldRenderer::Clear();
    }
}

void ProjectileIndicator::DrawPluginSettings()
{
    ImGui::Checkbox("Fill circle", &filled);
    ImGui::ColorEdit4("Color", reinterpret_cast<float*>(&color));
    ImGui::SliderInt("Display duration (ms)", &projectile_timer, 100, 4000);

    auto draw_id = 0;
    ImGui::Text("Tracked skills:");
    DrawIdList(tracked_skills, draw_id, [](const GW::Constants::SkillID id) { return SkillName(id); });

    ImGui::Text("Tracked enemy models:");
    if (tracked_enemy_models.empty()) {
        ImGui::SameLine();
        ImGui::Text("All");
    }
    DrawIdList(tracked_enemy_models, draw_id, ModelName);

    ImGui::Text("Supress projectiles from agents with model ID:");
    DrawIdList(suppressed_projectile_sources, draw_id, ModelName);
}

bool ProjectileIndicator::HasEmptySettings() const
{
    return filled && projectile_timer == default_projectile_timer
        && color.x == default_color.x && color.y == default_color.y && color.z == default_color.z && color.w == default_color.w
        && FormatIds(tracked_skills) == FormatIds(ParseIds<GW::Constants::SkillID>(default_tracked_skills))
        && tracked_enemy_models.empty()
        && FormatIds(suppressed_projectile_sources) == FormatIds(ParseIds<int>(default_suppressed_sources));
}

void ProjectileIndicator::LoadSettings(const wchar_t* folder)
{
    SpeedrunPlugin::LoadSettings(folder);
    LoadSetting("colorx", color.x);
    LoadSetting("colory", color.y);
    LoadSetting("colorz", color.z);
    LoadSetting("colorw", color.w);
    LoadSetting("filled", filled);
    LoadSetting("projectileTimer", projectile_timer);

    std::string skills = default_tracked_skills;
    std::string models;
    std::string suppressed = default_suppressed_sources;
    LoadSetting("skills", skills);
    LoadSetting("models", models);
    LoadSetting("suppressed", suppressed);
    tracked_skills = ParseIds<GW::Constants::SkillID>(skills);
    tracked_enemy_models = ParseIds<int>(models);
    suppressed_projectile_sources = ParseIds<int>(suppressed);
}

void ProjectileIndicator::SaveSettings(const wchar_t* folder)
{
    SaveSetting("colorx", color.x);
    SaveSetting("colory", color.y);
    SaveSetting("colorz", color.z);
    SaveSetting("colorw", color.w);
    SaveSetting("filled", filled);
    SaveSetting("projectileTimer", projectile_timer);
    SaveSetting("skills", FormatIds(tracked_skills));
    SaveSetting("models", FormatIds(tracked_enemy_models));
    SaveSetting("suppressed", FormatIds(suppressed_projectile_sources));
    SpeedrunPlugin::SaveSettings(folder);
}
