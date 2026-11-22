add_library(speedrun_plugin_common INTERFACE)
target_sources(speedrun_plugin_common INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/DirectDialog.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/GameIds.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/Hotkeys.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/PluginBackups.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/PluginChat.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/PluginFooter.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/PluginGui.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/SpeedrunPlugin.h")
target_include_directories(speedrun_plugin_common INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/Common")
target_link_libraries(speedrun_plugin_common INTERFACE shell32)

add_library(speedrun_ring_timer INTERFACE)
target_sources(speedrun_ring_timer INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/RingTimer.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/RingTimer.cpp")
target_link_libraries(speedrun_ring_timer INTERFACE directxtexloader)

add_library(speedrun_model_names INTERFACE)
target_sources(speedrun_model_names INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/ModelNames.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/ModelNames.cpp")

add_library(speedrun_world_renderer INTERFACE)
target_sources(speedrun_world_renderer INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/WorldRenderer.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/WorldRenderer.cpp")
target_include_directories(speedrun_world_renderer INTERFACE ${EARCUT_HPP_INCLUDE_DIRS})

add_library(speedrun_imgui_stdlib INTERFACE)
target_sources(speedrun_imgui_stdlib INTERFACE
    "${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.h"
    "${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp")

macro(add_speedrun_world_renderer PLUGIN)
    target_link_libraries(${PLUGIN} PRIVATE speedrun_world_renderer)
    add_dependencies(${PLUGIN} compile_shaders)
endmacro()

macro(add_speedrun_plugin PLUGIN)
    add_library(${PLUGIN} SHARED)
    file(GLOB SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/${PLUGIN}/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/${PLUGIN}/*.cpp")
    target_sources(${PLUGIN} PRIVATE ${SOURCES})
    target_include_directories(${PLUGIN} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/${PLUGIN}")
    target_link_libraries(${PLUGIN} PRIVATE plugin_base)
    set_target_properties(${PLUGIN} PROPERTIES PREFIX "" FOLDER "plugins/")
    target_link_libraries(${PLUGIN} PRIVATE speedrun_plugin_common)
endmacro()

add_speedrun_plugin(AgentPopTimer)
target_link_libraries(AgentPopTimer PRIVATE speedrun_ring_timer)
add_speedrun_plugin(DialogsWindow)
add_speedrun_plugin(FlatBowRangeIndicator)
add_speedrun_plugin(DeathPenaltyTimer)
target_link_libraries(DeathPenaltyTimer PRIVATE speedrun_ring_timer)
add_speedrun_plugin(Follow)
add_speedrun_plugin(LeechSignetCancel)
add_speedrun_plugin(PathingVisualizer)
add_speedrun_world_renderer(PathingVisualizer)
add_speedrun_plugin(PitsSoulsWindow)
add_speedrun_plugin(ProjectileIndicator)
target_link_libraries(ProjectileIndicator PRIVATE speedrun_model_names)
add_speedrun_world_renderer(ProjectileIndicator)
add_speedrun_plugin(RawDialogs)
add_speedrun_plugin(ShadowstepPredictor)
add_speedrun_plugin(SkinChanger)
target_link_libraries(SkinChanger PRIVATE speedrun_imgui_stdlib)
add_speedrun_plugin(Slowload)
add_speedrun_plugin(TargetEverything)
