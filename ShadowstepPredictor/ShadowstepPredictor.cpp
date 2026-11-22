#include "ShadowstepPredictor.h"

#include <GWCA/Constants/Constants.h>
#include <GWCA/GameEntities/Agent.h>
#include <GWCA/GameEntities/Skill.h>
#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/Managers/EffectMgr.h>
#include <GWCA/Managers/MapMgr.h>
#include <GWCA/Managers/SkillbarMgr.h>
#include <GWCA/Packets/StoC.h>
#include <GWCA/Utilities/Hook.h>
#include <GWCA/stdlib/StoCMgr.h>
#include <GWCA/stdlib/UIMgr.h>

#include <imgui.h>

#include <array>
#include <cmath>
#include <optional>
#include <string>

DLLAPI ToolboxPlugin* ToolboxPluginInstance()
{
    static ShadowstepPredictor instance;
    return &instance;
}

namespace {
    GW::HookEntry skill_activated_entry;
    GW::HookEntry generic_value_entry;
    GW::HookEntry instance_load_entry;

    const GW::PathingMapArray* path_map = nullptr;

    constexpr uint32_t recall_value_id = 20;
    constexpr uint32_t recall_value = 928;

    struct SkillSlotRect {
        ImVec2 top_left;
        ImVec2 size;
    };

    std::optional<std::array<SkillSlotRect, 8>> SkillbarSlots()
    {
        const auto skillbar_frame = GW::UI::GetFrameByLabel(L"Skillbar");
        if (!(skillbar_frame && skillbar_frame->IsVisible() && skillbar_frame->IsCreated())) {
            return std::nullopt;
        }
        const auto viewport = ImGui::GetMainViewport();
        std::array<SkillSlotRect, 8> slots{};
        for (uint32_t i = 0; i < slots.size(); i++) {
            const auto skill_frame = GW::UI::GetChildFrame(skillbar_frame, i);
            if (!skill_frame) {
                return std::nullopt;
            }
            const auto top_left = skill_frame->position.GetTopLeftOnScreen();
            const auto size = skill_frame->position.GetSizeOnScreen();
            if (std::isnan(top_left.x) || std::isnan(top_left.y) || std::isnan(size.x) || std::isnan(size.y)) {
                return std::nullopt;
            }
            slots[i] = {{top_left.x + viewport->Pos.x, top_left.y + viewport->Pos.y}, {size.x, size.y}};
        }
        return slots;
    }

    void DrawOverlay(const SkillSlotRect& slot, const OutcomeChances& chances, const ImVec4& success, const ImVec4& partial, const ImVec4& failure)
    {
        const auto draw_list = ImGui::GetBackgroundDrawList();
        const auto top = slot.top_left.y;
        const auto bottom = slot.top_left.y + slot.size.y;
        const auto success_start = slot.top_left.x;
        const auto partial_start = success_start + chances.success * slot.size.x;
        const auto failure_start = partial_start + chances.partial * slot.size.x;
        const auto failure_end = failure_start + chances.failure * slot.size.x;

        if (chances.success > 0.f) {
            draw_list->AddRectFilled({success_start, top}, {partial_start, bottom}, ImGui::ColorConvertFloat4ToU32(success));
        }
        if (chances.partial > 0.f) {
            draw_list->AddRectFilled({partial_start, top}, {failure_start, bottom}, ImGui::ColorConvertFloat4ToU32(partial));
        }
        if (chances.failure > 0.f) {
            draw_list->AddRectFilled({failure_start, top}, {failure_end, bottom}, ImGui::ColorConvertFloat4ToU32(failure));
        }
    }

    bool IsInSpellcastRange(const GW::AgentLiving* player, const GW::Agent* target)
    {
        return GW::GetSquareDistance(player->pos, target->pos) < GW::Constants::SqrRange::Spellcast;
    }
}

