# Agent guide: speedrun plugins for GWToolbox++

Guild Wars speedrunning plugins that GWToolbox++ loads at runtime. This file is the context an agent needs before touching the code. Read it fully once; the pitfalls at the end were each learned the hard way.

## Ground rules

- **The toolbox's `AGENTS.md` applies here too** (one level up, in the GWToolbox++ checkout). Most important: **no comments** except a genuine *why* the code can't express, at most two lines. Delete violating comments in files you touch.
- **Never change a serialization format** (plugin settings, SST script exports, SkinChanger strings, GWSplits data). Users have years of scripts and settings. If a format change is truly needed, it goes through SST's `currentVersion` / `SerializationIncrement` and needs explicit approval and testing with real user data.
- **Preserve old behaviour.** These plugins are in maintenance mode. A fix should not change what a plugin does unless asked. If you suspect a bug in existing behaviour, flag it instead of silently "fixing" it.
- **Debug-only tooling goes behind `#ifndef NDEBUG`.** Release builds must contain nothing that isn't user-facing (no debug commands, no diagnostic logging, no dump writers).
- **Plugins can't be unit tested.** The game is the test environment. Ask the user to verify in-game, and never claim a gameplay feature works because it compiles.
- **Push only with approval.** This repo is public (`origin` = GitHub `JaborGW/toolbox-plugins`). Commit locally freely; ask before every push. Never push the surrounding toolbox checkout.
- **Never commit a game client.** `test-data/` is git-ignored; keep it that way.

## Repository layout

```
CMakeLists.txt         all targets (plugins, shared libs, pattern tests)
Common/                code shared between plugins (header-only unless noted)
Scripting/             script engine shared by SpeedrunScriptingTools and GWSplits
<PluginName>/          one folder per plugin DLL (folder name = target name = DLL name)
tests/PatternTests.cpp offline checks of every game-function lookup
test-data/Gw.exe       (ignored) client the pattern tests run against
```

Plugins: AgentPopTimer, DeathPenaltyTimer, DhuumCalculator, DialogsWindow, FlatBowRangeIndicator, Follow, GWSplits, LeechSignetCancel, PathingVisualizer, PitsSoulsWindow, ProjectileIndicator, RawDialogs, ShadowstepPredictor, SkinChanger, Slowload, SpeedrunScriptingTools (SST), TargetEverything. The README has a one-line description of each.

## Relationship to GWToolbox++

This repo is cloned into `plugins/speedrun` **inside a GWToolbox++ checkout** (branch `dev`), excluded via the toolbox's `.git/info/exclude`. The toolbox's `cmake/gwtoolboxdll_plugins.cmake` needs an `add_subdirectory(plugins/speedrun)` guarded by `if(EXISTS ...)`. See the README for setup.

Plugins link against the toolbox's `plugin_base` (`plugins/Base/` in the toolbox repo): `ToolboxPlugin`, `ToolboxUIPlugin` (a plugin with an ImGui window), `PluginUtils`, `dllmain.cpp`.

**Compatibility:** plugins only load in a toolbox built from the same plugin SDK. `dev` exports a C function table with an ABI version check (`PluginModule.cpp`), binds GWCA 4.12's runtime API in `DllMain` (`GW::Runtime::BindGWCA`), and is built with llvm-mingw. GWToolbox release 8.34 (`master`) uses a C++ vtable interface, an older GWCA and MSVC, so these plugins **cannot load in it**. Don't try to support both.

## Building

One toolchain: **llvm-mingw** (clang, GNU-style flags, MinGW-w64 runtime). No MSVC, so no `corecrt_*` internals or MSVC-only flags. Run the toolbox bootstrap once (`bootstrap/README.md`), then from the toolbox root:

```
python scripts/build.py --preset debug --target DialogsWindow
python scripts/build.py --preset release --target SkinChanger --target SpeedrunScriptingTools
```

