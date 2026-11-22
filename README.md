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
