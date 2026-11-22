add_library(speedrun_plugin_common INTERFACE)
target_sources(speedrun_plugin_common INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/PluginBackups.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/PluginChat.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/PluginFooter.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/SpeedrunPlugin.h")
target_include_directories(speedrun_plugin_common INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/Common")
target_link_libraries(speedrun_plugin_common INTERFACE shell32)

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

add_speedrun_plugin(FlatBowRangeIndicator)
add_speedrun_plugin(Follow)
add_speedrun_plugin(LeechSignetCancel)
