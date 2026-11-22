#pragma once

#include <ToolboxPlugin.h>
#include <SpeedrunPlugin.h>

#include <GWCA/GameEntities/Item.h>

#include <IconsFontAwesome5.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct InventoryItem {
    uint32_t model_id = 0;
    std::wstring encoded_name;
    std::vector<GW::ItemModifier> modifiers;
};

struct ItemChange {
    InventoryItem item;
    std::string model_file_id = "0x";
    bool enable_dyes = false;
    std::array<GW::DyeColor, 4> dyes{GW::DyeColor::None, GW::DyeColor::None, GW::DyeColor::None, GW::DyeColor::None};
    uint8_t tint = 255;
};

struct NpcTransmog {
    std::string name_to_apply;
    int npc_id = 0;
    int scale = 0;
    std::string npc_model_file_id = "0x";
    std::string npc_model_file_data = "0x";
    std::string flags = "0x";
};

struct MinipetTransmog {
    int item_to_replace_model_id = 0;
    int agent_to_replace_model_id = 0;
    std::string replacement_item_model_file_id = "0x";
    NpcTransmog npc_transmog;
};

class SkinChanger : public SpeedrunPlugin<ToolboxPlugin> {
public:
    SkinChanger() = default;
    ~SkinChanger() override = default;

    const char* Name() const override { return "SkinChanger"; }
    const char* Icon() const override { return ICON_FA_PEOPLE_ARROWS; }
    bool HasSettings() const override { return true; }

    void LoadSettings(const wchar_t*) override;
    void SaveSettings(const wchar_t*) override;

    void Initialize(ImGuiContext* ctx, ImGuiAllocFns allocator_fns, HMODULE toolbox_dll) override;
    void SignalTerminate() override;

    void Update(float) override;

protected:
    const char* Version() const override { return "1.2.0"; }
    bool HasEmptySettings() const override { return item_changes.empty() && minipet_transmogs.empty(); }
    void DrawPluginSettings() override;

private:
    std::vector<ItemChange> item_changes;
    std::vector<MinipetTransmog> minipet_transmogs;
};
