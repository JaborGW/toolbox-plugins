#include "DialogsWindow.h"

#include <DirectDialog.h>
#include <PluginGui.h>

#include <GWCA/Constants/Constants.h>
#include <GWCA/Constants/QuestIDs.h>
#include <GWCA/Managers/AgentMgr.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>

DLLAPI ToolboxPlugin* ToolboxPluginInstance()
{
    static DialogsWindow instance;
    return &instance;
}

namespace {
    using GW::Constants::QuestID;
    namespace DialogID = GW::Constants::DialogID;

    struct NamedQuest {
        const char* name;
        QuestID id;
    };

    constexpr std::array quests = {
        NamedQuest{"UW - Chamber", QuestID::UW_Chamber},
        NamedQuest{"UW - Wastes", QuestID::UW_Wastes},
        NamedQuest{"UW - UWG", QuestID::UW_UWG},
        NamedQuest{"UW - Mnt", QuestID::UW_Mnt},
        NamedQuest{"UW - Pits", QuestID::UW_Pits},
        NamedQuest{"UW - Planes", QuestID::UW_Planes},
        NamedQuest{"UW - Pools", QuestID::UW_Pools},
        NamedQuest{"UW - Escort", QuestID::UW_Escort},
        NamedQuest{"UW - Restore", QuestID::UW_Restore},
        NamedQuest{"UW - Vale", QuestID::UW_Vale},
        NamedQuest{"FoW - Defend", QuestID::Fow_Defend},
        NamedQuest{"FoW - Army Of Darkness", QuestID::Fow_ArmyOfDarknesses},
        NamedQuest{"FoW - WailingLord", QuestID::Fow_WailingLord},
        NamedQuest{"FoW - Griffons", QuestID::Fow_Griffons},
        NamedQuest{"FoW - Slaves", QuestID::Fow_Slaves},
        NamedQuest{"FoW - Restore", QuestID::Fow_Restore},
        NamedQuest{"FoW - Hunt", QuestID::Fow_Hunt},
        NamedQuest{"FoW - Forgemaster", QuestID::Fow_Forgemaster},
        NamedQuest{"FoW - Tos", QuestID::Fow_Tos},
        NamedQuest{"FoW - Toc", QuestID::Fow_Toc},
        NamedQuest{"FoW - Khobay", QuestID::Fow_Khobay},
        NamedQuest{"DoA - Gloom 1: Deathbringer Company", QuestID::Doa_DeathbringerCompany},
        NamedQuest{"DoA - Gloom 2: The Rifts Between Us", QuestID::Doa_RiftBetweenUs},
        NamedQuest{"DoA - Gloom 3: To The Rescue", QuestID::Doa_ToTheRescue},
        NamedQuest{"DoA - City", QuestID::Doa_City},
        NamedQuest{"DoA - Veil 1: Breaching Stygian Veil", QuestID::Doa_BreachingStygianVeil},
        NamedQuest{"DoA - Veil 2: Brood Wars", QuestID::Doa_BroodWars},
        NamedQuest{"DoA - Foundry 1: Foundry Of Failed Creations", QuestID::Doa_FoundryOfFailedCreations},
        NamedQuest{"DoA - Foundry 2: Foundry Breakout", QuestID::Doa_FoundryBreakout},
    };

    struct NamedDialog {
        const char* name;
        uint32_t id;
    };

    constexpr std::array dialogs = {
        NamedDialog{"Craft fow armor", DialogID::FowCraftArmor},
        NamedDialog{"Prof Change - Warrior", DialogID::ProfChangeWarrior},
        NamedDialog{"Prof Change - Ranger", DialogID::ProfChangeRanger},
        NamedDialog{"Prof Change - Monk", DialogID::ProfChangeMonk},
        NamedDialog{"Prof Change - Necro", DialogID::ProfChangeNecro},
        NamedDialog{"Prof Change - Mesmer", DialogID::ProfChangeMesmer},
        NamedDialog{"Prof Change - Elementalist", DialogID::ProfChangeEle},
        NamedDialog{"Prof Change - Assassin", DialogID::ProfChangeAssassin},
        NamedDialog{"Prof Change - Ritualist", DialogID::ProfChangeRitualist},
        NamedDialog{"Prof Change - Paragon", DialogID::ProfChangeParagon},
        NamedDialog{"Prof Change - Dervish", DialogID::ProfChangeDervish},
        NamedDialog{"Kama -> Docks @ Hahnna", DialogID::FerryKamadanToDocks},
        NamedDialog{"Docks -> Kaineng @ Mhenlo", DialogID::FerryDocksToKaineng},
        NamedDialog{"Docks -> LA Gate @ Mhenlo", DialogID::FerryDocksToLA},
        NamedDialog{"LA Gate -> LA @ Neiro", DialogID::FerryGateToLA},
        NamedDialog{"Faction mission outpost", DialogID::FactionMissionOutpost},
        NamedDialog{"Nightfall mission outpost", DialogID::NightfallMissionOutpost},
    };

