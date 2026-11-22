#pragma once

#include <ToolboxUIPlugin.h>
#include <SpeedrunPlugin.h>

#include <IconsFontAwesome5.h>

class FlatBowRangeIndicator : public SpeedrunPlugin<ToolboxUIPlugin> {
public:
    FlatBowRangeIndicator();
    ~FlatBowRangeIndicator() override = default;

    const char* Name() const override { return "FlatBowRangeIndicator"; }
    const char* Icon() const override { return ICON_FA_RAINBOW; }

    void Draw(IDirect3DDevice9* device) override;

protected:
    const char* Version() const override { return "1.4.0"; }
    bool HasEmptySettings() const override;
};
