# Speedrun plugins for GWToolbox++

Plugins for tool-assisted Guild Wars speedruns, built against upstream GWToolbox++ and its binary GWCA.

## Setup

1. Check out GWToolbox++ (`dev`) and run its bootstrap (`bootstrap/README.md`).
2. Clone this repository to `plugins/speedrun` inside the GWToolbox++ checkout, and add `/plugins/speedrun/` to the toolbox's `.git/info/exclude`.
3. In the toolbox, `cmake/gwtoolboxdll_plugins.cmake` needs:

   ```cmake
   if(EXISTS "${PROJECT_SOURCE_DIR}/plugins/speedrun/CMakeLists.txt")
       add_subdirectory(plugins/speedrun)
   endif()
   ```

4. Build a plugin with `python scripts/build.py --preset debug --target <Plugin>`; the DLL lands in the toolbox's `bin/<Config>/`.

`Common/` holds the code shared between plugins.

## Pattern tests

All memory patterns, assertion lookups and GWCA-internal assumptions the plugins rely on live in `Common/GamePatterns.h`. `tests/PatternTests.cpp` maps a game client with `LoadLibraryEx(DONT_RESOLVE_DLL_REFERENCES)` (nothing runs), points GWCA's own `GW::Scanner` at it and runs the same lookups the plugins use in game: exact match counts, function prologues, the game server pointer, the derived CtoS opcodes, and the `gwca.dll` export stub TargetEverything relies on.

1. Copy the current `Gw.exe` to `test-data/Gw.exe` (ignored by git). The test fails if no client is found.
2. `python scripts/build.py --preset debug --target run_speedrun_pattern_tests` builds and runs it; or run `bin/Debug/speedrun_pattern_tests.exe [Gw.exe or folder ...]` to check other clients, e.g. a folder of client backups.

`Common/GwcaExpectations.cpp` is compiled into every plugin and `static_assert`s the GWCA constant values the plugins depend on numerically, so a GWCA update that renumbers them fails the build.
