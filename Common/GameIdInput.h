#pragma once

#include <imgui.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace GameIdInput {
    enum class Format { Decimal, Hex };

    inline std::optional<uint32_t> Parse(std::string_view text)
    {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
            text.remove_prefix(1);
        }
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
            text.remove_suffix(1);
        }
        auto base = 10;
        if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
            base = 16;
            text.remove_prefix(2);
        }
        if (text.empty()) {
            return std::nullopt;
        }
        uint32_t value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
        if (error != std::errc{} || end != text.data() + text.size()) {
            return std::nullopt;
        }
        return value;
    }

    inline std::optional<uint32_t> Parse(const std::wstring_view text)
    {
        std::string narrow;
        for (const auto c : text) {
            if (c > 0x7F) {
                return std::nullopt;
            }
            narrow.push_back(static_cast<char>(c));
        }
        return Parse(std::string_view{narrow});
    }

    inline bool DrawValue(const char* label, uint32_t& value, const Format format, const uint32_t max)
    {
        char text[16];
        std::snprintf(text, sizeof(text), format == Format::Hex ? "0x%X" : "%u", static_cast<unsigned>(value));
        const auto edited = ImGui::InputText(label, text, sizeof(text), ImGuiInputTextFlags_CharsNoBlank);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Decimal, or hexadecimal with a 0x prefix");
        }
        if (!edited) {
            return false;
        }
        const auto parsed = Parse(std::string_view{text});
        if (!parsed || *parsed > max) {
            return false;
        }
        value = *parsed;
        return true;
    }

    template <typename T>
        requires(std::is_integral_v<T> || std::is_enum_v<T>)
    bool Draw(const char* label, T& value, const Format format = Format::Decimal)
    {
        using Underlying = std::conditional_t<std::is_enum_v<T>, std::underlying_type<T>, std::type_identity<T>>::type;
        constexpr auto max = static_cast<uint32_t>(std::min<uint64_t>(std::numeric_limits<Underlying>::max(), std::numeric_limits<uint32_t>::max()));
        auto raw = static_cast<uint32_t>(value);
        if (!DrawValue(label, raw, format, max)) {
            return false;
        }
        value = static_cast<T>(raw);
        return true;
    }
}
