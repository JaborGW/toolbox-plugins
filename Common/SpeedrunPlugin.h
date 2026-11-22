#pragma once

#include <ToolboxPlugin.h>
#include <PluginUtils.h>

#include <PluginBackups.h>
#include <PluginChat.h>
#include <PluginFooter.h>

#include <Windows.h>
#include <shellapi.h>

#include <imgui.h>

#include <filesystem>
#include <string>

template <typename Base>
class SpeedrunPlugin : public Base {
public:
    void LoadSettings(const wchar_t* folder) override
    {
        settings_folder = folder;
        Base::LoadSettings(folder);
        std::error_code ec;
        if (!std::filesystem::exists(this->GetSettingFile(folder), ec) && !Backups().empty()) {
            PluginChat::Write(PluginUtils::StringToWString(this->Name()), L"No settings found, but there are backups. Restore one from this plugin's settings.");
        }
    }

    void SaveSettings(const wchar_t* folder) override
    {
        Base::SaveSettings(folder);
        PluginBackups::Save(this->GetSettingFile(folder), BackupFolder());
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

    virtual void DrawPluginSettings() {}

private:
    [[nodiscard]] std::filesystem::path BackupFolder() const
    {
        return std::filesystem::path(settings_folder) / L"backups" / PluginUtils::StringToWString(this->Name());
    }

    [[nodiscard]] std::vector<PluginBackups::Backup> Backups() const
    {
        return PluginBackups::List(BackupFolder(), this->GetSettingFile(settings_folder.c_str()).extension());
    }

    void DrawBackups()
    {
        if (settings_folder.empty() || !ImGui::TreeNodeEx("Backups", ImGuiTreeNodeFlags_SpanAvailWidth)) {
            return;
        }
        const auto backups = Backups();
        if (backups.empty()) {
            ImGui::TextDisabled("No backups yet. One is made whenever the settings are saved and have changed.");
        }
        for (const auto& backup : backups) {
            ImGui::PushID(backup.path.filename().string().c_str());
            if (ImGui::SmallButton("Restore")) {
                const auto settings_file = this->GetSettingFile(settings_folder.c_str());
                PluginBackups::Save(settings_file, BackupFolder());
                if (PluginBackups::Restore(backup, settings_file)) {
                    this->LoadSettings(settings_folder.c_str());
                    PluginChat::Write(PluginUtils::StringToWString(this->Name()), L"Restored backup from " + PluginUtils::StringToWString(PluginBackups::FormatLocalTime(backup.last_write)));
                }
            }
            ImGui::SameLine();
            ImGui::Text("%s  (%llu bytes)", PluginBackups::FormatLocalTime(backup.last_write).c_str(), static_cast<unsigned long long>(backup.size));
            ImGui::PopID();
        }
        if (ImGui::SmallButton("Open backup folder")) {
            std::error_code ec;
            std::filesystem::create_directories(BackupFolder(), ec);
            ShellExecuteW(nullptr, L"open", BackupFolder().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        ImGui::TreePop();
    }

    std::wstring settings_folder;
};
