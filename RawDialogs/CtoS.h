#pragma once

#include <cstdint>
#include <initializer_list>
#ifndef NDEBUG
#include <string>
#endif

namespace CtoS {
    struct Opcodes {
        uint32_t send_dialog = 0;
        uint32_t interact_gadget = 0;
        uint32_t send_signpost_dialog = 0;
    };

    bool Initialize();
    bool IsInitialized();
    const Opcodes& GetOpcodes();
    bool Send(std::initializer_list<uint32_t> packet);
#ifndef NDEBUG
    std::wstring Describe();
#endif
}
