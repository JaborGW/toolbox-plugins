#pragma once

#include <ToolboxPlugin.h>
#include <SpeedrunPlugin.h>

#include "Pathing.h"

#include <GWCA/Constants/Skills.h>

#include <IconsFontAwesome5.h>

#include <unordered_map>

class ShadowstepPredictor : public SpeedrunPlugin<ToolboxPlugin> {
public:
    ShadowstepPredictor() = default;
    ~ShadowstepPredictor() override = default;

    const char* Name() const override { return "Shadowstep Predictor"; }
    const char* Icon() const override { return ICON_FA_BEZIER_CURVE; }
    bool HasSettings() const override { return true; }
    bool* GetVisiblePtr() override { return &visible; }

    void Draw(IDirect3DDevice9* device) override;
    void Update(float) override;

    void LoadSettings(const wchar_t*) override;
    void SaveSettings(const wchar_t*) override;

    void Initialize(ImGuiContext* ctx, ImGuiAllocFns allocator_fns, HMODULE toolbox_dll) override;
    void SignalTerminate() override;

protected:
    const char* Version() const override { return "1.1.0"; }
    bool HasEmptySettings() const override;
    void DrawPluginSettings() override;

private:
    std::unordered_map<GW::Constants::SkillID, OutcomeChances> chances;

    PathPoint shadow_of_haste_location{};
    PathPoint shadow_walk_location{};
    GW::AgentID recall_target_id{};

    bool visible = true;
    ImVec4 success_color{0.f, 1.f, 0.f, 0.5f};
    ImVec4 failure_color{1.f, 0.f, 0.f, 0.67f};
    ImVec4 partial_color{1.f, 1.f, 0.f, 0.67f};

    bool show_recall = true;
    bool show_shadow_of_haste = true;
    bool show_shadow_walk = true;
    bool show_ally_shadow_steps = false;
    bool show_enemy_shadow_steps = false;
};
