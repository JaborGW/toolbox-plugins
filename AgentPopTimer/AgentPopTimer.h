#pragma once

#include <ToolboxUIPlugin.h>
#include <SpeedrunPlugin.h>
#include <RingTimer.h>

#include <IconsFontAwesome5.h>

class AgentPopTimer : public SpeedrunPlugin<ToolboxUIPlugin> {
public:
    AgentPopTimer();
    ~AgentPopTimer() override = default;

    const char* Name() const override { return "AgentPopTimer"; }
    const char* Icon() const override { return ICON_FA_RING; }

    void LoadSettings(const wchar_t*) override;
    void SaveSettings(const wchar_t*) override;
    void Draw(IDirect3DDevice9* device) override;

    void Initialize(ImGuiContext* ctx, ImGuiAllocFns allocator_fns, HMODULE toolbox_dll) override;
    void SignalTerminate() override;
    void Terminate() override;

protected:
    const char* Version() const override { return "1.2.0"; }
    bool HasEmptySettings() const override { return ring.HasDefaultSettings() && HasDefaultWindowSettings(); }
    void DrawPluginSettings() override { ring.DrawSettings(); }

private:
    RingTimer ring;
};
