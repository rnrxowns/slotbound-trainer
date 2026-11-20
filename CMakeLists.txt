cmake_minimum_required(VERSION 3.20)
project(slotbound_trainer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 静态链接 VC++ 运行库：exe 拷到没装运行库的电脑上也能直接跑
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# 便携版：配置文件用 exe 旁边的 trainer.ini，而不是仓库里的（build.ps1 -Dist 会打开）
option(SB_PORTABLE "Use trainer.ini next to the exe" OFF)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)

# 源码分层（include 都从 src/ 写起，如 "game/options.h"）：
#   app/     配置、快捷键
#   core/    外部读写进程内存、LuaJIT 布局、Lua 全局数值（金币等）
#   bridge/  注入的 DLL、注入器、命名管道客户端
#   game/    经 Lua 桥改游戏数据的编辑器（词条、刻印、觉醒、等级、物品、老虎机）
#   ui/      覆盖层、通用控件、菜单各页
set(APP_SOURCES
    src/main.cpp
    src/app/config.cpp
    src/core/process.cpp
    src/core/luajit.cpp
    src/core/luavar.cpp
    src/bridge/luaclient.cpp
    src/bridge/injector.cpp
    src/bridge/embedded_dll.cpp
    src/game/lua_editor.cpp
    src/game/options.cpp
    src/game/imprints.cpp
    src/game/awakening.cpp
    src/game/items.cpp
    src/game/unitlevel.cpp
    src/game/slotctl.cpp
    src/ui/overlay.cpp
    src/ui/widgets.cpp
    src/ui/labels.cpp
    src/ui/menu.cpp
    src/ui/page_resources.cpp
    src/ui/page_unit.cpp
    src/ui/unit_options.cpp
    src/ui/unit_imprints.cpp
    src/ui/unit_awakening.cpp
    src/ui/page_items.cpp
    src/ui/page_slot.cpp
    src/ui/page_system.cpp
)

set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

# WIN32 = 窗口程序，不弹控制台
add_executable(slotbound_trainer WIN32 ${APP_SOURCES} ${IMGUI_SOURCES})
set_target_properties(slotbound_trainer PROPERTIES OUTPUT_NAME slotbound_trainer_ko)

target_include_directories(slotbound_trainer PRIVATE ${CMAKE_SOURCE_DIR}/src ${IMGUI_DIR} ${IMGUI_DIR}/backends)

target_compile_definitions(slotbound_trainer PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    # 默认配置文件：仓库里的 config/trainer.ini；也可以在命令行第一个参数传别的路径
    SB_DEFAULT_CONFIG="${CMAKE_SOURCE_DIR}/config/trainer.ini"
)

if(SB_PORTABLE)
    target_compile_definitions(slotbound_trainer PRIVATE SB_PORTABLE)
endif()

# /utf-8：源码里的中文字符串按 UTF-8 编译，ImGui 直接能显示
target_compile_options(slotbound_trainer PRIVATE /utf-8 /MP)
set_source_files_properties(${APP_SOURCES} PROPERTIES COMPILE_OPTIONS "/W4")

target_link_libraries(slotbound_trainer PRIVATE d3d11 dxgi dwmapi)

# 注入用的 slotbound_lua.dll 编进修改器 exe（发出去只要一个 exe），注入前由 embedded_dll.cpp 释放出来。
# DLL 重编后这里跟着重新生成。
set(LUA_DLL_BLOB ${CMAKE_BINARY_DIR}/generated/lua_dll_blob.cpp)
add_custom_command(
    OUTPUT ${LUA_DLL_BLOB}
    COMMAND ${CMAKE_COMMAND} -DIN=$<TARGET_FILE:slotbound_lua> -DOUT=${LUA_DLL_BLOB} -DNAME=kLuaDll
            -P ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
    DEPENDS slotbound_lua $<TARGET_FILE:slotbound_lua> ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
    COMMENT "Embedding slotbound_lua.dll"
    VERBATIM)
target_sources(slotbound_trainer PRIVATE ${LUA_DLL_BLOB})

# 诊断工具：命令行里定位 Lua 全局变量、打印 / 写入，开发期核对用
add_executable(slotbound_probe src/core/probe.cpp src/core/process.cpp src/core/luajit.cpp)
target_compile_definitions(slotbound_probe PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(slotbound_probe PRIVATE /utf-8 /W4)

# ---- 注入部分（用户 2026-11-14 同意注入）：在游戏主线程执行 Lua ----
set(BRIDGE_DEFS UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

# 注入进游戏的 DLL：IAT 挂钩 love.dll → lua51.dll!lua_pushnumber，经命名管道收 Lua 代码执行
add_library(slotbound_lua SHARED src/bridge/bridge_dll.cpp)
target_compile_definitions(slotbound_lua PRIVATE ${BRIDGE_DEFS})
target_compile_options(slotbound_lua PRIVATE /utf-8 /W4)

# 注入 / 卸载
add_executable(slotbound_inject src/bridge/inject.cpp src/bridge/injector.cpp)
target_compile_definitions(slotbound_inject PRIVATE ${BRIDGE_DEFS})
target_compile_options(slotbound_inject PRIVATE /utf-8 /W4)

# 命令行执行 Lua（探索 / 排查用）
add_executable(slotbound_lua_cli src/bridge/lua_cli.cpp src/bridge/luaclient.cpp)
set_target_properties(slotbound_lua_cli PROPERTIES OUTPUT_NAME slotbound_luacmd)
target_compile_definitions(slotbound_lua_cli PRIVATE ${BRIDGE_DEFS})
target_compile_options(slotbound_lua_cli PRIVATE /utf-8 /W4)
