#pragma once

#include <ToolboxUIPlugin.h>
#include <SpeedrunPlugin.h>

#include <GWCA/Constants/Skills.h>

#include <IconsFontAwesome5.h>

#include <vector>

class ProjectileIndicator : public SpeedrunPlugin<ToolboxUIPlugin> {
public:
    ProjectileIndicator() = default;
    ~ProjectileIndicator() override = default;

    const char* Name() const override { return "ProjectileIndicator"; }
    const char* Icon() const override { return ICON_FA_CIRCLE; }

    void Initialize(ImGuiContext* ctx, ImGuiAllocFns allocator_fns, HMODULE toolbox_dll) override;
    void SignalTerminate() override;
    void Terminate() override;

    void LoadSettings(const wchar_t*) override;
    void SaveSettings(const wchar_t*) override;

    void Draw(IDirect3DDevice9* device) override;

protected:
    const char* Version() const override { return "1.3.0"; }
    bool HasEmptySettings() const override;
    void DrawPluginSettings() override;

private:
    bool filled = true;
    ImVec4 color = {.557f, 0.f, .627f, .353f};
    int projectile_timer = 1000;
    std::vector<GW::Constants::SkillID> tracked_skills;
    std::vector<int> tracked_enemy_models;
    std::vector<int> suppressed_projectile_sources;
};
