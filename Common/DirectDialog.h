#pragma once

#include <GamePatterns.h>

#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/stdlib/GameThreadMgr.h>
#include <GWCA/stdlib/UIMgr.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>

namespace DirectDialog {
    using SendAgentDialog_pt = void(__cdecl*)(uint32_t dialog_id);

    constexpr uint32_t quest_dialog_flag = 0x800000;
    constexpr uint32_t quest_dialog_type_mask = 0xFF;
    constexpr uint32_t quest_take = 0x01;
    constexpr uint32_t quest_enquire = 0x03;
    constexpr uint32_t quest_enquire_reward = 0x06;
    constexpr uint32_t quest_reward = 0x07;

    inline SendAgentDialog_pt SendAgentDialogFunction()
    {
        static const auto function = reinterpret_cast<SendAgentDialog_pt>(GamePatterns::FindSendAgentDialog());
        return function;
    }

    // The server only accepts taking a quest or its reward after the matching enquiry in the same conversation.
    inline std::optional<uint32_t> QuestEnquiry(const uint32_t dialog_id)
    {
        if ((dialog_id & 0xFF800000) != quest_dialog_flag) {
            return std::nullopt;
        }
        switch (dialog_id & quest_dialog_type_mask) {
            case quest_take:
                return (dialog_id & ~quest_dialog_type_mask) | quest_enquire;
            case quest_reward:
                return (dialog_id & ~quest_dialog_type_mask) | quest_enquire_reward;
            default:
                return std::nullopt;
        }
    }

    // Skips the client's checks in GW::Agents::SendDialog, so dialogs also go out e.g. to allies in combat.
    // The game function reads per-thread game state, so it must run on the game thread; UI code calls this from the render hook.
    inline bool Send(const uint32_t dialog_id)
    {
        const auto send = SendAgentDialogFunction();
        if (!send) {
            return false;
        }
        if (GW::GameThread::IsInGameThread()) {
            send(dialog_id);
        }
        else {
            GW::GameThread::Enqueue([send, dialog_id] { send(dialog_id); });
        }
        return true;
    }

    // The enquiry goes out raw so it also works in combat; the take/reward is then clicked in the offer window it opens.
    // A raw accept would close that window behind the UI's back.
    class QuestDialogSender {
    public:
        void Initialize()
        {
            GW::UI::RegisterUIMessageCallback(&button_entry, GW::UI::UIMessage::kDialogButton, [this](GW::HookStatus*, GW::UI::UIMessage, void* wparam, void*) {
                const auto button = static_cast<const GW::UI::DialogButtonInfo*>(wparam);
                const auto pending = pending_dialog.load();
                if (!button || !pending || button->dialog_id != pending) {
                    return;
                }
                pending_dialog = 0;
                GW::GameThread::Enqueue([pending] {
                    if (!GW::Agents::SendDialog(pending)) {
                        DirectDialog::Send(pending);
                    }
                });
            }, button_callback_altitude);
        }

        void Terminate()
        {
            pending_dialog = 0;
            GW::UI::RemoveUIMessageCallback(&button_entry);
        }

        bool Send(const uint32_t dialog_id)
        {
            const auto enquiry = QuestEnquiry(dialog_id);
            if (!enquiry) {
                return DirectDialog::Send(dialog_id);
            }
            if (!SendAgentDialogFunction()) {
                return false;
            }
            if (pending_dialog && Clock::now() <= deadline.load()) {
                return true;
            }
            deadline = Clock::now() + offer_timeout;
            pending_dialog = dialog_id;
            return DirectDialog::Send(*enquiry);
        }

        void Update()
        {
            if (pending_dialog && Clock::now() > deadline.load()) {
                pending_dialog = 0;
            }
        }

    private:
        using Clock = std::chrono::steady_clock;
        static constexpr auto offer_timeout = std::chrono::seconds(3);
        static constexpr int button_callback_altitude = 0x500;

        GW::HookEntry button_entry;
        std::atomic<uint32_t> pending_dialog = 0;
        std::atomic<Clock::time_point> deadline{};
    };
}
