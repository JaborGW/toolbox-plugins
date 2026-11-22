#pragma once

#include <ToolboxPlugin.h>
#include <SpeedrunPlugin.h>
#include <Hotkeys.h>

#include <IconsFontAwesome5.h>

class Slowload : public SpeedrunPlugin<ToolboxPlugin> {
public:
    Slowload() = default;
    ~Slowload() override = default;

    const char* Name() const override { return "Slowload"; }
    const char* Icon() const override { return ICON_FA_BED; }
    bool HasSettings() const override { return true; }

    bool WndProc(UINT message, WPARAM wparam, LPARAM lparam) override;

    void LoadSettings(const wchar_t*) override;
    void SaveSettings(const wchar_t*) override;

    void Initialize(ImGuiContext* ctx, ImGuiAllocFns allocator_fns, HMODULE toolbox_dll) override;
    void SignalTerminate() override;

protected:
    const char* Version() const override { return "1.2.0"; }
    bool HasEmptySettings() const override { return shortcut.key == 0; }
    void DrawPluginSettings() override;

private:
    Hotkeys::Hotkey shortcut;
};
