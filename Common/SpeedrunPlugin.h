#pragma once

#include <ToolboxPlugin.h>
#include <ToolboxUIPlugin.h>
#include <PluginUtils.h>

#include <PluginBackups.h>
#include <PluginChat.h>
#include <PluginFooter.h>

#include <GWCA/Managers/ChatMgr.h>
#include <GWCA/Managers/GameThreadMgr.h>
#include <GWCA/Utilities/Hook.h>

#include <Windows.h>
#include <shellapi.h>

#include <imgui.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <format>
#include <string>
#include <type_traits>
#include <vector>

template <typename Base>
class SpeedrunPlugin : public Base {
public:
    void Initialize(ImGuiContext* ctx, ImGuiAllocFns allocator_fns, HMODULE toolbox_dll) override
    {
        Base::Initialize(ctx, allocator_fns, toolbox_dll);
        if constexpr (is_window) {
            default_window_settings = CurrentWindowSettings();
        }
        instance = this;
        GW::Chat::CreateCommand(&restore_command_entry, L"restore", OnRestoreCommand);
    }

    void SignalTerminate() override
    {
        GW::Chat::DeleteCommand(&restore_command_entry, L"restore");
        instance = nullptr;
        Base::SignalTerminate();
    }

    void LoadSettings(const wchar_t* folder) override
    {
        settings_folder = folder;
        Base::LoadSettings(folder);
        GW::GameThread::Enqueue([](void* context) {
            const auto plugin = static_cast<SpeedrunPlugin*>(context);
            plugin->AnnounceRestorableBackups();
            plugin->ReportUnavailableFeatures();
        }, this);
    }

    void SaveSettings(const wchar_t* folder) override
    {
        Base::SaveSettings(folder);
        if (HasBackups() && !HasEmptySettings()) {
            PluginBackups::Save(this->GetSettingFile(folder), BackupFolder());
        }
    }

    void DrawSettings() final
    {
        Base::DrawSettings();
        DrawPluginSettings();
        DrawBackups();
        PluginFooter::Draw(Version());
    }

protected:
    [[nodiscard]] virtual const char* Version() const = 0;

    [[nodiscard]] virtual bool HasEmptySettings() const = 0;

    virtual void DrawPluginSettings() {}

    [[nodiscard]] virtual const wchar_t* CommandAlias() const { return nullptr; }

    [[nodiscard]] virtual bool HasBackups() const { return true; }

    [[nodiscard]] virtual std::vector<std::wstring> UnavailableFeatures() const { return {}; }

    [[nodiscard]] bool HasDefaultWindowSettings() const
        requires std::is_base_of_v<ToolboxUIPlugin, Base>
    {
        return CurrentWindowSettings() == default_window_settings;
    }

private:
    static constexpr bool is_window = std::is_base_of_v<ToolboxUIPlugin, Base>;

    struct WindowSettings {
        bool visible = true;
        bool lock_move = false;
        bool lock_size = false;
        bool show_closebutton = false;
        bool show_menubutton = false;
        bool show_title = false;
        bool operator==(const WindowSettings&) const = default;
    };

    [[nodiscard]] WindowSettings CurrentWindowSettings() const
        requires is_window
    {
        const auto visible = const_cast<SpeedrunPlugin*>(this)->GetVisiblePtr();
        return {!visible || *visible, this->lock_move, this->lock_size, this->show_closebutton, this->show_menubutton, this->show_title};
    }

    [[nodiscard]] std::wstring CommandName() const
    {
        auto name = PluginUtils::ToLower(PluginUtils::StringToWString(this->Name()));
        std::erase_if(name, [](const wchar_t c) { return std::iswspace(c); });
        return name;
    }

    void Print(const std::wstring& message) const
    {
        PluginChat::Write(PluginUtils::StringToWString(this->Name()), message);
    }

    [[nodiscard]] std::filesystem::path BackupFolder() const
    {
        return std::filesystem::path(settings_folder) / L"backups" / PluginUtils::StringToWString(this->Name());
    }

    [[nodiscard]] std::vector<PluginBackups::Backup> Backups() const
    {
        return PluginBackups::List(BackupFolder(), this->GetSettingFile(settings_folder.c_str()).extension());
    }

    [[nodiscard]] static std::wstring Describe(const PluginBackups::Backup& backup)
    {
        return std::format(L"Backup {}, last change {}, file size {}", backup.path.stem().wstring(),
            PluginUtils::StringToWString(PluginBackups::FormatLocalTime(backup.last_write)), backup.size);
    }

    void ReportUnavailableFeatures()
    {
        if (reported_unavailable_features) {
            return;
        }
        reported_unavailable_features = true;
        PluginChat::ReportUnavailable(PluginUtils::StringToWString(this->Name()), UnavailableFeatures());
    }

