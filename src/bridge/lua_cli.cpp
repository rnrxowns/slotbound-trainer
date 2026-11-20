// slotbound_lua.exe：在游戏里执行 Lua。排查 / 探索用。
//   slotbound_lua.exe "return coins"
//   slotbound_lua.exe -f script.lua
// 输出 UTF-8；出错时退出码 1。
#include <windows.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "luaclient.h"

static std::string ToUtf8(const wchar_t* s)
{
    const int len = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    std::string out(len > 0 ? len - 1 : 0, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), len, nullptr, nullptr);
    return out;
}

int wmain(int argc, wchar_t** argv)
{
    SetConsoleOutputCP(CP_UTF8);
    if (argc < 2) {
        fputs("사용법: slotbound_luacmd \"Lua 코드\" | -f 파일.lua\n", stderr);
        return 2;
    }
    std::string code;
    if (wcscmp(argv[1], L"-f") == 0 && argc > 2) {
        std::ifstream f(argv[2], std::ios::binary);
        if (!f) {
            fputs("파일을 읽지 못했습니다.\n", stderr);
            return 2;
        }
        std::stringstream ss;
        ss << f.rdbuf();
        code = ss.str();
    } else {
        code = ToUtf8(argv[1]);
    }
    const bridge::Result r = bridge::Execute(code);
    fwrite(r.text.data(), 1, r.text.size(), r.ok ? stdout : stderr);
    fputc('\n', r.ok ? stdout : stderr);
    return r.ok ? 0 : 1;
}
