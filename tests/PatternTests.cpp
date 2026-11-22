#include <GamePatterns.h>

#include <GWCA/Runtime.h>
#include <GWCA/Utilities/Scanner.h>

#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <iterator>
#include <filesystem>
#include <string>
#include <vector>

namespace {
    enum class Status { Passed, Warning, Failed };

    struct Result {
        Status status;
        std::string detail;
        uintptr_t function = 0;
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

    bool IsFunction(const uintptr_t address)
    {
        return StartsWithPrologue(address) || GamePatterns::IsFunctionStart(address);
    }

    Result Function(const uintptr_t function, const char* what)
    {
        if (!function) {
            return {Status::Failed, std::string(what) + " not found"};
        }
        if (!IsFunction(function)) {
            return {Status::Failed, Hex(function) + " is not a function start"};
        }
        return {Status::Passed, Hex(function), function};
    }

    Result CheckFunctionPattern(const GamePatterns::FunctionPattern& pattern)
    {
        const auto function = GamePatterns::FindFunction(pattern);
        auto result = Function(function, "no anchor resolved (or the anchors disagree)");
        if (result.status == Status::Failed) {
            return result;
        }
        auto anchors = 0;
        auto agreeing = 0;
        for (const auto& anchor : pattern.anchors) {
            if (!anchor.bytes) {
                continue;
            }
            ++anchors;
            const auto match = GamePatterns::FindUnique(anchor);
            if (match && match < function + 5) {
                return {Status::Failed, "anchor at " + Hex(match) + " overlaps the 5 bytes a hook overwrites"};
            }
            agreeing += GamePatterns::FindFunction(anchor) == function;
        }
        result.detail += ", " + std::to_string(agreeing) + "/" + std::to_string(anchors) + " anchors";
        if (agreeing != anchors) {
            result.status = Status::Warning;
            result.detail += " (update the anchor that no longer matches)";
        }
        return result;
    }

    Result CheckSendDialogOpcode()
    {
        const auto sender = GamePatterns::FindSendDialogSender(GamePatterns::FindSendAgentDialog());
        if (!IsFunction(sender)) {
            return {Status::Failed, "tail call target " + Hex(sender) + " is not a function start"};
        }
        const auto opcode = GamePatterns::ReadSenderOpcode(sender);
        if (!GamePatterns::IsPlausibleSendDialogOpcode(opcode)) {
            return {Status::Failed, "opcode " + Hex(opcode) + " is implausible"};
        }
        return {Status::Passed,
                "send dialog " + Hex(opcode) + ", interact gadget " + Hex(opcode + GamePatterns::interact_gadget_after_send_dialog) +
                    ", signpost dialog " + Hex(opcode + GamePatterns::send_signpost_dialog_after_send_dialog),
                sender};
    }

    Result CheckGameServerObject()
    {
        const auto object = GamePatterns::FindGameServerObject();
        if (!GW::Scanner::IsValidPtr(object, GW::ScannerSection::Section_DATA)) {
            return {Status::Failed, "pointer " + Hex(object) + " is not in .data"};
        }
        return {Status::Passed, Hex(object)};
    }

    const Check game_checks[] = {
        {"SendAgentDialog (DirectDialog)", [] { return Function(GamePatterns::FindSendAgentDialog(), "caller pattern"); }},
        {"CtoS SendPacket", [] { return Function(GamePatterns::FindSendPacket(), "assertion"); }},
        {"CtoS game server object", CheckGameServerObject},
        {"CtoS send dialog opcode", CheckSendDialogOpcode},
        {"Ping (call target)", [] {
             const GamePatterns::CallTargetPattern call_target;
             if (!call_target.pattern.anchors[0].bytes) {
                 return Result{Status::Failed, "send dialog opcode not found, so the call-target opcode is unknown"};
             }
             auto result = CheckFunctionPattern(call_target.pattern);
             result.detail += ", opcode " + Hex(static_cast<uint8_t>(call_target.bytes[3]));
             return result;
         }},
        {"SideWalk", [] { return CheckFunctionPattern(GamePatterns::SideWalk); }},
        {"FindPath", [] { return CheckFunctionPattern(GamePatterns::FindPath); }},
    };

    bool Report(const char* name, const Result& result)
    {
        const char* label = result.status == Status::Passed ? " OK " : result.status == Status::Warning ? "WARN" : "FAIL";
        std::printf("  [%s] %-32s %s\n", label, name, result.detail.c_str());
        return result.status != Status::Failed;
    }

