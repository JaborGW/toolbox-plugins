#pragma once

#include <GWCA/stdlib/ChatMgr.h>
#include <GWCA/stdlib/GameThreadMgr.h>

#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace PluginChat {
    inline void Write(const std::wstring_view sender, const std::wstring_view message)
    {
        auto text = std::make_unique<std::wstring>(std::format(L"<a=1>{}</a><c=#FFFFFF>: {}</c>", sender, message));
        const auto enqueued = GW::GameThread::Enqueue([](void* context) {
            const std::unique_ptr<std::wstring> owned(static_cast<std::wstring*>(context));
            GW::Chat::WriteChat(GW::Chat::Channel::CHANNEL_GWCA2, owned->c_str(), nullptr);
        }, text.get());
        if (enqueued) {
            text.release();
        }
    }

    inline void ReportUnavailable(const std::wstring_view sender, const std::vector<std::wstring>& features, const std::wstring_view cause = L"this game client")
    {
        if (features.empty()) {
            return;
        }
        std::wstring list;
        for (const auto& feature : features) {
            list += (list.empty() ? L"" : L", ") + feature;
        }
        Write(sender, std::format(L"Not supported by {}: {}. The plugin needs an update.", cause, list));
    }
}
