# Speedrun plugins for GWToolbox++

Plugins for Guild Wars speedrunning, built for [GWToolbox++](https://github.com/gwdevhub/GWToolboxpp).

## Plugins

| Plugin | What it does |
|---|---|
| SpeedrunScriptingTools | Scripts made of triggers, conditions and actions (hotkeys, movement, casting, dialogs, pings, ...). |
| GWSplits | Split timer for runs, with splits completed automatically by script conditions. |
| AgentPopTimer | Timer for the minipet/GitB repop cooldown. |
| DeathPenaltyTimer | Time since your last resurrection. |
| DhuumCalculator | Dhuum fight: damage and rest rates, finish times, and when to switch ghosts to Ghostly Fury. |
| DialogsWindow | Fork of the old GWToolbox window. Sends dialogs by button, from favorites or by ID. |
| FlatBowRangeIndicator | Shows the flatbow range around your character. |
| Follow | `/follow` keeps following your target once it gets too far away. |
| Leech Signet Cancel | Cancels your current cast when Leech Signet is activated. |
| PathingVisualizer | Draws the next point your target is walking to. |
| PitsSouls | Status of the souls in the Pits. |
| ProjectileIndicator | Marks projectiles of chosen skills while they are in flight. |
| RawDialog | `/rawdialog <id>` sends a dialog directly; `/openchest` opens a chest. |
| Shadowstep Predictor | Predicts where Death's Charge and Shadow of Haste will take you. |
| SkinChanger | Changes item skins and transforms minipets, client-side only. |
| Slowload | Holds you in the loading screen until you press the hotkey again. |
| Target Everything | Lets you target agents the game normally treats as untargetable. |

## Install

Copy the plugin DLLs into GWToolbox's `plugins` folder and load them from the toolbox settings. After a game update, a plugin prints a line in chat at load if a feature it needs could not be found in the new client.

## Build

The plugins build as part of a GWToolbox++ checkout:

1. Check out GWToolbox++ (`dev`) and run its bootstrap (`bootstrap/README.md`).
2. Clone this repository to `plugins/speedrun` inside the toolbox checkout, and add `/plugins/speedrun/` to the toolbox's `.git/info/exclude`.
3. Add to the toolbox's `cmake/gwtoolboxdll_plugins.cmake`:

   ```cmake
   if(EXISTS "${PROJECT_SOURCE_DIR}/plugins/speedrun/CMakeLists.txt")
       add_subdirectory(plugins/speedrun)
   endif()
   ```

4. Build with `python scripts/build.py --preset debug --target <Plugin>` (or `--preset relwithdebinfo`). The DLL lands in the toolbox's `bin/<Config>/`.

`Common/` holds the code shared between plugins, `Scripting/` the script engine used by SpeedrunScriptingTools and GWSplits.

## Pattern tests

All lookups of game functions live in `Common/GamePatterns.h`. `tests/PatternTests.cpp` checks them against a game client without running it:

1. Copy the current `Gw.exe` to `test-data/Gw.exe` (ignored by git; never commit it).
2. Run `python scripts/build.py --preset debug --target run_speedrun_pattern_tests`, or `bin/Debug/speedrun_pattern_tests.exe <Gw.exe or folder> ...` to check other clients.

## History

These plugins used to live in [JaborGW/GWToolboxpp](https://github.com/JaborGW/GWToolboxpp) (branch `combined-plugin-branch`), built against a custom GWCA ([JaborGW/GWCA](https://github.com/JaborGW/GWCA)). That setup no longer works with current GWToolbox++, so the plugins were extracted into this repository and ported to the official, binary GWCA. They are now in maintenance mode.

This repository's history starts at the extraction. For the full development history, see the repositories above.

## License

MIT, see [LICENSE](LICENSE).
