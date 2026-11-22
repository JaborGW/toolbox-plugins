#pragma once

#include <ToolboxUIPlugin.h>
#include <SpeedrunPlugin.h>

#include <IconsFontAwesome5.h>

class PitsSoulsWindow : public SpeedrunPlugin<ToolboxUIPlugin> {
public:
    PitsSoulsWindow() = default;
    ~PitsSoulsWindow() override = default;

    const char* Name() const override { return "PitsSouls"; }
    const char* Icon() const override { return ICON_FA_GHOST; }

    void Update(float) override;
    void Draw(IDirect3DDevice9* device) override;

    void Initialize(ImGuiContext* ctx, ImGuiAllocFns allocator_fns, HMODULE toolbox_dll) override;
    void SignalTerminate() override;

protected:
    const char* Version() const override { return "1.2.0"; }
    bool HasEmptySettings() const override { return HasDefaultWindowSettings(); }
};
