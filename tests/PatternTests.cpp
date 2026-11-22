#include <GamePatterns.h>

#include <GWCA/Runtime.h>
#include <GWCA/Utilities/Scanner.h>

#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {
    struct Result {
        bool passed;
        std::string detail;
    };

    struct Check {
        const char* name;
        Result (*run)();
    };

    std::string Hex(const uintptr_t value)
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "0x%X", static_cast<unsigned>(value));
        return buffer;
    }

    bool StartsWithPrologue(const uintptr_t address)
    {
        const auto bytes = reinterpret_cast<const uint8_t*>(address);
        return GamePatterns::IsReadable(bytes, 3) && bytes[0] == 0x55 && ((bytes[1] == 0x8B && bytes[2] == 0xEC) || (bytes[1] == 0x89 && bytes[2] == 0xE5));
    }

    size_t CountMatches(const GamePatterns::BytePattern& pattern)
    {
        size_t count = 0;
        while (GW::Scanner::FindNth(pattern.bytes, pattern.mask, count, pattern.offset)) {
            ++count;
        }
        return count;
    }

    Result UniqueFunction(const GamePatterns::BytePattern& pattern)
    {
        const auto count = CountMatches(pattern);
        const auto address = GamePatterns::Find(pattern);
        if (count != 1) {
            return {false, std::to_string(count) + " matches (expected exactly 1)"};
        }
        if (!StartsWithPrologue(address)) {
            return {false, "match at " + Hex(address) + " is not a function prologue"};
        }
        return {true, Hex(address)};
    }

    Result CheckSendAgentDialog()
    {
        const auto count = CountMatches(GamePatterns::SendAgentDialogCaller);
        const auto function = GamePatterns::FindSendAgentDialog();
        if (count != 1) {
            return {false, "caller pattern: " + std::to_string(count) + " matches (expected exactly 1)"};
        }
        if (!StartsWithPrologue(function)) {
            return {false, "near call target " + Hex(function) + " is not a function prologue"};
        }
        return {true, Hex(function)};
    }

    Result CheckSendPacket()
    {
        const auto function = GamePatterns::Find(GamePatterns::SendPacket);
        if (!StartsWithPrologue(function)) {
            return {false, "assertion result " + Hex(function) + " is not a function prologue"};
        }
        return {true, Hex(function)};
    }

    Result CheckGameServerObject()
    {
        const auto object = GamePatterns::FindGameServerObject();
        if (!GW::Scanner::IsValidPtr(object, GW::ScannerSection::Section_DATA)) {
            return {false, "pointer " + Hex(object) + " is not in .data"};
        }
        return {true, Hex(object)};
    }

    Result CheckSendDialogOpcode()
    {
        const auto sender = GamePatterns::FindSendDialogSender(GamePatterns::FindSendAgentDialog());
        if (!StartsWithPrologue(sender)) {
            return {false, "tail call target " + Hex(sender) + " is not a function prologue"};
        }
        const auto opcode = GamePatterns::ReadSenderOpcode(sender);
        if (!GamePatterns::IsPlausibleSendDialogOpcode(opcode)) {
            return {false, "opcode " + Hex(opcode) + " is implausible"};
        }
        return {true, "send dialog " + Hex(opcode) + ", interact gadget " + Hex(opcode + GamePatterns::interact_gadget_after_send_dialog) +
                          ", signpost dialog " + Hex(opcode + GamePatterns::send_signpost_dialog_after_send_dialog)};
    }

    const Check game_checks[] = {
        {"SendAgentDialog (DirectDialog)", CheckSendAgentDialog},
        {"CtoS SendPacket", CheckSendPacket},
        {"CtoS game server object", CheckGameServerObject},
        {"CtoS send dialog opcode", CheckSendDialogOpcode},
        {"Ping (call target)", [] {
             const GamePatterns::CallTargetPattern call_target;
             if (!call_target.pattern.bytes) {
                 return Result{false, "send dialog opcode not found, so the call-target opcode is unknown"};
             }
             auto result = UniqueFunction(call_target.pattern);
             result.detail += ", opcode " + Hex(static_cast<uint8_t>(call_target.bytes[3]));
             return result;
         }},
        {"SideWalk", [] { return UniqueFunction(GamePatterns::SideWalk); }},
        {"FindPath", [] { return UniqueFunction(GamePatterns::FindPath); }},
    };

    bool Report(const char* name, const Result& result)
    {
        std::printf("  [%s] %-32s %s\n", result.passed ? " OK " : "FAIL", name, result.detail.c_str());
        return result.passed;
    }

    bool CheckGwca(HMODULE gwca)
    {
        const auto stub = reinterpret_cast<uintptr_t>(GetProcAddress(gwca, "GW_Agents_GetAgentMatchesFlags"));
        const auto implementation = GamePatterns::Gwca::GetAgentMatchesFlagsImplementation(stub);
        std::printf("gwca.dll\n");
        return Report("GetAgentMatchesFlags implementation", implementation ? Result{true, Hex(implementation)} : Result{false, "export stub bytes changed"});
    }

    bool CheckGameClient(const std::filesystem::path& gw_exe)
    {
        std::printf("%s\n", gw_exe.string().c_str());
        const auto module = LoadLibraryExW(gw_exe.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
        if (!module) {
            std::printf("  [FAIL] could not map the client (error %lu)\n", GetLastError());
            return false;
        }
        GW::Scanner::InitializeForModule(module);
        auto passed = true;
        for (const auto& check : game_checks) {
            passed &= Report(check.name, check.run());
        }
        FreeLibrary(module);
        return passed;
    }

    std::vector<std::filesystem::path> GameClients(const int argc, wchar_t** argv)
    {
        std::vector<std::filesystem::path> inputs;
        for (auto i = 1; i < argc; ++i) {
            inputs.emplace_back(argv[i]);
        }
        if (inputs.empty()) {
            inputs.emplace_back(SPEEDRUN_TEST_GW_EXE);
        }
        std::vector<std::filesystem::path> clients;
        for (const auto& input : inputs) {
            std::error_code ec;
            if (std::filesystem::is_regular_file(input, ec)) {
                clients.push_back(input);
                continue;
            }
            for (const auto& entry : std::filesystem::recursive_directory_iterator(input, ec)) {
                const auto name = entry.path().filename().wstring();
                if (entry.is_regular_file(ec) && name.starts_with(L"Gw") && entry.path().extension() == L".exe") {
                    clients.push_back(entry.path());
                }
            }
        }
        return clients;
    }
}

int wmain(const int argc, wchar_t** argv)
{
    const auto clients = GameClients(argc, argv);
    if (clients.empty()) {
        std::printf("FAIL: no Gw.exe found. Copy the current client to %s, or pass Gw.exe paths or folders as arguments.\n", std::filesystem::path(SPEEDRUN_TEST_GW_EXE).string().c_str());
        return 1;
    }

    const auto gwca = GW::Runtime::LoadGWCA(SPEEDRUN_TEST_GWCA_DLL);
    if (!gwca) {
        std::printf("FAIL: could not load and bind %s (error %lu)\n", std::filesystem::path(SPEEDRUN_TEST_GWCA_DLL).string().c_str(), GetLastError());
        return 1;
    }

    auto passed = CheckGwca(gwca);
    for (const auto& client : clients) {
        passed &= CheckGameClient(client);
    }
    std::printf(passed ? "All pattern checks passed.\n" : "Some pattern checks FAILED.\n");
    return passed ? 0 : 1;
}
