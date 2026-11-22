#include "SkinChanger.h"

#include <PluginUtils.h>

#include <GWCA/Constants/Constants.h>
#include <GWCA/Constants/ItemIDs.h>
#include <GWCA/Context/GameContext.h>
#include <GWCA/Context/WorldContext.h>
#include <GWCA/GameContainers/Array.h>
#include <GWCA/GameEntities/Agent.h>
#include <GWCA/GameEntities/NPC.h>
#include <GWCA/GameEntities/Player.h>
#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/Managers/ItemMgr.h>
#include <GWCA/Managers/MapMgr.h>
#include <GWCA/Managers/PlayerMgr.h>
#include <GWCA/Packets/StoC.h>
#include <GWCA/Utilities/Hook.h>
#include <GWCA/stdlib/GameThreadMgr.h>
#include <GWCA/stdlib/StoCMgr.h>
#include <GWCA/stdlib/UIMgr.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cwctype>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <utility>

DLLAPI ToolboxPlugin* ToolboxPluginInstance()
{
    static SkinChanger instance;
    return &instance;
}

namespace {
    GW::HookEntry use_item_entry;
    GW::HookEntry agent_add_entry;
    bool has_agent_add_hook = false;
    GW::HookEntry instance_load_entry;

    std::map<GW::Constants::Bag, std::vector<InventoryItem>> available_items;
    std::unordered_map<std::wstring, std::array<wchar_t, 256>> decoded_names;

    std::optional<MinipetTransmog> pending_minipet_transmog;
    std::optional<uint32_t> popped_minipet_id;
    std::chrono::steady_clock::time_point last_pop = std::chrono::steady_clock::now();

    constexpr char end_of_string_identifier = '~';
    constexpr uint32_t default_agent_scale = 0x23000000;
    constexpr uint32_t scale_shift = 24;

    std::string RemoveTextInBrackets(std::string text)
    {
        for (auto left = text.find('<'); left != std::string::npos; left = text.find('<', left)) {
            const auto right = text.find('>', left);
            if (right == std::string::npos) {
                break;
            }
            text.erase(left, right - left + 1);
        }
        return text;
    }

    std::string Decode(const std::wstring& encoded)
    {
        if (encoded.empty()) {
            return "";
        }
        const auto [it, inserted] = decoded_names.try_emplace(encoded);
        if (inserted) {
            it->second.fill(L'\0');
            GW::GameThread::Enqueue([encoded, buffer = it->second.data(), size = it->second.size()] {
                GW::UI::AsyncDecodeStr(encoded.c_str(), buffer, size);
            });
        }
        return RemoveTextInBrackets(PluginUtils::WStringToString(it->second.data()));
    }

    std::optional<int> ToInt(const std::string& text)
    {
        const auto hex = text.starts_with("0x");
        const auto begin = text.data() + (hex ? 2 : 0);
        const auto end = text.data() + text.size();
        int value = 0;
        const auto [parsed_end, error] = std::from_chars(begin, end, value, hex ? 16 : 10);
        if (error != std::errc{} || parsed_end == begin) {
            return std::nullopt;
        }
        return value;
    }

    std::string ToHexString(const uint32_t value)
    {
        std::stringstream stream;
        stream << std::hex << std::uppercase << value;
        return "0x" + stream.str();
    }

