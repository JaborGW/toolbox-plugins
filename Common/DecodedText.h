#pragma once

#include <imgui.h>

#include <PluginUtils.h>

#include <GWCA/Constants/Skills.h>
#include <GWCA/GameEntities/Skill.h>
#include <GWCA/stdlib/GameThreadMgr.h>
#include <GWCA/stdlib/SkillbarMgr.h>
#include <GWCA/stdlib/UIMgr.h>

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace DecodedText {
    using Buffer = std::array<wchar_t, 256>;

    inline std::wstring_view Get(const std::wstring_view encoded)
    {
        static std::mutex mutex;
        static std::unordered_map<std::wstring, std::unique_ptr<Buffer>> cache;
        if (encoded.empty()) {
            return {};
        }
        std::scoped_lock lock{mutex};
        auto [it, inserted] = cache.try_emplace(std::wstring{encoded});
        if (inserted) {
            it->second = std::make_unique<Buffer>();
            it->second->fill(L'\0');
            const auto request = [key = it->first, buffer = it->second.get()] {
                GW::UI::AsyncDecodeStr(key.c_str(), buffer->data(), buffer->size());
            };
            if (GW::GameThread::IsInGameThread()) {
                request();
            }
            else {
                GW::GameThread::Enqueue(request);
            }
        }
        return {it->second->data()};
    }

    inline std::wstring_view Get(const wchar_t* encoded)
    {
        return encoded ? Get(std::wstring_view{encoded}) : std::wstring_view{};
    }

    inline std::wstring_view FromId(const uint32_t encoded_id)
    {
        wchar_t encoded[8] = {};
        return GW::UI::UInt32ToEncStr(encoded_id, encoded, std::size(encoded)) ? Get(std::wstring_view{encoded}) : std::wstring_view{};
    }

    inline std::wstring_view SkillName(const GW::Constants::SkillID id)
    {
        const auto skill = GW::SkillbarMgr::GetSkillConstantData(id);
        return skill ? FromId(skill->name) : std::wstring_view{};
    }

    inline std::string Utf8(const std::wstring_view text)
    {
        return PluginUtils::WStringToString(std::wstring{text});
    }
}
