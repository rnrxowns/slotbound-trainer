// Slotbound 트레이너는 게임 메모리를 읽고 쓰며 투명 오버레이에 UI를 표시합니다.
// 자원은 주입 후 Lua 브리지로 읽고 쓰며, 주입 전에는 기존 메모리 검색을 사용합니다.
//   单位 / 物品 / 老虎机页：用户点「注入」后经 slotbound_lua.dll 在游戏里执行 Lua。
// 这里只有主循环；各页的画法在 src/ui/，和游戏打交道的在 src/game/、src/core/。

#include <windows.h>
#include <shellapi.h>

#include <string>

#include "app/config.h"
#include "app/hotkeys.h"
#include "bridge/injector.h"
#include "core/process.h"
#include "ui/menu.h"
#include "ui/overlay.h"

#define SB_WIDEN2(x) L##x
#define SB_WIDEN(x) SB_WIDEN2(x)

namespace {

constexpr ULONGLONG kStatusBarIntervalMs = 100; // 菜单关着时状态栏的刷新间隔

// 便携版：exe 旁边的 trainer.ini；开发版：仓库里的 config/trainer.ini
std::wstring DefaultConfigPath()
{
#ifdef SB_PORTABLE
    wchar_t exe[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring path(exe, len);
    path.resize(path.find_last_of(L'\\') + 1);
    return path + L"trainer.ini";
#else
    return SB_WIDEN(SB_DEFAULT_CONFIG);
#endif
}

// 第一个命令行参数是配置文件路径（按 Windows 规则拆参数，去掉引号）
std::wstring ConfigPathFromCommandLine()
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring path = argv && argc > 1 ? argv[1] : DefaultConfigPath();
    LocalFree(argv);
    return path;
}

// 设置停止变化 0.5 秒后写盘（拖滑块、连按 +/- 时不会每帧都写）
class AutoSave {
public:
    AutoSave(const std::wstring& path, const Config& cfg) : path_(path), saved_(cfg), lastSeen_(cfg) {}

    // 每帧调用；写了盘就更新 fileFound（写失败 = false，菜单底部会提示）
    void Tick(const Config& cfg, ULONGLONG now, bool& fileFound)
    {
        if (cfg != lastSeen_) {
            lastSeen_ = cfg;
            changedAt_ = now;
        }
        if (cfg != saved_ && now - changedAt_ >= 500) {
            fileFound = SaveConfig(path_, cfg);
            saved_ = cfg;
        }
    }
    void Flush(const Config& cfg)
    {
        if (cfg != saved_)
            SaveConfig(path_, cfg);
    }

private:
    const std::wstring& path_;
    Config saved_;
    Config lastSeen_;
    ULONGLONG changedAt_ = 0;
};

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    const std::wstring cfgPath = ConfigPathFromCommandLine();
    Config cfg;
    bool cfgFileFound = LoadConfig(cfgPath, cfg);

    if (!overlay::Create()) {
        MessageBoxW(nullptr, L"D3D11을 초기화하지 못해 오버레이를 만들 수 없습니다.", L"Slotbound 트레이너", MB_ICONERROR);
        return 1;
    }
    overlay::SetInteractive(false);

    GameProcess game;
    // 可改的 Lua 全局变量。displayCoins 只有 _G 里有；别的表（状态快照）里也有同名字段，靠它挑出 _G 的那个。
    // 加新数值：这里加一行 + Config 里加一个 VarConfig + config.cpp 里 Load/Save 一行
    ui::TrackedVar vars[] = {
        { { game, "coins", "displayCoins" }, &Config::coins, { "골드", { 100, 1000, 10000 }, 1000, 10000, 99999, {} } },
        { { game, "spinsLeft", "displayCoins" }, &Config::spins, { "회전 횟수", { 1, 5, 10 }, 1, 10, 99, {} } },
        // 水晶的界面动画目标 jungsuBalanceTarget 只在游戏自己加减时更新，改值时要一起写，界面才会跟上
        { { game, "jungsu", "displayCoins", "jungsuBalanceTarget", "jungsuBalance" }, &Config::jungsu,
            { "정수", { 1, 5, 10 }, 1, 10, 99, {} } },
    };
    ResourceBridge resources({ &vars[0].var, &vars[1].var, &vars[2].var });
    ui::LuaPages pages;
    bool menuOpen = false;
    KeyEdge menuKey, panicKey, hideKey, exitKey;
    ULONGLONG lastPoll = 0;
    AutoSave autoSave(cfgPath, cfg);
    bool drewLastFrame = true; // 上一帧画了东西：变成什么都不画时要再清一次屏
    bool drewMenu = false;
    ULONGLONG lastDraw = 0;