    void Trim(std::string& text)
    {
        const auto not_space = [](const unsigned char c) { return !std::isspace(c); };
        text.erase(text.begin(), std::find_if(text.begin(), text.end(), not_space));
        text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(), text.end());
    }

    bool IsEquippable(const GW::Item* item)
    {
        if (!item) {
            return false;
        }
        using GW::Constants::ItemType;
        switch (static_cast<ItemType>(item->type)) {
            case ItemType::Axe:
            case ItemType::Boots:
            case ItemType::Bow:
            case ItemType::Chestpiece:
            case ItemType::Offhand:
            case ItemType::Gloves:
            case ItemType::Hammer:
            case ItemType::Headpiece:
            case ItemType::Leggings:
            case ItemType::Wand:
            case ItemType::Shield:
            case ItemType::Staff:
            case ItemType::Sword:
            case ItemType::Daggers:
            case ItemType::Scythe:
            case ItemType::Spear:
            case ItemType::Costume_Headpiece:
            case ItemType::Costume:
                return true;
            default:
                return false;
        }
    }

    enum class Behaviour { AllItems, EquippableOnly };

    void ForEachItem(const std::function<bool(GW::Item*)>& visit, const Behaviour behaviour)
    {
        using GW::Constants::Bag;
        for (const auto bag_slot : {Bag::Backpack, Bag::Belt_Pouch, Bag::Bag_1, Bag::Bag_2, Bag::Equipment_Pack, Bag::Equipped_Items}) {
            const auto bag = GW::Items::GetBag(bag_slot);
            if (!bag) {
                continue;
            }
            for (const auto item : bag->items) {
                const auto wanted = behaviour == Behaviour::AllItems ? item != nullptr : IsEquippable(item);
                if (wanted && visit(item)) {
                    return;
                }
            }
        }
    }

    std::vector<GW::ItemModifier> ExtractMods(const GW::Item* item)
    {
        return {item->mod_struct, item->mod_struct + item->mod_struct_size};
    }

    bool SameMods(const std::vector<GW::ItemModifier>& mods, const GW::ItemModifier* other, const size_t count)
    {
        return mods.size() == count && std::equal(mods.begin(), mods.end(), other, [](const auto& a, const auto& b) { return a.mod == b.mod; });
    }

    void DrawItemSelector(InventoryItem& inventory_item)
    {
        static bool need_to_fetch_bag_items = false;
        constexpr auto bag_names = std::array{"None", "Backpack", "Belt Pouch", "Bag 1", "Bag 2", "Equipment Pack"};

        if (ImGui::Button("Edit item")) {
            need_to_fetch_bag_items = true;
            ImGui::OpenPopup("Choose item to adjust");
        }
        ImGui::SameLine();

        if (inventory_item.model_id == 0) {
            ImGui::TextUnformatted("No item chosen");
            ImGui::SameLine();
        }
        else if (!inventory_item.encoded_name.empty()) {
            ImGui::TextUnformatted(Decode(inventory_item.encoded_name).c_str());
            ImGui::SameLine();
        }
        else {
            ForEachItem([&inventory_item](const GW::Item* item) {
                if (item->model_id == inventory_item.model_id && SameMods(inventory_item.modifiers, item->mod_struct, item->mod_struct_size)) {
                    inventory_item.encoded_name = item->single_item_name ? item->single_item_name : L"";
                    return true;
                }
                return false;
            }, Behaviour::EquippableOnly);
        }

        if (ImGui::BeginPopupModal("Choose item to adjust", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (need_to_fetch_bag_items) {
                available_items.clear();
                ForEachItem([](const GW::Item* item) {
                    const auto bag = item->bag ? item->bag->bag_id() : GW::Constants::Bag::Backpack;
                    available_items[bag].push_back(InventoryItem{item->model_id, item->single_item_name ? item->single_item_name : L"", ExtractMods(item)});
                    return false;
                }, Behaviour::EquippableOnly);
                need_to_fetch_bag_items = false;
            }
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
            for (auto& [bag, bag_items] : available_items) {
                const auto bag_index = static_cast<size_t>(bag);
                ImGui::TextUnformatted(bag == GW::Constants::Bag::Equipped_Items ? "Equipped items" : bag_index < bag_names.size() ? bag_names[bag_index] : "Bag");
                ImGui::Indent();
                for (auto& bag_item : bag_items) {
                    ImGui::PushID(&bag_item);
                    if (ImGui::Button((Decode(bag_item.encoded_name) + "##item").c_str())) {
                        inventory_item = bag_item;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopID();
                }
                ImGui::Unindent();
            }
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    constexpr ImVec4 palette[] = {
        {0.f, 0.f, 1.f, 0.f},
        {0.f, 0.75f, 0.f, 0.f},
        {0.5f, 0.f, 0.5f, 0.f},
        {1.f, 0.f, 0.f, 0.f},
        {1.f, 1.f, 0.f, 0.f},
        {0.5f, 0.25f, 0.f, 0.f},
        {1.f, 0.65f, 0.f, 0.f},
        {0.75f, 0.75f, 0.75f, 0.f},
        {0.f, 0.f, 0.f, 0.f},
        {0.5f, 0.5f, 0.5f, 0.f},
        {1.f, 1.f, 1.f, 0.f},
        {0.95f, 0.5f, 0.95f, 0.f},
    };

    std::optional<size_t> PaletteIndex(const GW::DyeColor color)
    {
        const auto index = static_cast<int>(std::to_underlying(color)) - static_cast<int>(std::to_underlying(GW::DyeColor::Blue));
        if (index < 0 || index >= static_cast<int>(std::size(palette))) {
            return std::nullopt;
        }
        return static_cast<size_t>(index);
    }

    bool ColorPalette(const char* label, size_t* palette_index)
    {
        constexpr size_t max_per_line = 7;
        ImGui::PushID(label);
        ImGui::BeginGroup();
        auto changed = false;
        for (size_t i = 0; i < std::size(palette); i++) {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::ColorButton("", palette[i], ImGuiColorEditFlags_AlphaOpaque)) {
                *palette_index = i;
                changed = true;
            }
            ImGui::PopID();
            if ((i + 1) % max_per_line != 0) {
                ImGui::SameLine();
            }
        }
        ImGui::PushID(static_cast<int>(std::size(palette)));
        if (ImGui::ColorButton("", ImVec4{})) {
            *palette_index = std::size(palette);
            changed = true;
        }
        ImGui::PopID();
        ImGui::EndGroup();
        ImGui::PopID();
        return changed;
    }

    bool DrawDyePicker(const char* label, GW::DyeColor* color)
    {
        ImGui::PushID(label);
        const auto index = PaletteIndex(*color);
        auto changed = false;
        if (ImGui::ColorButton("##ColorButton", index ? palette[*index] : ImVec4{}, index ? ImGuiColorEditFlags_AlphaOpaque : 0)) {
            ImGui::OpenPopup("picker");
        }
        if (ImGui::BeginPopup("picker")) {
            const auto label_end = ImGui::FindRenderedTextEnd(label);
            if (label != label_end) {
                ImGui::TextUnformatted(label, label_end);
                ImGui::Separator();
            }
            size_t picked;
            if (ColorPalette("##picker", &picked)) {
                *color = picked < std::size(palette) ? static_cast<GW::DyeColor>(std::to_underlying(GW::DyeColor::Blue) + picked) : GW::DyeColor::None;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
        return changed;
    }

    uint32_t ScaleFromPercent(const uint8_t percent)
    {
        return static_cast<uint32_t>(percent) << scale_shift;
    }

    void TransmoAgent(const uint32_t agent_id, const NpcTransmog& transmo)
    {
        if (!transmo.npc_id || !agent_id) {
            return;
        }
        const auto npc_model_file_id = ToInt(transmo.npc_model_file_id);
        const auto npc_model_file_data = ToInt(transmo.npc_model_file_data);
        const auto flags = ToInt(transmo.flags);
        if (!npc_model_file_id || !flags || *npc_model_file_id == 0) {
            return;
        }
        const auto agent = GW::Agents::GetAgentByID(agent_id);
        const auto living = agent ? agent->GetAsAgentLiving() : nullptr;
        if (!living) {
            return;
        }
        const auto existing_npc = GW::Agents::GetNPCByID(living->player_number);
        const auto scale = transmo.scale ? ScaleFromPercent(static_cast<uint8_t>(std::clamp(transmo.scale, 10, 255)))
                                         : existing_npc ? ScaleFromPercent(static_cast<uint8_t>(existing_npc->visual_adjustment.scale)) : default_agent_scale;

        const auto game_context = GW::GetGameContext();
        const auto& npcs = game_context->world->npcs;
        if (transmo.npc_id >= static_cast<int>(npcs.size()) || !npcs[transmo.npc_id].model_file_id) {
            GW::GameThread::Enqueue([npc_id = transmo.npc_id, model_file_id = *npc_model_file_id, npc_flags = *flags] {
                GW::Packet::StoC::NpcGeneralStats packet{};
                packet.npc_id = npc_id;
                packet.file_id = model_file_id;
                packet.flags = npc_flags;
                packet.profession = 1;
                GW::StoC::EmulatePacket(&packet);
            });
            GW::GameThread::Enqueue([npc_id = transmo.npc_id, npc_model_file_data] {
                GW::Packet::StoC::NPCModelFile packet{};
                packet.npc_id = npc_id;
                packet.count = npc_model_file_data ? 1 : 0;
                if (packet.count) {
                    packet.data[0] = *npc_model_file_data;
                }
                GW::StoC::EmulatePacket(&packet);
            });
        }

        const auto encoded_name = transmo.name_to_apply.empty() ? std::wstring{} : L"\x108\x107" + PluginUtils::StringToWString(transmo.name_to_apply) + L"\x1";
        GW::GameThread::Enqueue([npc_id = transmo.npc_id, encoded_name, agent_id, scale] {
            GW::Packet::StoC::AgentScale scale_packet{};
            scale_packet.header = GW::Packet::StoC::AgentScale::STATIC_HEADER;
            scale_packet.agent_id = agent_id;
            scale_packet.scale = scale;
            GW::StoC::EmulatePacket(&scale_packet);

            GW::Packet::StoC::AgentModel model_packet{};
            model_packet.header = GW::Packet::StoC::AgentModel::STATIC_HEADER;
            model_packet.agent_id = agent_id;
            model_packet.model_id = npc_id;
            GW::StoC::EmulatePacket(&model_packet);

            if (!encoded_name.empty()) {
                GW::Packet::StoC::AgentName name_packet{};
                name_packet.header = GW::Packet::StoC::AgentName::STATIC_HEADER;
                name_packet.agent_id = agent_id;
                wcsncpy(name_packet.name_enc, encoded_name.c_str(), std::size(name_packet.name_enc) - 1);
                GW::StoC::EmulatePacket(&name_packet);
            }
        });
    }

    void DrawNpcSelector(NpcTransmog& npc_transmog)
    {
        ImGui::PushItemWidth(70.f);
        const auto tooltip = [](const char* text) {
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", text);
            }
        };
        ImGui::PushID(0);
        ImGui::InputInt("", &npc_transmog.npc_id, 0);
        tooltip("Desired NPC ID");
        ImGui::PopID();
        ImGui::SameLine();
        ImGui::PushID(1);
        ImGui::InputText("", &npc_transmog.npc_model_file_id);
        tooltip("Desired NPC Model File ID");
        ImGui::PopID();
        ImGui::SameLine();
        ImGui::PushID(2);
        ImGui::InputText("", &npc_transmog.npc_model_file_data);
        tooltip("Desired NPC Model File");
        ImGui::PopID();
        ImGui::SameLine();
        ImGui::PushID(3);
        ImGui::InputText("", &npc_transmog.flags);
        tooltip("Desired NPC flags");
        ImGui::PopID();
        ImGui::SameLine();
        ImGui::PushID(4);
        ImGui::InputInt("", &npc_transmog.scale, 0);
        tooltip("Desired NPC scale (10-255). 0 keeps size of current target.");
        ImGui::PopID();
        npc_transmog.scale = std::clamp(npc_transmog.scale, 0, 255);
        ImGui::PopItemWidth();
    }

    void CopyNpcFromTarget(NpcTransmog& npc_transmog)
    {
        const auto target = GW::Agents::GetTarget();
        const auto living = target ? target->GetAsAgentLiving() : nullptr;
        if (!living) {
            return;
        }
        const auto player = living->IsPlayer() ? GW::PlayerMgr::GetPlayerByID(living->player_number) : nullptr;
        auto npc_id = living->IsNPC() ? living->player_number : 0u;
        if (player && (living->transmog_npc_id & 0x20000000)) {
            npc_id = living->transmog_npc_id ^ 0x20000000;
        }
        const auto npc = GW::Agents::GetNPCByID(npc_id);
        if (!npc) {
            return;
        }
        npc_transmog.npc_id = static_cast<int>(npc_id);
        npc_transmog.scale = static_cast<uint8_t>(npc->visual_adjustment.scale);
        npc_transmog.flags = ToHexString(npc->npc_flags);
        npc_transmog.npc_model_file_id = ToHexString(npc->model_file_id);
        if (npc->files_count) {
            npc_transmog.npc_model_file_data = ToHexString(npc->model_files[0]);
        }
    }

    void DrawMinipetSelector(MinipetTransmog& transmog)
    {
        const auto tooltip = [](const char* text) {
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", text);
            }
        };
        ImGui::PushItemWidth(160.f);
        ImGui::Text("Name:");
        ImGui::SameLine();
        ImGui::PushID(0);
        ImGui::InputText("", &transmog.npc_transmog.name_to_apply);
        tooltip("Name to apply (name is kept if empty)");
        ImGui::PopID();
        ImGui::SameLine();
        Trim(transmog.npc_transmog.name_to_apply);
        if (transmog.npc_transmog.name_to_apply.size() > 31) {
            transmog.npc_transmog.name_to_apply.resize(31);
        }
        ImGui::PopItemWidth();

        ImGui::PushItemWidth(50.f);
        ImGui::Text("Current:");
        ImGui::SameLine();
        ImGui::PushID(1);
        ImGui::InputInt("", &transmog.agent_to_replace_model_id, 0);
        tooltip("Agent model ID");
        ImGui::PopID();
        ImGui::SameLine();
        ImGui::PushID(2);
        ImGui::InputInt("", &transmog.item_to_replace_model_id, 0);
        tooltip("Item model ID");
        ImGui::PopID();
        ImGui::SameLine();
        ImGui::PopItemWidth();

        ImGui::PushItemWidth(70.f);
        ImGui::Text("Item:");
        ImGui::SameLine();
        ImGui::PushID(3);
        ImGui::InputText("", &transmog.replacement_item_model_file_id);
        tooltip("Desired item model file ID (optional)");
        ImGui::PopID();
        ImGui::SameLine();
        ImGui::Text("NPC:");
        ImGui::SameLine();
        if (ImGui::Button("Copy from target")) {
            CopyNpcFromTarget(transmog.npc_transmog);
        }
        ImGui::SameLine();
        ImGui::PushID(4);
        DrawNpcSelector(transmog.npc_transmog);
        ImGui::PopID();
        ImGui::PopItemWidth();
    }

    // Non-EotN elementalist headpieces carry an interaction value that makes transmogs to the EotN pieces fail.
    std::optional<uint32_t> InteractionOverwrite(const uint32_t model_file_id)
    {
        switch (model_file_id) {
            case 0x7F5:
            case 0x817:
            case 0x818:
            case 0x830:
            case 0x8AE:
            case 0x7F6:
            case 0x8AF:
            case 0x844:
            case 0x845:
            case 0x846:
                return 0x20000006;
            default:
                return std::nullopt;
        }
    }

    std::string FormatItemChanges(const std::vector<ItemChange>& changes)
    {
        std::string text;
        for (const auto& change : changes) {
            text += std::to_string(change.item.model_id) + " ";
            for (const auto& mod : change.item.modifiers) {
                text += std::to_string(mod.mod) + " ";
            }
            text += "S " + change.model_file_id + " ";
            for (const auto dye : change.dyes) {
                text += std::to_string(static_cast<int>(dye)) + " ";
            }
            text += std::to_string(change.tint) + " " + std::to_string(static_cast<int>(change.enable_dyes)) + " END ";
        }
        return text;
    }

    std::vector<ItemChange> ParseItemChanges(const std::string& text)
    {
        std::vector<ItemChange> changes;
        std::stringstream stream{text};
        while (stream >> std::ws && stream.peek() != EOF) {
            ItemChange change;
            stream >> change.item.model_id >> std::ws;
            while (stream && stream.peek() != 'S') {
                uint32_t mod;
                stream >> mod >> std::ws;
                change.item.modifiers.push_back({mod});
            }
            if (!stream || stream.get() != 'S') {
                break;
            }
            stream >> std::ws >> change.model_file_id;
            for (auto& dye : change.dyes) {
                int value;
                stream >> value;
                dye = static_cast<GW::DyeColor>(value);
            }
            int tint;
            stream >> tint;
            if (tint >= 0 && tint < 256) {
                change.tint = static_cast<uint8_t>(tint);
            }
            stream >> change.enable_dyes;
            for (std::string token; stream >> token && token != "END";) {}
            changes.push_back(change);
        }
        return changes;
    }

    std::string FormatMinipetTransmogs(const std::vector<MinipetTransmog>& transmogs)
    {
        std::string text;
        for (const auto& transmog : transmogs) {
            text += std::to_string(transmog.agent_to_replace_model_id) + " ";
            text += std::to_string(transmog.item_to_replace_model_id) + " ";
            text += transmog.replacement_item_model_file_id + " ";
            text += transmog.npc_transmog.flags + " ";
            text += transmog.npc_transmog.name_to_apply + " " + end_of_string_identifier + " ";
            text += std::to_string(transmog.npc_transmog.npc_id) + " ";
            text += transmog.npc_transmog.npc_model_file_data + " ";
            text += transmog.npc_transmog.npc_model_file_id + " ";
            text += std::to_string(transmog.npc_transmog.scale) + " END ";
        }
        return text;
    }

    std::vector<MinipetTransmog> ParseMinipetTransmogs(const std::string& text)
    {
        std::vector<MinipetTransmog> transmogs;
        std::stringstream stream{text};
        while (stream >> std::ws && stream.peek() != EOF) {
            MinipetTransmog transmog;
            stream >> transmog.agent_to_replace_model_id >> transmog.item_to_replace_model_id;
            stream >> std::ws >> transmog.replacement_item_model_file_id;
            stream >> std::ws >> transmog.npc_transmog.flags;
            while (stream) {
                stream >> std::ws;
                if (stream.peek() == end_of_string_identifier) {
                    stream.get();
                    break;
                }
                std::string word;
                stream >> word;
                transmog.npc_transmog.name_to_apply += word + " ";
            }
            Trim(transmog.npc_transmog.name_to_apply);
            stream >> std::ws >> transmog.npc_transmog.npc_id;
            stream >> std::ws >> transmog.npc_transmog.npc_model_file_data;
            stream >> std::ws >> transmog.npc_transmog.npc_model_file_id;
            stream >> std::ws >> transmog.npc_transmog.scale;
            for (std::string token; stream >> token && token != "END";) {}
            if (!stream && transmog.agent_to_replace_model_id == 0 && transmog.item_to_replace_model_id == 0) {
                break;
            }
            transmogs.push_back(transmog);
        }
        return transmogs;
    }

    void ApplyItemChanges(const std::vector<ItemChange>& item_changes, const std::vector<MinipetTransmog>& minipet_transmogs)
    {
        ForEachItem([&](GW::Item* item) {
            const auto change = std::ranges::find_if(item_changes, [item](const ItemChange& candidate) {
                return candidate.item.model_id && candidate.item.model_id == item->model_id && SameMods(candidate.item.modifiers, item->mod_struct, item->mod_struct_size);
            });
            if (change == item_changes.end()) {
                return false;
            }
            if (const auto model_file_id = ToInt(change->model_file_id); model_file_id && *model_file_id) {
                item->model_file_id = static_cast<uint32_t>(*model_file_id);
                if (const auto interaction = InteractionOverwrite(item->model_file_id)) {
                    item->interaction = *interaction;
                }
            }
            if (change->enable_dyes) {
                item->dye.dye_tint = change->tint;
                item->dye.dye1 = change->dyes[0];
                item->dye.dye2 = change->dyes[1];
                item->dye.dye3 = change->dyes[2];
                item->dye.dye4 = change->dyes[3];
            }
            return false;
        }, Behaviour::EquippableOnly);

        ForEachItem([&](GW::Item* item) {
            const auto transmog = std::ranges::find_if(minipet_transmogs, [item](const MinipetTransmog& candidate) {
                return candidate.item_to_replace_model_id && static_cast<int>(item->model_id) == candidate.item_to_replace_model_id;
            });
            if (transmog != minipet_transmogs.end()) {
                if (const auto model_file_id = ToInt(transmog->replacement_item_model_file_id); model_file_id && *model_file_id) {
                    item->model_file_id = static_cast<uint32_t>(*model_file_id);
                }
            }
            return false;
        }, Behaviour::AllItems);
    }
}

void SkinChanger::LoadSettings(const wchar_t* folder)
{
    SpeedrunPlugin::LoadSettings(folder);
    std::string items;
    std::string transmogs;
    LoadSetting("itemChanges", items);
    LoadSetting("minipetTransmogs", transmogs);
    item_changes = ParseItemChanges(items);
    minipet_transmogs = ParseMinipetTransmogs(transmogs);
}

void SkinChanger::SaveSettings(const wchar_t* folder)
{
    SaveSetting("itemChanges", FormatItemChanges(item_changes));
    SaveSetting("minipetTransmogs", FormatMinipetTransmogs(minipet_transmogs));
    SpeedrunPlugin::SaveSettings(folder);
}

void SkinChanger::DrawPluginSettings()
{
    if (GW::Map::GetInstanceType() == GW::Constants::InstanceType::Loading || !GW::Agents::GetControlledCharacter()) {
        return;
    }

    std::optional<size_t> item_to_delete;
    for (size_t i = 0; i < item_changes.size(); i++) {
        auto& change = item_changes[i];
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Button("X")) {
            item_to_delete = i;
        }
        ImGui::SameLine();
        DrawItemSelector(change.item);
        ImGui::SameLine();
        ImGui::PushItemWidth(80.f);
        ImGui::InputText("", &change.model_file_id);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Model file ID");
        }
        std::erase_if(change.model_file_id, [](const unsigned char c) { return std::isspace(c); });
        ImGui::PopItemWidth();
        ImGui::SameLine();
        ImGui::Checkbox("##enable_dyes", &change.enable_dyes);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Enable dyes");
        }
        if (change.enable_dyes) {
            for (auto d = 0; d < 4; d++) {
                ImGui::SameLine();
                const std::string label = "Dye " + std::to_string(d + 1);
                DrawDyePicker(label.c_str(), &change.dyes[d]);
            }
            ImGui::SameLine();
            int tint = change.tint;
            ImGui::PushItemWidth(50.f);
            ImGui::InputInt("Tint (0-255)", &tint, 0);
            ImGui::PopItemWidth();
            change.tint = static_cast<uint8_t>(std::clamp(tint, 0, 255));
        }
        ImGui::PopID();
    }
    if (ImGui::Button("+")) {
        item_changes.push_back({});
    }
    if (item_to_delete) {
        item_changes.erase(item_changes.begin() + static_cast<std::ptrdiff_t>(*item_to_delete));
    }
    ImGui::Text("Item changes are applied on instance load.");

    ImGui::Text("Minipet transmogs:");
    std::optional<size_t> transmog_to_delete;
    for (size_t i = 0; i < minipet_transmogs.size(); i++) {
        ImGui::PushID(static_cast<int>(i + item_changes.size()));
        if (ImGui::Button("X")) {
            transmog_to_delete = i;
        }
        ImGui::SameLine();
        DrawMinipetSelector(minipet_transmogs[i]);
        ImGui::PopID();
    }
    ImGui::PushID(&minipet_transmogs);
    if (ImGui::Button("+")) {
        minipet_transmogs.push_back({});
    }
    ImGui::PopID();
    if (transmog_to_delete) {
        minipet_transmogs.erase(minipet_transmogs.begin() + static_cast<std::ptrdiff_t>(*transmog_to_delete));
    }
}

