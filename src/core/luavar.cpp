#include "luavar.h"

#include <chrono>

namespace {

// 没找到时（主菜单 / 加载中，变量还没创建）多久再试一次
constexpr ULONGLONG kRetryMs = 5000;

} // namespace

void LuaVar::StartLocate()
{
    lastAttempt_ = GetTickCount64();
    state_ = State::Locating;
    const GameProcess* game = &game_;
    pending_ = std::async(std::launch::async,
        [game, name = name_, neighbor = neighbor_, mirror = mirrorName_, mirrorNeighbor = mirrorNeighbor_] {
            LocateResult r;
            r.ok = lj::LocateGlobalNumber(*game, name, neighbor, r.field, r.log);
            // 显示字段找不到不影响改值本身，只是界面要等游戏下次加减才刷新
            if (r.ok && mirror) {
                r.log += "--- 표시값 동기화 필드 ---\n";
                if (!lj::LocateGlobalNumber(*game, mirror, mirrorNeighbor ? mirrorNeighbor : "", r.mirror, r.log))
                    r.mirror = {};
            }
            return r;
        });
}

bool LuaVar::Write(double value)
{
    if (!game_.EnableWrite() || !lj::WriteNumber(game_, field_, value))
        return false;
    if (mirror_.Valid() && !lj::WriteNumber(game_, mirror_, value))
        mirror_ = {}; // 显示字段搬家了：下次重新定位时再找
    value_ = value;
    return true;
}

void LuaVar::Update(bool lockOn, double lockValue)
{
    if (!game_.Pid()) {
        state_ = State::NotConnected;
        return;
    }

    // 后台定位结束了就收结果
    if (pending_.valid()) {
        if (pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        LocateResult r = pending_.get();
        log_ = std::move(r.log);
        field_ = r.ok ? r.field : lj::Field{};
        mirror_ = r.ok ? r.mirror : lj::Field{};
        state_ = r.ok ? State::Ready : State::NotFound;
    }

    if (state_ == State::Ready && !lj::ReadNumber(game_, field_, value_)) {
        // 节点被搬走了（_G 扩容）或者读不到了：重新定位
        log_ += "노드가 무효화되어 다시 찾는 중\n";
        field_ = {};
        StartLocate();
        return;
    }
    if (state_ != State::Ready) {
        if (state_ == State::NotConnected || GetTickCount64() - lastAttempt_ >= kRetryMs)
            StartLocate();
        return;
    }

    if (lockOn && value_ != lockValue)
        Write(lockValue);
}

bool LuaVar::Set(double value)
{
    return state_ == State::Ready && Write(value);
}

void LuaVar::Relocate()
{
    if (!game_.Pid() || pending_.valid())
        return;
    field_ = {};
    mirror_ = {};
    StartLocate();
}

void LuaVar::Reset()
{
    if (pending_.valid())
        pending_.wait(); // 后台线程还在用进程句柄，先等它结束
    pending_ = {};
    field_ = {};
    mirror_ = {};
    value_ = 0.0;
    state_ = State::NotConnected;
}