void ShadowstepPredictor::Update(float)
{
    const auto player = GW::Agents::GetControlledCharacter();
    if (!player || GW::Map::GetInstanceType() != GW::Constants::InstanceType::Explorable) {
        chances.clear();
        return;
    }
    if (!path_map) {
        path_map = GW::Map::GetPathingMap();
    }
    if (!path_map) {
        chances.clear();
        return;
    }

    const auto player_point = PathPoint{player->pos, FindTrapezoid(player->pos, path_map)};

    const auto check_effect = [&](const GW::Constants::SkillID id, const PathPoint& return_spot) {
        if (GW::Effects::GetPlayerEffectBySkillId(id)) {
            chances[id] = ShadowOfHastePrediction(player_point, return_spot);
        }
        else {
            chances.erase(id);
        }
    };
    if (show_shadow_of_haste) {
        check_effect(GW::Constants::SkillID::Shadow_of_Haste, shadow_of_haste_location);
    }
    if (show_shadow_walk) {
        check_effect(GW::Constants::SkillID::Shadow_Walk, shadow_walk_location);
    }
    const auto recall_active = GW::Effects::GetPlayerEffectBySkillId(GW::Constants::SkillID::Recall) != nullptr;
    if (show_recall && recall_active) {
        chances[GW::Constants::SkillID::Recall] = DeathsChargePrediction(player_point, GW::Agents::GetAgentByID(recall_target_id), path_map);
    }
    else {
        chances.erase(GW::Constants::SkillID::Recall);
    }

    const auto target = GW::Agents::GetTarget();
    const auto target_living = target ? target->GetAsAgentLiving() : nullptr;
    std::optional<OutcomeChances> jump_chances;
    const auto check_jump = [&](const GW::Constants::SkillID id, const bool target_is_enemy) {
        const auto can_jump = target_living && (target_living->allegiance == GW::Constants::Allegiance::Enemy) == target_is_enemy && IsInSpellcastRange(player, target);
        if (!can_jump || GW::SkillbarMgr::GetSkillSlot(id) == -1) {
            chances.erase(id);
            return;
        }
        if (!jump_chances) {
            jump_chances = DeathsChargePrediction(player_point, target, path_map);
        }
        chances[id] = *jump_chances;
    };

    if (show_ally_shadow_steps) {
        check_jump(GW::Constants::SkillID::Ebon_Escape, false);
        if (!show_recall || !recall_active) {
            check_jump(GW::Constants::SkillID::Recall, false);
        }
        check_jump(GW::Constants::SkillID::Deaths_Retreat, false);
        check_jump(GW::Constants::SkillID::Return, false);
    }

    if (show_enemy_shadow_steps) {
        check_jump(GW::Constants::SkillID::Deaths_Charge, true);
        check_jump(GW::Constants::SkillID::Dark_Prison, true);
        check_jump(GW::Constants::SkillID::Scorpion_Wire, true);
        check_jump(GW::Constants::SkillID::Shadow_Fang, true);
        check_jump(GW::Constants::SkillID::Shadow_Prison, true);
        check_jump(GW::Constants::SkillID::Shadow_Theft, true);
        check_jump(GW::Constants::SkillID::Wastrels_Collapse, true);
        if (!GW::Effects::GetPlayerEffectBySkillId(GW::Constants::SkillID::Shadow_Walk)) {
            check_jump(GW::Constants::SkillID::Shadow_Walk, true);
        }
    }
}

void ShadowstepPredictor::Draw(IDirect3DDevice9*)
{
    if (chances.empty()) {
        return;
    }
    const auto skillbar = GW::SkillbarMgr::GetPlayerSkillbar();
    const auto slots = SkillbarSlots();
    if (!skillbar || !slots) {
        return;
    }
    for (size_t i = 0; i < slots->size(); i++) {
        if (const auto found = chances.find(skillbar->skills[i].skill_id); found != chances.end()) {
            DrawOverlay((*slots)[i], found->second, success_color, partial_color, failure_color);
        }
    }
}

void ShadowstepPredictor::DrawPluginSettings()
{
    auto changed = false;
    ImGui::Text("Show:");
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Recall", &show_recall);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Shadow of Haste", &show_shadow_of_haste);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Shadow Walk", &show_shadow_walk);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Ally Shadow Steps", &show_ally_shadow_steps);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Enemy Shadow Steps", &show_enemy_shadow_steps);
    if (changed) {
        chances.clear();
    }

    ImGui::ColorEdit4("Success", &success_color.x);
    ImGui::ColorEdit4("Partial", &partial_color.x);
    ImGui::ColorEdit4("Failure", &failure_color.x);
}

std::vector<std::wstring> ShadowstepPredictor::UnavailableFeatures() const
{
    if (IsPathingAvailable()) {
        return {};
    }
    return {L"pathing checks in the predictions"};
}

