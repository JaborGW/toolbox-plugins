#pragma once

#include <GamePatterns.h>

#include <cstdint>

namespace DirectDialog {
    using SendAgentDialog_pt = void(__cdecl*)(uint32_t dialog_id);

    inline SendAgentDialog_pt SendAgentDialogFunction()
    {
        static const auto function = reinterpret_cast<SendAgentDialog_pt>(GamePatterns::FindSendAgentDialog());
        return function;
    }

    // Skips the client's checks in GW::Agents::SendDialog, so dialogs also go out e.g. to allies in combat.
    inline bool Send(const uint32_t dialog_id)
    {
        const auto send = SendAgentDialogFunction();
        if (!send) {
            return false;
        }
        send(dialog_id);
        return true;
    }
}
