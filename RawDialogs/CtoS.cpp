#include "CtoS.h"

#include <DirectDialog.h>

#include <GamePatterns.h>
#include <GWCA/stdlib/GameThreadMgr.h>

#include <cstring>
#include <Windows.h>

#ifndef NDEBUG
#include <Logger.h>
#endif
#include <vector>

namespace {
    using SendPacket_pt = void(__cdecl*)(uint32_t context, uint32_t size, void* packet);
    SendPacket_pt SendPacket_Func = nullptr;
    uintptr_t game_server_object_address = 0;
    CtoS::Opcodes opcodes;

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
        SendPacket_Func = reinterpret_cast<SendPacket_pt>(GamePatterns::FindSendPacket());
        const auto object_address = GamePatterns::FindGameServerObject();
        game_server_object_address = GamePatterns::IsReadable(reinterpret_cast<const void*>(object_address), sizeof(uint32_t)) ? object_address : 0;
        const auto sender = GamePatterns::FindSendDialogSender(reinterpret_cast<uintptr_t>(DirectDialog::SendAgentDialogFunction()));
        if (const auto send_dialog = GamePatterns::ReadSenderOpcode(sender); GamePatterns::IsPlausibleSendDialogOpcode(send_dialog)) {
            opcodes = {send_dialog, send_dialog + GamePatterns::interact_gadget_after_send_dialog, send_dialog + GamePatterns::send_signpost_dialog_after_send_dialog};
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

#ifndef NDEBUG
    void LogDiagnostics()
    {
        Log::Log("[RawDialog] send fn 0x%X, assertion 0x%X", reinterpret_cast<uintptr_t>(SendPacket_Func), GamePatterns::FindAssertion(GamePatterns::SendPacket));
        Log::Log("[RawDialog] server object 0x%X, assertion 0x%X", game_server_object_address, GamePatterns::FindAssertion(GamePatterns::GameServerObject));
        const auto send_agent_dialog = reinterpret_cast<const uint8_t*>(DirectDialog::SendAgentDialogFunction());
        if (!GamePatterns::IsReadable(send_agent_dialog, 0x40)) {
            Log::Log("[RawDialog] SendAgentDialog 0x%X not readable", reinterpret_cast<uintptr_t>(send_agent_dialog));
            Log::FlushFile();
            return;
        }
        Log::Log("[RawDialog] SendAgentDialog 0x%X: %02X %02X %02X %02X %02X %02X %02X %02X", reinterpret_cast<uintptr_t>(send_agent_dialog),
            send_agent_dialog[0], send_agent_dialog[1], send_agent_dialog[2], send_agent_dialog[3], send_agent_dialog[4], send_agent_dialog[5], send_agent_dialog[6], send_agent_dialog[7]);
        Log::Log("[RawDialog] send dialog opcode 0x%X", GamePatterns::ReadSenderOpcode(GamePatterns::FindSendDialogSender(reinterpret_cast<uintptr_t>(send_agent_dialog))));
        Log::FlushFile();
    }
#endif

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
