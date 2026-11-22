#include "CtoS.h"

#include <DirectDialog.h>

#include <GWCA/Utilities/Scanner.h>
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

    constexpr uint32_t interact_gadget_after_send_dialog = 0x16;
    constexpr uint32_t send_signpost_dialog_after_send_dialog = 0x18;

    bool IsReadable(const void* address, const size_t size)
    {
        MEMORY_BASIC_INFORMATION info{};
        if (!address || !VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
            return false;
        }
        return reinterpret_cast<uintptr_t>(address) + size <= reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
    }

    // The client renumbers its outgoing opcodes in some updates (all shifted by one in the 2025-12 client), so read send-dialog's from the game.
    uint32_t ReadSendDialogOpcode()
    {
        const auto send_agent_dialog = reinterpret_cast<const uint8_t*>(DirectDialog::SendAgentDialogFunction());
        if (!IsReadable(send_agent_dialog, 0x45)) {
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
        if (!IsReadable(sender, 0x37)) {
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
        if (const auto address = GW::Scanner::FindAssertion("GcGameCmd.cpp", "No valid case for switch variable 'code'", 0, -0x32); IsReadable(reinterpret_cast<const void*>(address), sizeof(uintptr_t))) {
            const auto object_address = *reinterpret_cast<const uintptr_t*>(address);
            game_server_object_address = IsReadable(reinterpret_cast<const void*>(object_address), sizeof(uint32_t)) ? object_address : 0;
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

#ifndef NDEBUG
    void LogDiagnostics()
    {
        Log::Log("[RawDialog] send fn 0x%X, assertion 0x%X", reinterpret_cast<uintptr_t>(SendPacket_Func), GW::Scanner::FindAssertion("MsgConn.cpp", "bytes >= sizeof(dword)", 0, 0));
        Log::Log("[RawDialog] server object 0x%X, assertion 0x%X", game_server_object_address, GW::Scanner::FindAssertion("GcGameCmd.cpp", "No valid case for switch variable 'code'", 0, 0));
        const auto send_agent_dialog = reinterpret_cast<const uint8_t*>(DirectDialog::SendAgentDialogFunction());
        if (!IsReadable(send_agent_dialog, 0x40)) {
            Log::Log("[RawDialog] SendAgentDialog 0x%X not readable", reinterpret_cast<uintptr_t>(send_agent_dialog));
            Log::FlushFile();
            return;
        }
        Log::Log("[RawDialog] SendAgentDialog 0x%X: %02X %02X %02X %02X %02X %02X %02X %02X", reinterpret_cast<uintptr_t>(send_agent_dialog),
            send_agent_dialog[0], send_agent_dialog[1], send_agent_dialog[2], send_agent_dialog[3], send_agent_dialog[4], send_agent_dialog[5], send_agent_dialog[6], send_agent_dialog[7]);
        Log::Log("[RawDialog] send dialog opcode 0x%X", ReadSendDialogOpcode());
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
