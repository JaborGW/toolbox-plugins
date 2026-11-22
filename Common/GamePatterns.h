#pragma once

#include <GWCA/Utilities/Scanner.h>

#include <Windows.h>

#include <cstdint>
#include <cstring>

namespace GamePatterns {
    struct Anchor {
        const char* bytes;
        const char* mask;
    };

    struct FunctionPattern {
        const char* name;
        Anchor anchors[2];
    };

    struct Assertion {
        const char* name;
        const char* file;
        const char* message;
    };

    constexpr Anchor SendAgentDialogCaller{"\x89\x4b\x00\x8b\x4b\x00\x83\xe9\x00\x74\x00\x83\xe9\x01\x0f\x85\x00\x00\x00\x00\x50\xe8", "xx?xx?xxxx?xxxxx????xx"};
    constexpr int send_agent_dialog_call_offset = 0x15;
    constexpr FunctionPattern SideWalk{"SideWalk", {
        {"\xd9\x06\xd9\x5d\x00\xd9\x46\x00\xd9\x5d\x00\xd9\x45\x00\xd9\x45\x00\xd8\xc9\x8b\x45\x00", "xxxx?xx?xx?xx?xx?xxxx?"},
        {"\xda\xe9\xdf\xe0\xf6\xc4\x44\x7a\x00\x83\x3d\x00\x00\x00\x00\xff\x74\x00", "xxxxxxxx?xx????xx?"},
    }};
    constexpr FunctionPattern FindPath{"FindPath", {
        {"\x8b\x5d\x00\x8b\x7d\x00\x8b\x70\x00\xd9\x03\xd9\x5d\x00\xd9\x43\x00", "xx?xx?xx?xxxx?xx?"},
        {"\xd9\x05\x00\x00\x00\x00\xd8\x5d\x00\xdf\xe0\xf6\xc4\x01\x75\x00\x8b\x4d\x00\x8b\x03", "xx????xx?xxxxxx?xx?xx"},
    }};
    constexpr Assertion SendPacket{"CtoS SendPacket", "MsgConn.cpp", "bytes >= sizeof(dword)"};
    constexpr Assertion GameServerObject{"CtoS game server object", "GcGameCmd.cpp", "No valid case for switch variable 'code'"};

    constexpr uint32_t interact_gadget_after_send_dialog = 0x16;
    constexpr uint32_t send_signpost_dialog_after_send_dialog = 0x18;
    constexpr uint32_t call_target_before_send_dialog = 0x18;
    constexpr uintptr_t max_function_start_distance = 0x200;
    constexpr uintptr_t max_server_object_load_distance = 0x80;

    inline bool IsReadable(const void* address, const size_t size)
    {
        MEMORY_BASIC_INFORMATION info{};
        if (!address || !VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
            return false;
        }
        return reinterpret_cast<uintptr_t>(address) + size <= reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
    }

    inline uintptr_t FindUnique(const Anchor& anchor)
    {
        if (!anchor.bytes) {
            return 0;
        }
        const auto first = GW::Scanner::FindNth(anchor.bytes, anchor.mask, 0, 0);
        return first && !GW::Scanner::FindNth(anchor.bytes, anchor.mask, 1, 0) ? first : 0;
    }

    inline uint32_t ReadDword(const uint8_t* bytes)
    {
        uint32_t value;
        std::memcpy(&value, bytes, sizeof(value));
        return value;
    }

    inline bool IsCode(const uintptr_t address)
    {
        return GW::Scanner::IsValidPtr(address, GW::ScannerSection::Section_TEXT);
    }

    // Functions follow padding, a return, or a switch jump table; a start hooked by another tool jumps out of the game's code.
    inline bool IsFunctionStart(const uint8_t* bytes)
    {
        const auto prologue = bytes[0] == 0x55 && ((bytes[1] == 0x8B && bytes[2] == 0xEC) || (bytes[1] == 0x89 && bytes[2] == 0xE5));
        const auto hooked = bytes[0] == 0xE9 && !IsCode(reinterpret_cast<uintptr_t>(bytes) + 5 + static_cast<int32_t>(ReadDword(bytes + 1)));
        const auto after_boundary = bytes[-1] == 0xCC || bytes[-1] == 0xC3 || bytes[-1] == 0x90 || bytes[-3] == 0xC2 || IsCode(ReadDword(bytes - 4));
        return (prologue || hooked) && after_boundary;
    }

