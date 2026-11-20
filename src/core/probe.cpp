// slotbound_probe：命令行诊断工具。定位一个 Lua 全局数字变量，打印过程和当前值，可选写入。
// 用法：slotbound_probe [变量名=coins] [--neighbor 邻居变量=displayCoins] [--set 数值] [--exe Slotbound.exe]

#include <windows.h>

#include <chrono>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "luajit.h"
#include "process.h"

int wmain(int argc, wchar_t** argv)
{
    SetConsoleOutputCP(CP_UTF8);

    std::string name = "coins";
    std::string neighbor = "displayCoins";
    std::wstring exe = L"Slotbound.exe";
    bool doSet = false;
    double setValue = 0.0;

    auto narrow = [](const wchar_t* w) {
        std::string s;
        for (; *w; ++w)
            s += static_cast<char>(*w); // 变量名都是 ASCII
        return s;
    };
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--neighbor" && i + 1 < argc)
            neighbor = narrow(argv[++i]);
        else if (a == L"--set" && i + 1 < argc) {
            doSet = true;
            setValue = _wtof(argv[++i]);
        } else if (a == L"--exe" && i + 1 < argc)
            exe = argv[++i];
        else
            name = narrow(argv[i]);
    }

    GameProcess game;
    if (!game.Attach(exe)) {
        std::setlocale(LC_CTYPE, ".UTF8");
        wprintf(L"프로세스를 찾지 못했습니다: %ls\n", exe.c_str());
        return 1;
    }
    printf("연결됨. PID %lu\n", game.Pid());

    const auto t0 = std::chrono::steady_clock::now();
    lj::Field field;
    std::string log;
    const bool ok = lj::LocateGlobalNumber(game, name, neighbor, field, log);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    printf("%s", log.c_str());
    printf("탐색 시간: %lld ms\n", static_cast<long long>(ms));
    if (!ok)
        return 2;

    double value = 0.0;
    lj::ReadNumber(game, field, value);
    printf("%s = %.0f\n", name.c_str(), value);

    if (doSet) {
        if (!game.EnableWrite() || !lj::WriteNumber(game, field, setValue)) {
            printf("값 쓰기에 실패했습니다.\n");
            return 3;
        }
        Sleep(500); // 等游戏跑几帧，确认没被改回去
        lj::ReadNumber(game, field, value);
        printf("%.0f을 기록했습니다. 0.5초 후 읽은 값: %.0f\n", setValue, value);
    }
    return 0;
}