void SkinChanger::Initialize(ImGuiContext* ctx, const ImGuiAllocFns allocator_fns, const HMODULE toolbox_dll)
{
    SpeedrunPlugin::Initialize(ctx, allocator_fns, toolbox_dll);

    GW::StoC::RegisterPostPacketCallback<GW::Packet::StoC::InstanceLoadFile>(&instance_load_entry, [this](GW::HookStatus*, const GW::Packet::StoC::InstanceLoadFile*) {
        ApplyItemChanges(item_changes, minipet_transmogs);
        using namespace std::chrono_literals;
        popped_minipet_id = std::nullopt;
        last_pop = std::chrono::steady_clock::now() - 1h;
    });

    GW::UI::RegisterUIMessageCallback(&use_item_entry, GW::UI::UIMessage::kSendUseItem, [this](GW::HookStatus*, GW::UI::UIMessage, void* wparam, void*) {
        if (!wparam) {
            return;
        }
        const auto item = GW::Items::GetItemById(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(wparam)));
        if (!item) {
            return;
        }
        const auto transmog = std::ranges::find_if(minipet_transmogs, [id = static_cast<int>(item->model_id)](const MinipetTransmog& candidate) { return candidate.item_to_replace_model_id == id; });
        const auto is_ghost_in_the_box = item->model_id == GW::Constants::ItemID::GhostInTheBox;
        if (!is_ghost_in_the_box && transmog == minipet_transmogs.end()) {
            return;
        }
        if (popped_minipet_id == item->model_id) {
            popped_minipet_id = std::nullopt;
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - last_pop <= std::chrono::milliseconds(10'000)) {
            return;
        }
        last_pop = now;
        if (!is_ghost_in_the_box) {
            popped_minipet_id = item->model_id;
            pending_minipet_transmog = *transmog;
        }
    });
}