    while (overlay::PumpMessages()) {
        const ULONGLONG now = GetTickCount64();
        autoSave.Tick(cfg, now, cfgFileFound);

        // 每秒一次：连接/断开游戏进程、跟随游戏窗口
        if (now - lastPoll >= 1000) {
            lastPoll = now;
            if (game.Pid() && !game.IsAlive()) {
                resources.Select(0, false, cfg.enabled);
                for (ui::TrackedVar& t : vars)
                    t.var.Reset(); // 先等后台定位线程停下，再关句柄
                game.Detach();
            }
            if (!game.Pid())
                game.Attach(cfg.processName);
            game.RefreshWindow();
            overlay::FollowWindow(game.Window());
        }

        // 全局快捷键
        if (menuKey.Pressed(cfg.menuKey)) {
            menuOpen = !menuOpen;
            overlay::SetInteractive(menuOpen);
            if (!menuOpen && game.Window())
                overlay::BringToFront(game.Window());
        }
        if (panicKey.Pressed(cfg.panicKey))
            cfg.enabled = !cfg.enabled;
        if (hideKey.Pressed(cfg.hideKey))
            overlay::SetVisible(!overlay::Visible());
        if (exitKey.Pressed(cfg.exitKey))
            PostQuitMessage(0);
        const bool injected = game.Pid() && bridge::Injected();
        resources.Select(game.Pid(), injected, cfg.enabled);
        for (ui::TrackedVar& t : vars) {
            VarConfig& vc = cfg.*t.cfg;
            if (t.lockEdge.Pressed(vc.lockKey))
                vc.lock = !vc.lock;
            if (t.addEdge.Pressed(vc.addKey) && cfg.enabled && t.var.Ready())
                t.var.Add(vc.addAmount);
        }
        if (menuOpen)
            overlay::SetVisible(true); // 菜单开着时一定要看得见

        overlay::KeepAbove(game.Window());

        // 等垂直同步，然后读数据 → 画 → 提交（数据最多旧一帧）
        overlay::WaitForVBlank();
        for (ui::TrackedVar& t : vars) {
            const VarConfig& vc = cfg.*t.cfg;
            t.var.Update(cfg.enabled && vc.lock, vc.lockValue);
        }
        resources.Tick();
        pages.Update(menuOpen, injected, cfg);

        // 覆盖层隐藏、或者菜单和状态栏都关着：不用画。只在刚变成这样的那一帧再画一次（清掉上一帧的内容）。
        // 只有状态栏时每秒画 10 次就够（DWM 会一直显示上一次提交的画面）；菜单开着时每帧画，操作才跟手
        const bool drawSomething = overlay::Visible() && (menuOpen || cfg.overlay);
        if (!drawSomething && !drewLastFrame)
            continue;
        if (drawSomething && drewLastFrame && !menuOpen && !drewMenu && now - lastDraw < kStatusBarIntervalMs)
            continue;
        drewLastFrame = drawSomething;
        drewMenu = menuOpen; // 刚关菜单的那一帧也要马上画，免得菜单残留
        lastDraw = now;

        overlay::BeginFrame();
        if (cfg.overlay)
            ui::DrawStatusBar(cfg, game, vars);
        if (menuOpen) {
            ui::MenuContext ctx{ cfg, cfgPath, cfgFileFound, game, injected, vars, pages };
            if (!ui::DrawMenu(ctx))
                PostQuitMessage(0);
        }
        overlay::EndFrame();
    }

    resources.Select(0, false, false);
    for (ui::TrackedVar& t : vars)
        t.var.Reset();
    autoSave.Flush(cfg);
    overlay::Destroy();
    return 0;
}