- Presets: `debug`, `relwithdebinfo`, `release`. Output goes to the toolbox's `bin/<Config>/`.
- **Without `--target`, only GWToolbox.exe is built, not the plugins.** Pass every plugin target you need (repeat `--target`).
- Ninja must be on `PATH` (Visual Studio ships one under `Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja`). The llvm-mingw toolchain lives in the toolbox's `.toolchain/`; it includes `llvm-symbolizer` and `llvm-objdump`.
- SST and GWSplits compile with exceptions (`GWTB_EXCEPTIONS`). GWSplits also defines `LiveSplitMode`, which switches parts of `Scripting/` to the split-timer variant.
- Plugins that draw in the 3D world link `speedrun_world_renderer` and depend on the toolbox's `compile_shaders` target (via the `add_speedrun_world_renderer` macro).
- New plugin: add a folder plus `add_speedrun_plugin(Name)` (it globs `*.h`/`*.cpp`) and link any shared libs it needs.

## Installing and testing in-game

- Copy the DLL to GWToolbox's `plugins` folder (`Documents/GWToolboxpp/<computer name>/plugins/`) and load it from the toolbox settings (Plugins section). Loading is manual, per plugin.
- **A loaded DLL is locked.** Ask the user to unload it (or close the game) before overwriting. Never overwrite the user's installed plugins without asking.
- Debug toolbox: `bin/Debug/GWToolbox.exe` from the toolbox build. Debug and release plugins both load in either toolbox build, but test what you ship.
- Toolbox log: `log.txt` in the same `GWToolboxpp/<computer name>/` folder. Plugins write there via the toolbox's `Log::`.
- Settings: `<plugins folder>/<Name>.json`. `LoadSetting` falls back to a legacy `<Name>.ini` **only if the JSON lacks the key**; restoring from an old ini needs the JSON deleted (with the plugin unloaded).
- Game client backups may exist for comparison (older `Gw.exe` versions, e.g. `D:\Spiele\gwclientbackup\<date>\`). **Never start, modify or overwrite them.** Read them in place or copy them first; `test-data/Gw.exe` is the working copy for static analysis.

## Guild Wars primer

Just enough game knowledge to read the code:

- **Instances:** every map load is a new instance. *Outposts* (towns, no combat) vs *explorable* areas. `GW::Map::GetInstanceType()`; during a load screen it is `Loading`. Lots of state (agent IDs, timers) resets per instance; plugins listen for `StoC::InstanceLoadFile` to reset.
- **Agents:** anything in the world (players, NPCs, enemies, items on the ground, gadgets like chests). `agent_id` is per instance. Living agents have a `player_number` that, for NPCs, is the **model ID** (stable game data, what `GameIds::ModelID` lists). `GetIsGadgetType()`, `GetIsItemType()`, `GetIsLivingType()` distinguish kinds. Allegiance tells ally/enemy/NPC/minipet etc.
- **Encoded strings:** game text is `wchar_t` encoded strings (`enc_name`, skill/map/quest names) that must be decoded asynchronously on the game thread. Use `Common/DecodedText.h`; never decode yourself into a `std::wstring` with only `reserve()` (length stays 0, see Pitfalls).
- **IDs:** skill IDs (`GW::Constants::SkillID`, `GameIds::SkillIdCount`), map IDs, quest IDs, item model IDs, model *file* IDs (SkinChanger: the actual 3D model). Users type IDs; `Common/GameIdInput.h` accepts decimal or `0x` hex.
- **Dialogs:** talking to an NPC opens a dialog window with buttons; each button carries a dialog ID that the client sends to the server. Quest dialogs are `0x800000 | quest_id << 8 | type`, type `01` take, `03` enquire, `04` enquire next, `05` recap, `06` enquire reward, `07` reward. **The server only accepts take/reward after the matching enquiry in the same conversation.** The client refuses to send dialogs in combat; an enquiry sent directly still works in combat and opens the offer window.
- **StoC / CtoS:** server-to-client packets (agent updates, chat, projectiles, quest updates...) and client-to-server packets (move, interact, dialog...). GWCA names StoC packets (`GW::Packet::StoC::*`) and lets you hook or emulate them. CtoS opcodes get renumbered by game updates.
- **UI messages:** the game's UI reacts to `GW::UI::UIMessage` values (`kDialogButton`, `kSendUseItem`, `kWriteToChatLog`...). GWCA lets you register callbacks on them; wparam/lparam meaning is message-specific.
- **Speedrunning context:** runs are timed, often in Underworld (UW), Fissure of Woe (FoW), Deep, Urgoz, DoA. Plugins automate or visualise things runners otherwise do by hand: precise timers (minipet repop, death penalty), scripted hotkeys, split timing, pathing previews, shadowstep landing predictions. Players care about frame-perfect behaviour; a "small" timing change can ruin a strategy.

## GWCA

GWCA is the library that wraps the game's memory and functions. The toolbox uses the **official binary GWCA 4.12**: headers in the toolbox's `Dependencies/GWCA/include`, the implementation is `gwca.dll` loaded at runtime. You can't patch GWCA; work around it in plugin code.

- **GWCA is closed source.** The only sources are the headers in `Dependencies/GWCA` and an old open checkout at [JaborGW/GWCA](https://github.com/JaborGW/GWCA). Patterns, offsets and packet layouts from the old checkout must be checked against a matching old client from the backups (see Installing), not assumed to hold for the current one.

- **Managers:** `GW::Agents`, `GW::Map`, `GW::Chat`, `GW::UI`, `GW::StoC`, `GW::GameThread`, `GW::Items`, `GW::Skillbar`, `GW::Quest`... Context accessors (`GW::GetWorldContext()`, `GetCharContext()`, `GetGameContext()`) return raw pointers into game memory that **can be null** (load screens, character select). Check every pointer every time; never cache them across frames.
- **Headers:** `GWCA/stdlib/*.h` are adapter headers for `std::function` overloads. Don't include both `GWCA/stdlib/X.h` and `GWCA/Managers/X.h` for the same manager in one translation unit; it fails to compile. `PluginUtils.h` needs `imgui.h` included first.
- **Hooks and callbacks:** a `GW::HookEntry` identifies a registration. `RegisterPacketCallback` / `RegisterPostPacketCallback`, `RegisterUIMessageCallback`, `Chat::CreateCommand`. Every registration must be removed in `SignalTerminate`/`Terminate`, or the game calls into an unloaded DLL. `HookStatus::blocked` stops the game from handling the event. The altitude argument orders callbacks: ≤ 0 fires before the game handles the event, > 0 after (default −0x8000). `RegisterPostPacketCallback` is deprecated in favour of a positive altitude; existing uses still work.
- **Function hooks** (`GW::Hook::CreateHook`/`EnableHooks`, MinHook underneath): wrap the detour in `GW::Hook::EnterHook()`/`LeaveHook()` so unloading waits for running detours. Other tools (and toolbox itself) hook game functions too, so a function's first bytes may already be a `jmp`.
- **`StoC::EmulatePacket(Packet<T>*)`** feeds a fake server packet to the game. The templated overload sets the header itself. The struct layout must match what the current client expects, or the game reads garbage.
- **Value dependencies:** `Common/GwcaExpectations.cpp` `static_assert`s the GWCA enum values the plugins rely on (UI message IDs, control actions, allegiances, generic value IDs, item IDs). When you depend on a new GWCA value by number, add it there; a GWCA update that renumbers it then fails the build.

## Threads

- **`Update(delta)` runs on the game thread** (the toolbox's game-loop callback). **`Draw(device)` runs in the render hook, not on the game thread** (`GW::GameThread::IsInGameThread()` is false).
- **Game functions must be called on the game thread.** Many read per-thread game state and silently misbehave or crash otherwise. From `Draw`/ImGui callbacks, use `GW::GameThread::Enqueue(...)`. `Common/DirectDialog.h` `Send` shows the pattern (call directly if already on the game thread, otherwise enqueue).
- Writing chat goes through `PluginChat::Write`, which enqueues for you.
- State shared between a UI callback and the game thread needs atomics or must be confined to one thread.

## Plugin architecture

Every plugin derives from **`SpeedrunPlugin<ToolboxPlugin>`** or **`SpeedrunPlugin<ToolboxUIPlugin>`** (`Common/SpeedrunPlugin.h`), except TargetEverything and LeechSignetCancel, which predate it. It adds:

- `Version()` (pure virtual): shown in the settings footer. Bump the minor version when releasing user-visible changes. It is *not* a serialization version.
- `HasEmptySettings()` (pure virtual): drives the **settings backups**. Whenever settings are saved and non-empty, a backup is written to `<plugins folder>/backups/<Name>/`. If a plugin loads with empty settings and backups exist, it tells the user in chat. `/restore <plugin> [recent|largest|list|<n>|help]` and a Backups tree in the settings restore them.
- `DrawPluginSettings()`: the plugin's own settings UI (`DrawSettings` is final).
- `UnavailableFeatures()`: names of features whose game function couldn't be found in this client. It's evaluated once after load, and a chat line tells the user. After a game update, this is how users find out what broke. Implement it for every feature that depends on a pattern.
- `CommandAlias()`, `HasBackups()`.

Settings use `LoadSetting("key", value)` / `SaveSetting("key", value)` from `ToolboxPlugin` (JSON with legacy ini fallback). Call `SpeedrunPlugin::LoadSettings`/`SaveSettings` around your own.

Lifecycle: `Initialize(ctx, allocator_fns, toolbox_dll)` → `LoadSettings` → per frame `Update`/`Draw` → `SignalTerminate` (unregister everything) → `CanTerminate` (wait for pending work) → `Terminate`. Always call the base implementation.

## Common/

| File | Use it for |
|---|---|
| `SpeedrunPlugin.h` | Plugin base (see above). |
| `GamePatterns.h` | **Every** game-memory lookup. Nothing scans memory anywhere else. |
| `GwcaExpectations.cpp` | `static_assert`s on GWCA values (compiled into every plugin). |
| `DecodedText.h` | Decoding encoded game strings: `Get(encoded)`, `FromId(enc_id)`, `SkillName(id)`, `Utf8(view)`. Per-DLL cache with stable storage; returns empty while pending. |
| `DirectDialog.h` | Sending dialogs past the client's checks (`Send`) and the in-combat quest flow (`QuestDialogSender`: raw enquiry, then click the take/reward button in the offer window, with a timeout and double-click guard). |
| `GameIdInput.h` | `Parse` decimal/`0x` hex IDs; `Draw` an ImGui input for an ID (dialog IDs shown as hex). Display only: **values are still serialized as before (decimal).** |
| `GameIds.h` | Model/item IDs GWCA doesn't name. |
| `Hotkeys.h` | Hotkey struct, `DrawSelector`, `PressedKey`/`CurrentModifier` for WndProc handling. |
| `PluginChat.h` | Chat output (`Write`, `ReportUnavailable`). |
| `PluginGui.h`, `PluginFooter.h` | Small ImGui helpers (`ShowHelp`), settings footer. |
| `PluginBackups.h` | Backup files behind `SpeedrunPlugin`. |
| `InstanceProgress.h` | The instance progress bar value (`CharContext::progress_bars[0]`). |
| `RingTimer.*` | Circular countdown overlay with an icon (AgentPopTimer, DeathPenaltyTimer). |
| `WorldRenderer.*` | Drawing shapes on the 3D world (see Rendering). |
| `ModelNames.*` | Names for model IDs (SST, ProjectileIndicator). |

Before adding a helper, check here and in the toolbox's `Utils/` (`TextUtils.h`, `GuiUtils.h`).

## Game function patterns

Some features call or hook game functions GWCA doesn't expose. They are found by **byte patterns** in `Common/GamePatterns.h`, and nowhere else.

- An `Anchor` is bytes plus a mask (`x` = must match, `?` = wildcard). A `FunctionPattern` has **two independent anchors**; each must match exactly once (`FindUnique`), and if both match they must resolve to the same function, otherwise the lookup fails (returns 0) rather than guessing.
- `FunctionStartBefore` walks back (≤ 0x200 bytes) to a prologue (`55 8B EC` / `55 89 E5`) or a hook's `E9` jump out of `.text`, preceded by padding, a `ret`, or a switch jump table.
- Other lookups: by assertion string (`FindAssertion`, e.g. `MsgConn.cpp` for SendPacket), by decoding a `call` from a known caller (decode `E8 rel32` manually; GWCA's `FunctionFromNearCall` follows `E9` jumps and lands in someone's hook), and by **deriving opcodes** (Ping's opcode = SendDialog's opcode − 0x18) instead of hard-coding them, since game updates renumber opcodes.
- **Choose anchors so they survive game updates:** wildcard relative call/jump targets, displacements and immediates that are addresses; avoid the first 5 bytes of a function (other tools' hooks overwrite them); keep ≥ 12 fixed bytes; check uniqueness across all available client versions.
- **GWCA's scanner reads the file image of the module, not live memory**, so other tools' hooks don't break anchor matches. Only the walk-back and opcode reads see live (possibly hooked) bytes.
- Every lookup returns 0 on failure. Callers must handle that and report through `UnavailableFeatures()`. Never dereference an unverified result.
- Raw struct offsets GWCA doesn't map (e.g. PathingVisualizer reads `AgentMovement + 34 floats`) can't be verified offline. Keep them as named constants with a one-line why.

### Pattern tests

`tests/PatternTests.cpp` → `speedrun_pattern_tests`. It loads the pinned `gwca.dll` (`GW::Runtime::LoadGWCA`), maps a client with `LoadLibraryExW(DONT_RESOLVE_DLL_REFERENCES)` (relocated, nothing runs), initialises GWCA's scanner on it, and runs the plugins' real lookups through GWCA's real matcher. Checks: exactly one match per anchor, anchor agreement, prologue at the start, game-server pointer in `.data`, plausible opcodes, hooked-start simulation, each anchor broken in turn.

```
python scripts/build.py --preset debug --target run_speedrun_pattern_tests   # uses test-data/Gw.exe
bin/Debug/speedrun_pattern_tests.exe <Gw.exe or folder> ...                  # matrix across clients
```

**Every new lookup in `GamePatterns.h` gets a check in the test.** Run it after every game update and after touching any pattern.

## Rendering

- **ImGui** for windows and settings. The toolbox shares its ImGui context and allocator with each plugin through `Initialize`; never create your own context. `ToolboxUIPlugin` provides the window, its visibility and its lock/close/title settings.
- **Overlays in screen space** (RingTimer): ImGui draw lists plus D3D9 textures created from embedded PNGs. Textures belong to the device: release them in `Terminate` (`RingTimer::ReleaseTexture`).
- **3D world drawing** (`WorldRenderer`, used by PathingVisualizer and ProjectileIndicator): D3D9 with the toolbox's compiled shaders. Circles, ellipses and a polyline at `GW::GamePos` (x, y, plane `zplane`), triangulated with earcut, with optional time-to-live per shape. Call `WorldRenderer::Draw(device)` from the plugin's `Draw` and `Terminate()` on unload. Shader compilation differs on Linux builds (vkd3d-shader), so check rendering changes in-game.
- Draw is the render thread: read game state there only through pointers you null-check, and do anything that calls game functions via the game thread.

## Game memory manipulation (handle with care)

These plugins go beyond reading:

- **SkinChanger** changes item skins by writing item fields (`model_file_id`, `interaction`) and transforms minipets/NPCs by **emulating StoC packets** (`NpcGeneralStats`, `NPCModelFile`, `AgentScale`, `AgentModel`, `AgentName`). Client-side only, but a wrong struct layout corrupts game state. Minipet transmogs apply on map load.
- **TargetEverything** hooks the game's agent-filter implementation behind GWCA's export (found via `GamePatterns::Gwca::GetAgentMatchesFlagsImplementation`, because GWCA's own target check calls the implementation directly).
- **Slowload** holds the `InstanceLoadFile` packet (blocks it) and re-emulates it later.
- **RawDialogs** `/openchest` and the CtoS fallback build raw CtoS packets via the game's SendPacket. Opt-in only; sending packets the client wouldn't send **can flag the account**. Keep such features off by default with a warning.
- **DirectDialog** calls the game's own SendAgentDialog, bypassing GWCA's client-side checks (it's a plugin setting in DialogsWindow).

Rules: do game writes on the game thread; validate every pointer and index; restore what you change on unload where possible; prefer GWCA's API over raw writes.

## Scripting / SST

- `Scripting/` holds the conditions, actions and characteristics with their `*Impls` and `*IO` (serialization), `Enums.h`, `InstanceInfo`/`QuestInfo` (per-instance and quest tracking), and `ScriptVariables`.
- Scripts serialize into a compact text format, encoded for export strings. **Version history lives in `currentVersion` (`SpeedrunScriptingTools.cpp`)** with old-format readers in `SerializationIncrement.*`. **Enums in `Enums.h` are append-only**: their numeric values are serialized.
- Quest/objective conditions match decoded, localized names (case-sensitive substring). The user decided to keep that (not ID-based).
- GWSplits compiles the same `Scripting/` with `LiveSplitMode`.

## Style specifics beyond AGENTS.md

- Prefer `std::format` for wide strings. **Never use `%s` with wide printf (`swprintf`, `Log::LogW`)**: under the MinGW runtime `%s` is narrow; use `%ls` or `std::format`.
- Wide/UTF-8 conversion: `PluginUtils::StringToWString`/`WStringToString`, or `DecodedText::Utf8`.
- Names and IDs shown to the user come from the game (decode them) or GWCA constants, not hard-coded English where avoidable.
- Keep plugin code self-contained per folder; move to `Common/` only when a second plugin needs it.

## Pitfalls (learned the hard way)

- **`std::wstring` + `reserve()` as a decode buffer**: the async decoder writes into the storage, but `size()` stays 0, so the text "disappears". Use `DecodedText`.
- **Dangling game pointers**: Be careful with queueing raw `GW::Quest*`, `GW::Agent*` or array element pointers for later; game arrays reallocate. Queue IDs and look them up when you process them.
- **Clearing maps while async decodes are pending** is a use-after-free; decode into stable storage (`DecodedText` never frees entries).
- **Calling game functions from `Draw`** (render thread) crashed in combat. Enqueue onto the game thread.
- **Raw quest take without enquiry** is silently ignored by the server. **Double-sending** quest dialogs crashed the game; guard against repeated clicks.
- **Python-generated source edits** mangle C++ escapes (`\x..`, `\0`, `\n`) and once wrote literal NUL bytes into a source file. Use exact-text edits for such files and check for `\x00` afterwards.
- **GWCA's mask lengths**: a mask shorter than its pattern silently ignores the trailing bytes.
- **Legacy ini fallback** only applies when the JSON lacks the key (see Installing).
- **`build.py` without `--target`** doesn't rebuild plugins.
- **Locked DLLs**: copying over a loaded plugin fails, or worse, mixes versions. Always confirm the installed DLL's hash matches the build you mean to test.

## Debugging crashes

1. Reproduce with Debug toolbox and Debug plugins; note what was loaded and what the player was doing (combat? which dialog? map load?).
2. Check `log.txt` and the toolbox crash dialog (toolbox writes its own crash dumps).
3. If the crash window stays open, a full minidump of `Gw.exe` can be taken externally (`MiniDumpWriteDump` from a PowerShell `Add-Type` helper) and analysed with Python (`minidump` + `capstone`) or WinDbg. Resolve toolbox/plugin frames with `llvm-symbolizer` against the Debug DLLs.
4. Exit code `0xC0000374` (`STATUS_HEAP_CORRUPTION`) or a crash inside `RtlFreeHeap` means **the heap was corrupted earlier**; the crashing stack is just whoever freed next (often chat-log writes) and is not the culprit. Bisecting plugins needs many repetitions, because such crashes are intermittent. The next tool is **page heap** (`gflags`/IFEO `GlobalFlag` for `Gw.exe`), which faults at the corrupting write. It's a system setting and needs admin, so only with the user's approval.

## Workflow summary

1. Read the relevant plugin folder and `Common/` first. Check `git log` for context.
2. Make the minimal change; keep behaviour and formats.
3. Build Debug for the target(s); run the pattern tests if patterns changed.
4. Ask the user to unload the plugin, install the build, and test in-game. Describe exactly what to test.
5. Commit after confirmation (end with the attribution lines your harness asks for). Ask before pushing.