void SkinChanger::Update(float)
{
    if (GW::Map::GetInstanceType() == GW::Constants::InstanceType::Loading) {
        pending_minipet_transmog = std::nullopt;
        if (has_agent_add_hook) {
            has_agent_add_hook = false;
            GW::StoC::RemoveCallback<GW::Packet::StoC::AgentAdd>(&agent_add_entry);
        }
        return;
    }
    if (has_agent_add_hook) {
        return;
    }
    has_agent_add_hook = true;
    GW::StoC::RegisterPostPacketCallback<GW::Packet::StoC::AgentAdd>(&agent_add_entry, [](GW::HookStatus* status, const GW::Packet::StoC::AgentAdd* packet) {
        if (status->blocked || !packet->agent_type || !pending_minipet_transmog) {
            return;
        }
        const auto agent = GW::Agents::GetAgentByID(packet->agent_id);
        const auto living = agent ? agent->GetAsAgentLiving() : nullptr;
        if (!living || living->player_number != pending_minipet_transmog->agent_to_replace_model_id) {
            return;
        }
        TransmoAgent(agent->agent_id, pending_minipet_transmog->npc_transmog);
        pending_minipet_transmog = std::nullopt;
    });
}

void SkinChanger::SignalTerminate()
{
    GW::StoC::RemoveCallback<GW::Packet::StoC::InstanceLoadFile>(&instance_load_entry);
    GW::UI::RemoveUIMessageCallback(&use_item_entry, GW::UI::UIMessage::kSendUseItem);
    GW::StoC::RemoveCallbacks(&agent_add_entry);
    SpeedrunPlugin::SignalTerminate();
}
