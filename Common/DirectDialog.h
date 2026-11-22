#pragma once

#include <GWCA/Utilities/Scanner.h>

#include <cstdint>

namespace DirectDialog {
    using SendAgentDialog_pt = void(__cdecl*)(uint32_t dialog_id);

    inline SendAgentDialog_pt SendAgentDialogFunction()
    {
        static const auto function = [] {
            const auto address = GW::Scanner::Find("\x89\x4b\x24\x8b\x4b\x28\x83\xe9\x00", "xxxxxxxxx");
            return GW::Scanner::IsValidPtr(address, GW::ScannerSection::Section_TEXT)
                ? reinterpret_cast<SendAgentDialog_pt>(GW::Scanner::FunctionFromNearCall(address + 0x15))
                : nullptr;
        }();
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
