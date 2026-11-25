#pragma once

#include <ToolboxUIPlugin.h>
#include <SpeedrunPlugin.h>

#include <IconsFontAwesome5.h>

class DhuumCalculator : public SpeedrunPlugin<ToolboxUIPlugin> {
public:
    DhuumCalculator() = default;
    ~DhuumCalculator() override = default;

    const char* Name() const override { return "DhuumCalculator"; }
    const char* Icon() const override { return ICON_FA_CALCULATOR; }

    void LoadSettings(const wchar_t*) override;
    void SaveSettings(const wchar_t*) override;
    void Update(float) override;
#ifndef NDEBUG
    void Initialize(ImGuiContext* ctx, ImGuiAllocFns allocator_fns, HMODULE toolbox_dll) override;
    void SignalTerminate() override;
#endif
    void Draw(IDirect3DDevice9* device) override;

protected:
    const char* Version() const override { return "2.0.0"; }
    bool HasEmptySettings() const override;
    void DrawPluginSettings() override;
};