    std::vector<Result> RunGameChecks()
    {
        std::vector<Result> results;
        for (const auto& check : game_checks) {
            results.push_back(check.run());
        }
        return results;
    }

    struct SimulatedHook {
        uint8_t* address;
        uint8_t original[5];
    };

    bool WriteJump(uint8_t* address, const uint8_t (&bytes)[5])
    {
        DWORD protection;
        if (!VirtualProtect(address, sizeof(bytes), PAGE_EXECUTE_READWRITE, &protection)) {
            return false;
        }
        std::memcpy(address, bytes, sizeof(bytes));
        VirtualProtect(address, sizeof(bytes), protection, &protection);
        return true;
    }

    bool CheckWithHookedStarts(const std::vector<Result>& unhooked)
    {
        std::vector<SimulatedHook> hooks;
        for (const auto& result : unhooked) {
            if (!result.function) {
                continue;
            }
            SimulatedHook hook{reinterpret_cast<uint8_t*>(result.function), {}};
            std::memcpy(hook.original, hook.address, sizeof(hook.original));
            const auto relative = static_cast<int32_t>(0x1000 - (result.function + 5));
            uint8_t jump[5] = {0xE9};
            std::memcpy(jump + 1, &relative, sizeof(relative));
            if (!WriteJump(hook.address, jump)) {
                return Report("lookups with hooked function starts", {Status::Failed, "could not patch " + Hex(result.function)});
            }
            hooks.push_back(hook);
        }

        const auto hooked = RunGameChecks();
        for (auto it = hooks.rbegin(); it != hooks.rend(); ++it) {
            WriteJump(it->address, it->original);
        }

        for (size_t i = 0; i < unhooked.size(); ++i) {
            if (hooked[i].detail != unhooked[i].detail || hooked[i].status != unhooked[i].status) {
                return Report("lookups with hooked function starts", {Status::Failed, std::string(game_checks[i].name) + " changed to: " + hooked[i].detail});
            }
        }
        return Report("lookups with hooked function starts", {Status::Passed, "identical with " + std::to_string(hooks.size()) + " starts overwritten by a jump"});
    }

    bool CheckWithOneAnchorBroken(const GamePatterns::FunctionPattern& pattern)
    {
        const auto label = std::string(pattern.name) + " with one anchor broken";
        const auto function = GamePatterns::FindFunction(pattern);
        if (!function) {
            return true;
        }
        for (size_t broken = 0; broken < std::size(pattern.anchors); ++broken) {
            const auto& anchor = pattern.anchors[broken];
            std::string bytes(anchor.bytes, std::strlen(anchor.mask));
            const auto fixed = std::strchr(anchor.mask, 'x') - anchor.mask;
            bytes[fixed] = static_cast<char>(~bytes[fixed]);
            auto damaged = pattern;
            damaged.anchors[broken].bytes = bytes.data();
            const auto broken_anchor = GamePatterns::FindFunction(damaged.anchors[broken]);
            const auto with_other_anchor = GamePatterns::FindFunction(damaged);
            if (broken_anchor || with_other_anchor != function) {
                return Report(label.c_str(), {Status::Failed, "anchor " + std::to_string(broken + 1) + " broken: it resolved to " + Hex(broken_anchor) + ", lookup gave " + Hex(with_other_anchor)});
            }
        }
        return Report(label.c_str(), {Status::Passed, "still " + Hex(function) + " with either anchor broken"});
    }

    bool CheckGwca(HMODULE gwca)
    {
        const auto stub = reinterpret_cast<uintptr_t>(GetProcAddress(gwca, "GW_Agents_GetAgentMatchesFlags"));
        const auto implementation = GamePatterns::Gwca::GetAgentMatchesFlagsImplementation(stub);
        std::printf("gwca.dll\n");
        return Report("GetAgentMatchesFlags implementation", implementation ? Result{Status::Passed, Hex(implementation)} : Result{Status::Failed, "export stub bytes changed"});
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
        const auto results = RunGameChecks();
        auto passed = true;
        for (size_t i = 0; i < results.size(); ++i) {
            passed &= Report(game_checks[i].name, results[i]);
        }
        passed &= CheckWithHookedStarts(results);
        passed &= CheckWithOneAnchorBroken(GamePatterns::SideWalk);
        passed &= CheckWithOneAnchorBroken(GamePatterns::FindPath);
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