    constexpr uint32_t dhuum_quest_dialog = 8677633;

    uint32_t QuestAcceptDialog(const QuestID quest)
    {
        return (static_cast<uint32_t>(quest) << 8) | 0x800001;
    }

    uint32_t QuestRewardDialog(const QuestID quest)
    {
        return (static_cast<uint32_t>(quest) << 8) | 0x800007;
    }

    bool ParseInt(const char* text, int* value)
    {
        char* end;
        errno = 0;
        *value = static_cast<int>(std::strtol(text, &end, 0));
        return *end == 0 && errno != ERANGE;
    }

    const char* QuestName(void* data, const int index)
    {
        return static_cast<const NamedQuest*>(data)[index].name;
    }

    const char* DialogName(void* data, const int index)
    {
        return static_cast<const NamedDialog*>(data)[index].name;
    }
}

void DialogsWindow::SendDialog(const uint32_t dialog_id) const
{
    if (!(use_function_ptr && DirectDialog::Send(dialog_id))) {
        GW::Agents::SendDialog(dialog_id);
    }
}

void DialogsWindow::Draw(IDirect3DDevice9*)
{
    if (!GetVisiblePtr() || !*GetVisiblePtr()) {
        return;
    }

    auto region_width = 0.f;
    const auto dialog_button = [this, &region_width](const int x_idx, const int x_qty, const char* text, const char* help, const uint32_t dialog) {
        if (x_idx != 0) {
            ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
        }
        const auto width = (region_width - ImGui::GetStyle().ItemInnerSpacing.x * static_cast<float>(x_qty - 1)) / static_cast<float>(x_qty);
        if (ImGui::Button(text, ImVec2(width, 0))) {
            SendDialog(dialog);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", help);
        }
    };

    const auto common_dialogs_to_show = show_foundry_reward + show_tower_of_strength + show_demon_assassin + show_four_horsemen + show_dhuum;
    auto common_dialog_counter = 0;
    const auto common_dialog = [&](const char* label, const char* description, const uint32_t id) {
        const auto dialogs_left = common_dialogs_to_show - common_dialog_counter;
        dialog_button(common_dialog_counter % 2, std::max(std::min(dialogs_left, 2), common_dialog_counter % 2 + 1), label, description, id);
        common_dialog_counter++;
    };

    const auto& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_FirstUseEver);

    if (ImGui::Begin(Name(), show_closebutton ? GetVisiblePtr() : nullptr, GetWinFlags())) {
        region_width = ImGui::GetContentRegionAvail().x;
        if (show_four_horsemen) {
            common_dialog("Four Horseman", "Take quest in Planes", QuestAcceptDialog(QuestID::UW_Planes));
        }
        if (show_demon_assassin) {
            common_dialog("Demon Assassin", "Take quest in Mountains", QuestAcceptDialog(QuestID::UW_Mnt));
        }
        if (show_tower_of_strength) {
            common_dialog("Tower of Strength", "Take quest", QuestAcceptDialog(QuestID::Fow_Tos));
        }
        if (show_foundry_reward) {
            common_dialog("Foundry Reward", "Accept reward", QuestRewardDialog(QuestID::Doa_FoundryBreakout));
        }
        if (show_dhuum) {
            common_dialog("Dhuum", "Take quest", dhuum_quest_dialog);
        }
        if (common_dialogs_to_show > 0) {
            ImGui::Separator();
        }

        if (show_uwteles) {
            dialog_button(0, 4, "Lab", "Teleport Lab", DialogID::UwTeleLab);
            dialog_button(1, 4, "Vale", "Teleport Vale", DialogID::UwTeleVale);
            dialog_button(2, 4, "Pits", "Teleport Pits", DialogID::UwTelePits);
            dialog_button(3, 4, "Pools", "Teleport Pools", DialogID::UwTelePools);

            dialog_button(0, 3, "Planes", "Teleport Planes", DialogID::UwTelePlanes);
            dialog_button(1, 3, "Wastes", "Teleport Wastes", DialogID::UwTeleWastes);
            dialog_button(2, 3, "Mountains", "Teleport Mountains\nThis is NOT the mountains quest", DialogID::UwTeleMnt);
            ImGui::Separator();
        }

        if (show_favorites) {
            for (size_t i = 0; i < fav_index.size(); ++i) {
                auto& quest_index = fav_index[i];
                quest_index = std::clamp(quest_index, 0, static_cast<int>(quests.size()) - 1);
                ImGui::PushID(static_cast<int>(i));
                ImGui::PushItemWidth(-100.0f - ImGui::GetStyle().ItemInnerSpacing.x * 2);
                ImGui::Combo("", &quest_index, QuestName, const_cast<NamedQuest*>(quests.data()), static_cast<int>(quests.size()));
                ImGui::PopItemWidth();
                ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
                if (ImGui::Button("Take", ImVec2(40.0f, 0))) {
                    SendDialog(QuestAcceptDialog(quests[quest_index].id));
                }
                ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
                if (ImGui::Button("Reward", ImVec2(60.0f, 0))) {
                    SendDialog(QuestRewardDialog(quests[quest_index].id));
                }
                ImGui::PopID();
            }
            ImGui::Separator();
        }

        if (show_custom) {
            static int dialog_index = 0;
            ImGui::PushItemWidth(-60.0f - ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::Combo("###dialogcombo", &dialog_index, DialogName, const_cast<NamedDialog*>(dialogs.data()), static_cast<int>(dialogs.size()));
            ImGui::PopItemWidth();
            ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
            if (ImGui::Button("Send##1", ImVec2(60.0f, 0))) {
                SendDialog(dialogs[dialog_index].id);
            }

            ImGui::PushItemWidth(-60.0f - ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::InputText("###dialoginput", custom_dialog_buffer, sizeof(custom_dialog_buffer));
            ImGui::PopItemWidth();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("You can prefix the number by \"0x\" to specify an hexadecimal number");
            }
            ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
            if (ImGui::Button("Send##2", ImVec2(60.0f, 0))) {
                if (int id; ParseInt(custom_dialog_buffer, &id) && id >= 0) {
                    SendDialog(static_cast<uint32_t>(id));
                }
            }
        }
    }
    ImGui::End();
}

