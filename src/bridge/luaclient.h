#pragma once

#include <stop_token>
#include <string>

// 连游戏内 slotbound_lua.dll 的管道，发一段 Lua 代码，拿回结果。
namespace bridge {

struct Result {
    bool ok = false;      // false：连不上 / 超时 / Lua 报错，text 是原因
    std::string text;     // 成功时是各返回值用 \t 连起来
    bool wrongProcess = false;
};

// DLL 在不在（管道存在）
bool Available();

// 同步执行，最多等 timeoutMs（DLL 自己还有 5 秒的主线程等待上限）
Result Execute(const std::string& code, unsigned timeoutMs = 8000, unsigned long expectedPid = 0,
    std::stop_token stop = {});

} // namespace bridge
