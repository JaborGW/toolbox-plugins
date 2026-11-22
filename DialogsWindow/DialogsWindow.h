#pragma once

#include <ToolboxUIPlugin.h>
#include <SpeedrunPlugin.h>

#include <IconsFontAwesome5.h>

#include <vector>

class DialogsWindow : public SpeedrunPlugin<ToolboxUIPlugin> {
public:
    DialogsWindow()
    {
        can_close = true;
        show_menubutton = true;
    }
    ~DialogsWindow() override = default;

    const char* Name() const override { return "Dialogs"; }
    const char* Icon() const override { return ICON_FA_COMMENT_DOTS; }

    void Draw(IDirect3DDevice9* device) override;

    void LoadSettings(const wchar_t*) override;
    void SaveSettings(const wchar_t*) override;

protected:
    const char* Version() const override { return "1.4.0"; }
    bool HasEmptySettings() const override;
    std::vector<std::wstring> UnavailableFeatures() const override;
    void DrawPluginSettings() override;

private:
    void SendDialog(uint32_t dialog_id) const;

    static constexpr int default_favorite_count = 3;

    int fav_count = default_favorite_count;
    std::vector<int> fav_index = std::vector<int>(default_favorite_count, 0);

    bool show_foundry_reward = true;
    bool show_tower_of_strength = true;
    bool show_demon_assassin = true;
    bool show_four_horsemen = true;
    bool show_dhuum = true;

    bool show_uwteles = true;
    bool show_favorites = true;
    bool show_custom = true;
    bool use_function_ptr = false;

    char custom_dialog_buffer[64] = "";
};
