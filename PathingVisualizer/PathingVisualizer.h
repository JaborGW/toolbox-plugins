#pragma once

#include <ToolboxPlugin.h>
#include <SpeedrunPlugin.h>

#include <IconsFontAwesome5.h>

class PathingVisualizer : public SpeedrunPlugin<ToolboxPlugin> {
public:
    PathingVisualizer() = default;
    ~PathingVisualizer() override = default;

    const char* Name() const override { return "PathingVisualizer"; }
    const char* Icon() const override { return ICON_FA_ROUTE; }
    bool HasSettings() const override { return true; }
    bool* GetVisiblePtr() override { return &visible; }

    void LoadSettings(const wchar_t*) override;
    void SaveSettings(const wchar_t*) override;
    void Terminate() override;

    void Draw(IDirect3DDevice9* device) override;
    void Update(float) override;

protected:
    const char* Version() const override { return "1.1.0"; }
    bool HasEmptySettings() const override { return visible; }
    bool HasBackups() const override { return false; }

private:
    bool visible = true;
};
