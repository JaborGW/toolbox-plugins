#include "CtoS.h"

#include <DirectDialog.h>

#include <GWCA/Utilities/Scanner.h>
#include <GWCA/stdlib/GameThreadMgr.h>

#include <cstring>
#include <vector>

namespace {
    using SendPacket_pt = void(__cdecl*)(uint32_t context, uint32_t size, void* packet);
    SendPacket_pt SendPacket_Func = nullptr;
    uintptr_t game_server_object_address = 0;
    CtoS::Opcodes opcodes;

    constexpr uint32_t interact_gadget_after_send_dialog = 0x16;
    constexpr uint32_t send_signpost_dialog_after_send_dialog = 0x18;

    // The client renumbers its outgoing opcodes in some updates (all shifted by one in the 2025-12 client), so read send-dialog's from the game.
    uint32_t ReadSendDialogOpcode()
    {
        const auto send_agent_dialog = reinterpret_cast<const uint8_t*>(DirectDialog::SendAgentDialogFunction());
        if (!send_agent_dialog) {
            return 0;
        }
        const uint8_t* sender = nullptr;
        for (auto i = 0; i < 0x40 && !sender; ++i) {
            if (send_agent_dialog[i] == 0xE9) {
                int32_t relative;
                std::memcpy(&relative, send_agent_dialog + i + 1, sizeof(relative));
                sender = send_agent_dialog + i + 5 + relative;
            }
        }
        if (!sender) {
            return 0;
        }
        for (auto i = 0; i < 0x30; ++i) {
            if (sender[i] == 0xC7 && sender[i + 1] == 0x45) {
                uint32_t opcode;
                std::memcpy(&opcode, sender + i + 3, sizeof(opcode));
                return opcode;
            }
        }
        return 0;
    }

    void SendNow(const std::vector<uint32_t>& packet)
    {
        SendPacket_Func(*reinterpret_cast<uint32_t*>(game_server_object_address), static_cast<uint32_t>(packet.size() * sizeof(uint32_t)), const_cast<uint32_t*>(packet.data()));
    }
}

namespace CtoS {
    bool Initialize()
    {
        if (IsInitialized()) {
            return true;
        }
        SendPacket_Func = reinterpret_cast<SendPacket_pt>(GW::Scanner::FindAssertion("MsgConn.cpp", "bytes >= sizeof(dword)", 0, -0x67));
        if (const auto address = GW::Scanner::FindAssertion("GcGameCmd.cpp", "No valid case for switch variable 'code'", 0, -0x32)) {
            game_server_object_address = *reinterpret_cast<uintptr_t*>(address);
        }
        if (const auto send_dialog = ReadSendDialogOpcode(); send_dialog >= 0x30 && send_dialog < 0x60) {
            opcodes = {send_dialog, send_dialog + interact_gadget_after_send_dialog, send_dialog + send_signpost_dialog_after_send_dialog};
        }
        return IsInitialized();
    }

    bool IsInitialized()
    {
        return SendPacket_Func && game_server_object_address && opcodes.send_dialog;
    }

    const Opcodes& GetOpcodes()
    {
        return opcodes;
    }

    bool Send(const std::initializer_list<uint32_t> packet)
    {
        if (!IsInitialized()) {
            return false;
        }
        std::vector<uint32_t> copy(packet);
        if (GW::GameThread::IsInGameThread()) {
            SendNow(copy);
            return true;
        }
        GW::GameThread::Enqueue([copy = std::move(copy)] {
            SendNow(copy);
        });
        return true;
    }
}
