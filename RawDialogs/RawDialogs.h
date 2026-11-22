#pragma once

#include <ToolboxPlugin.h>
#include <SpeedrunPlugin.h>

#include <IconsFontAwesome5.h>

class RawDialogs : public SpeedrunPlugin<ToolboxPlugin> {
public:
    RawDialogs() = default;
    ~RawDialogs() override = default;

    const char* Name() const override { return "RawDialog"; }
    const char* Icon() const override { return ICON_FA_DICE; }
    bool HasSettings() const override { return true; }

    void LoadSettings(const wchar_t*) override;
    void SaveSettings(const wchar_t*) override;

    void Initialize(ImGuiContext* ctx, ImGuiAllocFns allocator_fns, HMODULE toolbox_dll) override;
    void SignalTerminate() override;

    bool use_ctos = false;

protected:
    const char* Version() const override { return "1.3.0"; }
    bool HasEmptySettings() const override { return !use_ctos; }
    std::vector<std::wstring> UnavailableFeatures() const override;
    void DrawPluginSettings() override;
};
