#pragma once

#include <ToolboxPlugin.h>
#include <SpeedrunPlugin.h>

#include <IconsFontAwesome5.h>

class FollowPlugin : public SpeedrunPlugin<ToolboxPlugin> {
public:
    FollowPlugin() = default;
    ~FollowPlugin() override = default;

    const char* Name() const override { return "FollowPlugin"; }
    const char* Icon() const override { return ICON_FA_RUNNING; }

    void Update(float) override;

    void LoadSettings(const wchar_t*) override;
    void SaveSettings(const wchar_t*) override;
    bool HasSettings() const override { return true; }

    void Initialize(ImGuiContext* ctx, ImGuiAllocFns allocator_fns, HMODULE toolbox_dll) override;
    void SignalTerminate() override;

protected:
    const char* Version() const override { return "1.1.0"; }
    bool HasEmptySettings() const override { return follow_distance == default_follow_distance; }
    void DrawPluginSettings() override;

private:
    static constexpr float default_follow_distance = 200.f;
    float follow_distance = default_follow_distance;
};
