#pragma once

#include <Windows.h>

#include <Keys.h>

#include <imgui.h>

#include <optional>
#include <string>

namespace Hotkeys {
    struct Hotkey {
        long key = 0;
        long modifier = 0;
        bool operator==(const Hotkey&) const = default;
    };

    inline std::string Describe(const Hotkey& hotkey)
    {
        char description[256];
        ModKeyName(description, sizeof(description), hotkey.modifier, hotkey.key);
        return description;
    }

    inline long CurrentModifier()
    {
        long modifier = 0;
        if (GetKeyState(VK_CONTROL) < 0) {
            modifier |= ModKey_Control;
        }
        if (GetKeyState(VK_SHIFT) < 0) {
            modifier |= ModKey_Shift;
        }
        if (GetKeyState(VK_MENU) < 0) {
            modifier |= ModKey_Alt;
        }
        return modifier;
    }

    inline std::optional<long> MouseButtonKey(const WPARAM wparam)
    {
        if (LOWORD(wparam) & MK_XBUTTON2) {
            return VK_XBUTTON2;
        }
        if (LOWORD(wparam) & MK_XBUTTON1) {
            return VK_XBUTTON1;
        }
        if (LOWORD(wparam) & MK_MBUTTON) {
            return VK_MBUTTON;
        }
        return std::nullopt;
    }

    inline std::optional<long> PressedKey(const UINT message, const WPARAM wparam, const LPARAM lparam)
    {
        constexpr LPARAM repeat_flag = 1 << 30;
        switch (message) {
            case WM_KEYDOWN:
                return (lparam & repeat_flag) ? std::nullopt : std::optional<long>(static_cast<long>(wparam));
            case WM_SYSKEYDOWN:
                return static_cast<long>(wparam);
            case WM_XBUTTONDOWN:
                return MouseButtonKey(wparam);
            case WM_MBUTTONDOWN:
                return MouseButtonKey(wparam);
            case WM_XBUTTONDBLCLK:
                return MouseButtonKey(wparam);
            case WM_MBUTTONDBLCLK:
                return VK_MBUTTON;
            default:
                return std::nullopt;
        }
    }

    inline bool IsTriggered(const Hotkey& hotkey, const UINT message, const WPARAM wparam, const LPARAM lparam)
    {
        const auto key = PressedKey(message, wparam, lparam);
        return hotkey.key && key && *key == hotkey.key && CurrentModifier() == hotkey.modifier;
    }

    inline bool DrawSelector(Hotkey& hotkey, const float width, const char* unset_label = "Set hotkey")
    {
        auto changed = false;
        ImGui::PushItemWidth(width);
        if (ImGui::Button(hotkey.key ? Describe(hotkey).c_str() : unset_label)) {
            ImGui::OpenPopup("Select Hotkey");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Click to change hotkey");
        }
        if (ImGui::BeginPopup("Select Hotkey")) {
            static long pending_key = 0;
            long modifier = 0;
            ImGui::Text("Press key");
            if (ImGui::IsKeyDown(ImGuiMod_Ctrl)) {
                modifier |= ModKey_Control;
            }
            if (ImGui::IsKeyDown(ImGuiMod_Shift)) {
                modifier |= ModKey_Shift;
            }
            if (ImGui::IsKeyDown(ImGuiMod_Alt)) {
                modifier |= ModKey_Alt;
            }

            if (pending_key == 0) {
                for (WORD vkey = 0; vkey < 512; vkey++) {
                    switch (vkey) {
                        case VK_CONTROL:
                        case VK_LCONTROL:
                        case VK_RCONTROL:
                        case VK_SHIFT:
                        case VK_LSHIFT:
                        case VK_RSHIFT:
                        case VK_MENU:
                        case VK_LMENU:
                        case VK_RMENU:
                        case VK_LBUTTON:
                        case VK_RBUTTON:
                            continue;
                        default:
                            if (::GetKeyState(vkey) & 0x8000) {
                                pending_key = vkey;
                            }
                    }
                }
            }
            else if (!(::GetKeyState(pending_key) & 0x8000)) {
                hotkey = {pending_key, modifier};
                changed = true;
                pending_key = 0;
                ImGui::CloseCurrentPopup();
            }

            ImGui::Text("%s", Describe({pending_key, modifier}).c_str());
            ImGui::EndPopup();
        }
        ImGui::PopItemWidth();
        return changed;
    }
}
