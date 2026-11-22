#pragma once

#include <ToolboxPlugin.h>

#include <IconsFontAwesome5.h>

class LeechSignetCancel : public ToolboxPlugin {
public:
    LeechSignetCancel() = default;
    ~LeechSignetCancel() override = default;

    const char* Name() const override { return "Leech Signet Cancel"; }
    const char* Icon() const override { return ICON_FA_TINT; }

    void Initialize(ImGuiContext* ctx, ImGuiAllocFns allocator_fns, HMODULE toolbox_dll) override;
    void SignalTerminate() override;
};
