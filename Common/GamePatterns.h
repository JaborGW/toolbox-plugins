#pragma once

#include <GWCA/Utilities/Scanner.h>

#include <Windows.h>

#include <cstdint>
#include <cstring>

namespace GamePatterns {
    struct BytePattern {
        const char* name;
        const char* bytes;
        const char* mask;
        int offset;
    };

    struct Assertion {
        const char* name;
        const char* file;
        const char* message;
        int offset;
    };

    constexpr BytePattern SendAgentDialogCaller{"SendAgentDialog caller", "\x89\x4b\x24\x8b\x4b\x28\x83\xe9\x00", "xxxxxxxxx", 0};
    constexpr int send_agent_dialog_call_offset = 0x15;
    // Matches the call-target sender stub by its opcode (0x23), so it breaks whenever the client renumbers its opcodes.
    constexpr BytePattern Ping{"Ping (call target)", "\xc7\x45\xf0\x23", "xxxx", -0x22};
    constexpr BytePattern SideWalk{"SideWalk", "\xd9\x46\x04\xd9\x5d\xfc\xd9\x45\x0c\xd9\x45", "xxxxxxxxxx", -0x27};
    constexpr BytePattern FindPath{"FindPath", "\x8b\x5d\x0c\x8b\x7d\x08\x8b\x70\x14\xd9", "xxxxxxxxxx", -0xE};
    constexpr Assertion SendPacket{"CtoS SendPacket", "MsgConn.cpp", "bytes >= sizeof(dword)", -0x67};
    constexpr Assertion GameServerObject{"CtoS game server object", "GcGameCmd.cpp", "No valid case for switch variable 'code'", -0x32};

    constexpr uint32_t interact_gadget_after_send_dialog = 0x16;
    constexpr uint32_t send_signpost_dialog_after_send_dialog = 0x18;

    inline uintptr_t Find(const BytePattern& pattern)
    {
        return GW::Scanner::Find(pattern.bytes, pattern.mask, pattern.offset);
    }

    inline uintptr_t Find(const Assertion& assertion)
    {
        return GW::Scanner::FindAssertion(assertion.file, assertion.message, 0, assertion.offset);
    }

    inline bool IsReadable(const void* address, const size_t size)
    {
        MEMORY_BASIC_INFORMATION info{};
        if (!address || !VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
            return false;
        }
        return reinterpret_cast<uintptr_t>(address) + size <= reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
    }

    inline uintptr_t FindSendAgentDialog()
    {
        const auto caller = Find(SendAgentDialogCaller);
        if (!GW::Scanner::IsValidPtr(caller, GW::ScannerSection::Section_TEXT)) {
            return 0;
        }
        return GW::Scanner::FunctionFromNearCall(caller + send_agent_dialog_call_offset);
    }

    inline uintptr_t FindGameServerObject()
    {
        const auto address = Find(GameServerObject);
        if (!IsReadable(reinterpret_cast<const void*>(address), sizeof(uintptr_t))) {
            return 0;
        }
        return *reinterpret_cast<const uintptr_t*>(address);
    }

    // The game's own SendAgentDialog tail-calls its sender stub after `pop ebp`; its first bytes may already be a hook's jump.
    inline uintptr_t FindSendDialogSender(const uintptr_t send_agent_dialog)
    {
        const auto bytes = reinterpret_cast<const uint8_t*>(send_agent_dialog);
        if (!IsReadable(bytes, 0x45)) {
            return 0;
        }
        for (auto i = 1; i < 0x40; ++i) {
            if (bytes[i - 1] == 0x5D && bytes[i] == 0xE9) {
                int32_t relative;
                std::memcpy(&relative, bytes + i + 1, sizeof(relative));
                return reinterpret_cast<uintptr_t>(bytes + i + 5) + relative;
            }
        }
        return 0;
    }

    inline uint32_t ReadSenderOpcode(const uintptr_t sender)
    {
        const auto bytes = reinterpret_cast<const uint8_t*>(sender);
        if (!IsReadable(bytes, 0x37)) {
            return 0;
        }
        for (auto i = 0; i < 0x30; ++i) {
            if (bytes[i] == 0xC7 && bytes[i + 1] == 0x45) {
                uint32_t opcode;
                std::memcpy(&opcode, bytes + i + 3, sizeof(opcode));
                return opcode;
            }
        }
        return 0;
    }

    inline bool IsPlausibleSendDialogOpcode(const uint32_t opcode)
    {
        return opcode >= 0x30 && opcode < 0x60;
    }

    namespace Gwca {
        constexpr uint8_t get_agent_matches_flags_stub[] = {0x55, 0x89, 0xe5, 0x83, 0xec, 0x18, 0x8b, 0x45, 0x0c, 0x89, 0x44, 0x24, 0x04, 0x8b, 0x45, 0x08, 0x89, 0x04, 0x24, 0xe8};

        // GWCA's own change-target check calls the implementation behind the export directly, so hooks must target that.
        inline uintptr_t GetAgentMatchesFlagsImplementation(const uintptr_t export_stub)
        {
            const auto bytes = reinterpret_cast<const uint8_t*>(export_stub);
            if (!IsReadable(bytes, sizeof(get_agent_matches_flags_stub) + 4) || std::memcmp(bytes, get_agent_matches_flags_stub, sizeof(get_agent_matches_flags_stub)) != 0) {
                return 0;
            }
            int32_t relative;
            std::memcpy(&relative, bytes + sizeof(get_agent_matches_flags_stub), sizeof(relative));
            return reinterpret_cast<uintptr_t>(bytes) + sizeof(get_agent_matches_flags_stub) + sizeof(relative) + relative;
        }
    }
}