    inline bool IsFunctionStart(const uintptr_t address)
    {
        const auto bytes = reinterpret_cast<const uint8_t*>(address);
        return address > 4 && IsReadable(bytes - 4, 9) && IsFunctionStart(bytes);
    }

    inline uintptr_t FunctionStartBefore(const uintptr_t address)
    {
        if (address <= max_function_start_distance + 4) {
            return 0;
        }
        const auto lowest = reinterpret_cast<const uint8_t*>(address - max_function_start_distance);
        if (!IsReadable(lowest - 4, max_function_start_distance + 9)) {
            return 0;
        }
        for (auto bytes = reinterpret_cast<const uint8_t*>(address); bytes >= lowest; --bytes) {
            if (IsFunctionStart(bytes)) {
                return reinterpret_cast<uintptr_t>(bytes);
            }
        }
        return 0;
    }

    inline uintptr_t FindFunction(const Anchor& anchor)
    {
        const auto match = FindUnique(anchor);
        return match ? FunctionStartBefore(match) : 0;
    }

    inline uintptr_t FindFunction(const FunctionPattern& pattern)
    {
        uintptr_t found = 0;
        for (const auto& anchor : pattern.anchors) {
            const auto start = FindFunction(anchor);
            if (!start) {
                continue;
            }
            if (found && found != start) {
                return 0;
            }
            found = start;
        }
        return found;
    }

    inline uintptr_t FindAssertion(const Assertion& assertion)
    {
        return GW::Scanner::FindAssertion(assertion.file, assertion.message, 0, 0);
    }

    inline uintptr_t FindSendPacket()
    {
        const auto assertion = FindAssertion(SendPacket);
        return assertion ? FunctionStartBefore(assertion) : 0;
    }

    inline uintptr_t FindSendAgentDialog()
    {
        const auto caller = FindUnique(SendAgentDialogCaller);
        if (!caller) {
            return 0;
        }
        int32_t relative;
        std::memcpy(&relative, reinterpret_cast<const void*>(caller + send_agent_dialog_call_offset + 1), sizeof(relative));
        const auto function = caller + send_agent_dialog_call_offset + 5 + relative;
        return IsFunctionStart(function) ? function : 0;
    }

    // The server object is the global loaded by `mov eax, [global]; test eax, eax` shortly before the assertion.
    inline uintptr_t FindGameServerObject()
    {
        const auto assertion = FindAssertion(GameServerObject);
        if (assertion <= max_server_object_load_distance) {
            return 0;
        }
        const auto bytes = reinterpret_cast<const uint8_t*>(assertion);
        if (!IsReadable(bytes - max_server_object_load_distance, max_server_object_load_distance)) {
            return 0;
        }
        for (uintptr_t distance = 5; distance < max_server_object_load_distance; ++distance) {
            const auto load = bytes - distance;
            if (load[0] == 0xA1 && load[5] == 0x85 && load[6] == 0xC0) {
                uintptr_t global;
                std::memcpy(&global, load + 1, sizeof(global));
                return global;
            }
        }
        return 0;
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

    inline uint32_t SendDialogOpcode()
    {
        const auto opcode = ReadSenderOpcode(FindSendDialogSender(FindSendAgentDialog()));
        return IsPlausibleSendDialogOpcode(opcode) ? opcode : 0;
    }

    // The call-target (ping) sender stub is found by the opcode it stores at [ebp-0x10], which moves when the client renumbers.
    struct CallTargetPattern {
        char bytes[5] = {'\xc7', '\x45', '\xf0', 0, 0};
        FunctionPattern pattern{"Ping (call target)", {}};

        CallTargetPattern()
        {
            if (const auto send_dialog = SendDialogOpcode()) {
                bytes[3] = static_cast<char>(send_dialog - call_target_before_send_dialog);
                pattern.anchors[0] = {bytes, "xxxx"};
            }
        }

        CallTargetPattern(const CallTargetPattern&) = delete;
        CallTargetPattern& operator=(const CallTargetPattern&) = delete;
    };

    inline uintptr_t FindPing()
    {
        const CallTargetPattern call_target;
        return FindFunction(call_target.pattern);
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