bool ShadowstepPredictor::HasEmptySettings() const
{
    constexpr auto same = [](const ImVec4& a, const ImVec4& b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; };
    return visible && show_recall && show_shadow_of_haste && show_shadow_walk && !show_ally_shadow_steps && !show_enemy_shadow_steps
        && same(success_color, {0.f, 1.f, 0.f, 0.5f}) && same(failure_color, {1.f, 0.f, 0.f, 0.67f}) && same(partial_color, {1.f, 1.f, 0.f, 0.67f});
}

void ShadowstepPredictor::LoadSettings(const wchar_t* folder)
{
    SpeedrunPlugin::LoadSettings(folder);
    const auto load_color = [this](ImVec4& color, const std::string& name) {
        LoadSetting((name + "x").c_str(), color.x);
        LoadSetting((name + "y").c_str(), color.y);
        LoadSetting((name + "z").c_str(), color.z);
        LoadSetting((name + "w").c_str(), color.w);
    };
    LoadSetting("visible", visible);
    load_color(success_color, "successColor");
    load_color(partial_color, "partialColor");
    load_color(failure_color, "failureColor");
    LoadSetting("showRecall", show_recall);
    LoadSetting("showSoH", show_shadow_of_haste);
    LoadSetting("showShadowWalk", show_shadow_walk);
    LoadSetting("showAllyShadowSteps", show_ally_shadow_steps);
    LoadSetting("showEnemyShadowSteps", show_enemy_shadow_steps);
}

void ShadowstepPredictor::SaveSettings(const wchar_t* folder)
{
    const auto save_color = [this](const ImVec4& color, const std::string& name) {
        SaveSetting((name + "x").c_str(), color.x);
        SaveSetting((name + "y").c_str(), color.y);
        SaveSetting((name + "z").c_str(), color.z);
        SaveSetting((name + "w").c_str(), color.w);
    };
    SaveSetting("visible", visible);
    save_color(success_color, "successColor");
    save_color(partial_color, "partialColor");
    save_color(failure_color, "failureColor");
    SaveSetting("showRecall", show_recall);
    SaveSetting("showSoH", show_shadow_of_haste);
    SaveSetting("showShadowWalk", show_shadow_walk);
    SaveSetting("showAllyShadowSteps", show_ally_shadow_steps);
    SaveSetting("showEnemyShadowSteps", show_enemy_shadow_steps);
    SpeedrunPlugin::SaveSettings(folder);
}

void ShadowstepPredictor::Initialize(ImGuiContext* ctx, const ImGuiAllocFns allocator_fns, const HMODULE toolbox_dll)
{
    SpeedrunPlugin::Initialize(ctx, allocator_fns, toolbox_dll);
    InitializePathing();

    GW::UI::RegisterUIMessageCallback(&skill_activated_entry, GW::UI::UIMessage::kSkillActivated, [this](GW::HookStatus*, GW::UI::UIMessage, void* wparam, void*) {
        struct Payload {
            uint32_t agent_id;
            GW::Constants::SkillID skill_id;
        };
        const auto payload = *static_cast<const Payload*>(wparam);
        const auto player = GW::Agents::GetControlledCharacter();
        if (!player || payload.agent_id != player->agent_id || !path_map) {
            return;
        }
        if (payload.skill_id == GW::Constants::SkillID::Shadow_of_Haste) {
            shadow_of_haste_location = {player->pos, FindTrapezoid(player->pos, path_map)};
        }
        else if (payload.skill_id == GW::Constants::SkillID::Shadow_Walk) {
            shadow_walk_location = {player->pos, FindTrapezoid(player->pos, path_map)};
        }
    });

    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::InstanceLoadFile>(&instance_load_entry, [this](GW::HookStatus*, const GW::Packet::StoC::InstanceLoadFile*) {
        chances.clear();
        shadow_of_haste_location = {};
        shadow_walk_location = {};
        recall_target_id = 0;
        path_map = nullptr;
    });

    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::GenericValueTarget>(&generic_value_entry, [this](GW::HookStatus*, const GW::Packet::StoC::GenericValueTarget* packet) {
        if (packet->Value_id == recall_value_id && packet->caster == GW::Agents::GetControlledCharacterId() && packet->value == recall_value) {
            recall_target_id = packet->target;
        }
    });
}

void ShadowstepPredictor::SignalTerminate()
{
    GW::StoC::RemoveCallbacks(&instance_load_entry);
    GW::StoC::RemoveCallbacks(&generic_value_entry);
    GW::UI::RemoveUIMessageCallback(&skill_activated_entry, GW::UI::UIMessage::kSkillActivated);
    SpeedrunPlugin::SignalTerminate();
}