    void AnnounceRestorableBackups() const
    {
        if (instance == this && HasBackups() && HasEmptySettings() && !Backups().empty()) {
            Print(L"Settings are empty, but there are backups. Type \"/restore\" to restore the most recent one.");
        }
    }

    void Restore(const PluginBackups::Backup& backup)
    {
        const auto settings_file = this->GetSettingFile(settings_folder.c_str());
        if (!HasEmptySettings()) {
            PluginBackups::Save(settings_file, BackupFolder());
        }
        if (!PluginBackups::Restore(backup, settings_file)) {
            Print(L"Failed to restore " + Describe(backup));
            return;
        }
        this->LoadSettings(settings_folder.c_str());
        Print(L"Restored " + Describe(backup));
    }

    void PrintHelp() const
    {
        const auto name = CommandName();
        Print(std::format(L"Type \"/restore {} recent\" to restore the most recent backup", name));
        Print(std::format(L"Type \"/restore {} largest\" to restore the largest backup", name));
        Print(std::format(L"Type \"/restore {} list\" to show the available backups", name));
        Print(std::format(L"Type \"/restore {} $NUMBER\" to restore a specific backup", name));
        Print(std::format(L"Type \"/restore {} help\" to show this menu", name));
        Print(L"Type \"/restore\" to restore the most recent backup of every plugin whose settings are empty");
    }

    bool HandleRestoreCommand(const int argc, const LPWSTR* argv)
    {
        if (!HasBackups()) {
            return false;
        }
        const auto backups = Backups();
        if (argc < 2) {
            if (HasEmptySettings() && !backups.empty()) {
                Restore(backups.front());
            }
            return false;
        }
        const auto target = PluginUtils::ToLower(std::wstring(argv[1]));
        const auto alias = CommandAlias();
        if (target != CommandName() && !(alias && target == PluginUtils::ToLower(std::wstring(alias)))) {
            return false;
        }

        const auto option = argc < 3 ? std::wstring(L"recent") : PluginUtils::ToLower(std::wstring(argv[2]));
        if (option == L"help") {
            PrintHelp();
            return true;
        }
        if (option == L"list") {
            Print(backups.empty() ? L"No backups available." : L"Available backups:");
            for (const auto& backup : backups) {
                Print(Describe(backup));
            }
            return true;
        }
        if (backups.empty()) {
            Print(L"No backups available.");
            return true;
        }
        if (option == L"recent") {
            Restore(backups.front());
            return true;
        }
        if (option == L"largest") {
            Restore(*std::ranges::max_element(backups, {}, &PluginBackups::Backup::size));
            return true;
        }
        const auto found = std::ranges::find(backups, option, [](const PluginBackups::Backup& backup) { return backup.path.stem().wstring(); });
        if (found == backups.end()) {
            Print(L"Unknown backup or option \"" + option + L"\".");
            PrintHelp();
            return true;
        }
        Restore(*found);
        return true;
    }

    static void __cdecl OnRestoreCommand(GW::HookStatus* status, const wchar_t*, const int argc, const LPWSTR* argv)
    {
        if (!instance || !instance->HandleRestoreCommand(argc, argv)) {
            status->blocked = false;
        }
    }

    void DrawBackups()
    {
        if (!HasBackups() || settings_folder.empty() || !ImGui::TreeNodeEx("Backups", ImGuiTreeNodeFlags_SpanAvailWidth)) {
            return;
        }
        ImGui::TextDisabled("Also available in chat: /restore %s help", PluginUtils::WStringToString(CommandName()).c_str());
        const auto backups = Backups();
        if (backups.empty()) {
            ImGui::TextDisabled("No backups yet. One is made whenever the settings are saved and have changed.");
        }
        for (const auto& backup : backups) {
            const auto id = backup.path.filename().string();
            ImGui::PushID(id.c_str());
            if (ImGui::SmallButton("Restore")) {
                Restore(backup);
            }
            ImGui::SameLine();
            ImGui::Text("%s", PluginUtils::WStringToString(Describe(backup)).c_str());
            ImGui::PopID();
        }
        if (ImGui::SmallButton("Open backup folder")) {
            std::error_code ec;
            std::filesystem::create_directories(BackupFolder(), ec);
            ShellExecuteW(nullptr, L"open", BackupFolder().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        ImGui::TreePop();
    }

    inline static GW::HookEntry restore_command_entry;
    inline static SpeedrunPlugin* instance = nullptr;
    std::wstring settings_folder;
    bool reported_unavailable_features = false;
    WindowSettings default_window_settings;
};
