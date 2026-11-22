#pragma once

#include <ToolboxUIPlugin.h>
#include <SpeedrunPlugin.h>
#include <RingTimer.h>

#include <IconsFontAwesome5.h>

class DeathPenaltyTimer : public SpeedrunPlugin<ToolboxUIPlugin> {
public:
    DeathPenaltyTimer();
    ~DeathPenaltyTimer() override = default;

    const char* Name() const override { return "DeathPenaltyTimer"; }
    const char* Icon() const override { return ICON_FA_CIRCLE; }

    void LoadSettings(const wchar_t*) override;
    void SaveSettings(const wchar_t*) override;
    void Draw(IDirect3DDevice9* device) override;
    void Update(float) override;

    void Initialize(ImGuiContext* ctx, ImGuiAllocFns allocator_fns, HMODULE toolbox_dll) override;
    void SignalTerminate() override;
    void Terminate() override;

protected:
    const char* Version() const override { return "1.2.0"; }
    bool HasEmptySettings() const override;
    void DrawPluginSettings() override { ring.DrawSettings(); }

private:
    RingTimer ring;
};