void DialogsWindow::DrawPluginSettings()
{
    ImGui::PushItemWidth(100.0f);
    if (ImGui::InputInt("Number of favorites", &fav_count)) {
        fav_count = std::clamp(fav_count, 0, 100);
        fav_index.resize(static_cast<size_t>(fav_count), 0);
    }
    ImGui::PopItemWidth();

    ImGui::Text("Show common dialog:");
    ImGui::SameLine();
    ImGui::Checkbox("Four Horseman", &show_four_horsemen);
    ImGui::SameLine();
    ImGui::Checkbox("Demon Assassin", &show_demon_assassin);
    ImGui::SameLine();
    ImGui::Checkbox("Tower of Strength", &show_tower_of_strength);
    ImGui::SameLine();
    ImGui::Checkbox("Foundry Reward", &show_foundry_reward);
    ImGui::SameLine();
    ImGui::Checkbox("Dhuum", &show_dhuum);

    ImGui::Text("Show:");
    ImGui::SameLine();
    ImGui::Checkbox("UW Teles", &show_uwteles);
    ImGui::SameLine();
    ImGui::Checkbox("Favorites", &show_favorites);
    ImGui::SameLine();
    ImGui::Checkbox("Custom", &show_custom);

    ImGui::Checkbox("Send dialogs in combat", &use_function_ptr);
    PluginGui::ShowHelp("Allows to send more dialogs, for example to allies in combat. Might flag your account, use at your own risk.");
}

bool DialogsWindow::HasEmptySettings() const
{
    return HasDefaultWindowSettings() && fav_count == default_favorite_count && std::ranges::all_of(fav_index, [](const int index) { return index == 0; })
        && show_foundry_reward && show_tower_of_strength && show_demon_assassin && show_four_horsemen && show_dhuum
        && show_uwteles && show_favorites && show_custom && !use_function_ptr;
}

void DialogsWindow::LoadSettings(const wchar_t* folder)
{
    SpeedrunPlugin::LoadSettings(folder);
    LoadSetting("fav_count", fav_count);
    fav_count = std::clamp(fav_count, 0, 100);
    fav_index.assign(static_cast<size_t>(fav_count), 0);
    for (size_t i = 0; i < fav_index.size(); ++i) {
        LoadSetting(("Quest" + std::to_string(i)).c_str(), fav_index[i]);
    }
    LoadSetting("show_four_horsemen", show_four_horsemen);
    LoadSetting("show_foundry_reward", show_foundry_reward);
    LoadSetting("show_tower_of_strength", show_tower_of_strength);
    LoadSetting("show_demon_assassin", show_demon_assassin);
    LoadSetting("show_dhuum", show_dhuum);
    LoadSetting("show_uwteles", show_uwteles);
    LoadSetting("show_favorites", show_favorites);
    LoadSetting("show_custom", show_custom);
    LoadSetting("useFunctionPtr", use_function_ptr);
}

void DialogsWindow::SaveSettings(const wchar_t* folder)
{
    SaveSetting("fav_count", fav_count);
    for (size_t i = 0; i < fav_index.size(); ++i) {
        SaveSetting(("Quest" + std::to_string(i)).c_str(), fav_index[i]);
    }
    SaveSetting("show_four_horsemen", show_four_horsemen);
    SaveSetting("show_foundry_reward", show_foundry_reward);
    SaveSetting("show_tower_of_strength", show_tower_of_strength);
    SaveSetting("show_demon_assassin", show_demon_assassin);
    SaveSetting("show_dhuum", show_dhuum);
    SaveSetting("show_uwteles", show_uwteles);
    SaveSetting("show_favorites", show_favorites);
    SaveSetting("show_custom", show_custom);
    SaveSetting("useFunctionPtr", use_function_ptr);
    SpeedrunPlugin::SaveSettings(folder);
}
