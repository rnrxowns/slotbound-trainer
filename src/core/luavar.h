#pragma once

#include <array>
#include <future>
#include <optional>
#include <string>

#include "bridge/luaclient.h"
#include "luajit.h"
#include "process.h"

// 一个数字类型的 Lua 全局变量（_G 里的 double），例如：
//   coins      金币。界面数字是派生的：coins → 动画表.target → 插值 → displayCoins → ui.goldBalance，下游改了没用
//   spinsLeft  剩余旋转次数。另有一个同名字段在别的表里（状态快照），不是来源
//
// 地址每次开游戏都不一样，_G 扩容时还会搬家，所以不存地址：
// 连上游戏后在后台线程扫一遍堆定位（约 0.5 秒），之后每帧核对节点的键还对不对，不对就重新定位。
// 同名字段可能很多，用 neighbor（只有 _G 才有的全局变量名，例如 displayCoins）挑出 _G 那个。
class LuaVar {
public:
    enum class State { NotConnected, Locating, Ready, NotFound };

    // mirror：有的数值界面不会自己跟上（水晶的动画目标只在游戏自己加减时才更新），
    // 改值时把这个字段也写成同一个数，界面才会滚到新值。mirrorNeighbor 是它所在表里的另一个键，用来区分同名字段
    LuaVar(GameProcess& game, const char* name, const char* neighbor, const char* mirror = nullptr,
        const char* mirrorNeighbor = nullptr)
        : game_(game), name_(name), neighbor_(neighbor), mirrorName_(mirror), mirrorNeighbor_(mirrorNeighbor)
    {
    }
    ~LuaVar() { Reset(); }
    LuaVar(const LuaVar&) = delete;
    LuaVar& operator=(const LuaVar&) = delete;

    // 每帧调用。lockOn 时把值保持在 lockValue
    void Update(bool lockOn, double lockValue);

    // 一次性改成 value。没定位到 / 写失败返回 false
    bool Set(double value);
    bool Add(double amount);

    // 立刻重新定位（菜单里的「重新定位」）
    void Relocate();

    // 游戏进程没了 / 要断开前调用：等后台定位结束，清掉地址
    void Reset();

    const char* Name() const { return name_; }
    State GetState() const { return state_; }
    bool Ready() const { return state_ == State::Ready; }
    double Value() const { return value_; }
    uintptr_t Node() const { return field_.node; }
    const std::string& Log() const { return log_; }
    bool UsesBridge() const { return useBridge_; }
    const std::string& Message() const { return message_; }

private:
    friend class ResourceBridge;

    struct Change {
        bool relative;
        double value;
    };

    struct LocateResult {
        bool ok = false;
        lj::Field field;
        lj::Field mirror;
        std::string log;
    };

    void StartLocate();
    bool Write(double value);
    void UseBridge(bool useBridge);

    GameProcess& game_;
    const char* name_;
    const char* neighbor_;
    const char* mirrorName_;
    const char* mirrorNeighbor_;
    State state_ = State::NotConnected;
    lj::Field field_;
    lj::Field mirror_; // 没有 / 没找到时 node = 0
    double value_ = 0.0;
    std::string log_;
    std::future<LocateResult> pending_;
    ULONGLONG lastAttempt_ = 0;
    bool discardLocate_ = false;
    bool useBridge_ = false;
    bool refresh_ = false;
    bool mirrorWarning_ = false;
    bool queuedFailure_ = false;
    std::optional<Change> change_;
    std::optional<double> lockValue_;
    std::string message_;
};

// Shares one background Lua request across all three resources.
class ResourceBridge {
public:
    explicit ResourceBridge(std::array<LuaVar*, 3> vars) : vars_(vars) {}
    ~ResourceBridge() { stop_.request_stop(); }

    void Select(DWORD pid, bool injected, bool enabled);
    void Tick();

private:
    void Complete();

    std::array<LuaVar*, 3> vars_;
    DWORD pid_ = 0;
    bool enabled_ = false;
    bool discardReply_ = false;
    ULONGLONG nextPoll_ = 0;
    ULONGLONG retryBridgeAt_ = 0;
    DWORD mismatchedPid_ = 0;
    std::stop_source stop_;
    std::future<bridge::Result> pending_;
    std::array<bool, 3> manual_{};
};
