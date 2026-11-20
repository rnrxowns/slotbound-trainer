#include "luavar.h"

#include <charconv>
#include <chrono>
#include <cmath>
#include <string_view>

#include "resource_lua.h"

namespace {

// 没找到时（主菜单 / 加载中，变量还没创建）多久再试一次
constexpr ULONGLONG kRetryMs = 5000;
constexpr ULONGLONG kBridgePollMs = 150;

void AppendChange(std::string& code, const char* operation, double value)
{
    char number[32];
    const auto result = std::to_chars(number, number + sizeof(number), value);
    code += "{\"";
    code += operation;
    code += "\",";
    code.append(number, result.ptr);
    code += "},";
}

std::optional<double> ParseValue(std::string_view text)
{
    double value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec == std::errc{} && result.ptr == text.data() + text.size() && std::isfinite(value))
        return value;
    return std::nullopt;
}

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
    if (useBridge_) {
        lockValue_ = lockOn ? std::optional<double>(lockValue) : std::nullopt;
        return;
    }
    if (!game_.Pid()) {
        state_ = State::NotConnected;
        return;
    }

    // 后台定位结束了就收结果
    if (pending_.valid()) {
        if (pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        LocateResult r = pending_.get();
        if (discardLocate_) {
            discardLocate_ = false;
            StartLocate();
            return;
        }
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
    if (!Ready() || !std::isfinite(value))
        return false;
    if (!useBridge_)
        return Write(value);
    change_ = Change{ false, value };
    message_ = "변경 결과를 기다리는 중…";
    return true;
}

bool LuaVar::Add(double amount)
{
    if (!Ready() || !std::isfinite(amount))
        return false;
    if (!useBridge_)
        return Set(value_ + amount);
    if (change_)
        change_->value += amount;
    else
        change_ = Change{ true, amount };
    message_ = "변경 결과를 기다리는 중…";
    return true;
}

void LuaVar::Relocate()
{
    if (useBridge_) {
        refresh_ = true;
        return;
    }
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
    discardLocate_ = false;
    UseBridge(false);
    field_ = {};
    mirror_ = {};
    value_ = 0.0;
    state_ = State::NotConnected;
}

void LuaVar::UseBridge(bool useBridge)
{
    if (useBridge_ == useBridge)
        return;
    useBridge_ = useBridge;
    if (useBridge)
        discardLocate_ = pending_.valid();
    state_ = useBridge ? State::Locating : State::NotConnected;
    field_ = {};
    mirror_ = {};
    value_ = 0;
    refresh_ = false;
    mirrorWarning_ = false;
    queuedFailure_ = false;
    change_.reset();
    lockValue_.reset();
    log_.clear();
    message_.clear();
}

void ResourceBridge::Select(DWORD pid, bool injected, bool enabled)
{
    const bool retryAllowed = pid != mismatchedPid_ || GetTickCount64() >= retryBridgeAt_;
    const DWORD bridgePid = injected && retryAllowed ? pid : 0;
    if (bridgePid != pid_) {
        pid_ = bridgePid;
        discardReply_ = pending_.valid();
        stop_.request_stop();
        nextPoll_ = 0;
        for (LuaVar* var : vars_) {
            var->UseBridge(false);
            var->UseBridge(pid_ != 0);
        }
    }
    if (enabled_ && !enabled) {
        discardReply_ = pending_.valid();
        stop_.request_stop();
        nextPoll_ = 0;
        for (LuaVar* var : vars_) {
            var->change_.reset();
            var->message_.clear();
            var->queuedFailure_ = false;
        }
    }
    enabled_ = enabled;
}

void ResourceBridge::Tick()
{
    if (pending_.valid()) {
        if (pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        Complete();
    }
    if (!pid_)
        return;

    const ULONGLONG now = GetTickCount64();
    bool requested = false;
    for (const LuaVar* var : vars_)
        requested |= var->refresh_ || (enabled_ && var->change_.has_value());
    if (!requested && now < nextPoll_)
        return;

    std::string code = "local changes = {";
    for (size_t i = 0; i < vars_.size(); ++i) {
        LuaVar& var = *vars_[i];
        var.refresh_ = false;
        manual_[i] = enabled_ && var.change_.has_value();
        if (manual_[i]) {
            AppendChange(code, var.change_->relative ? "add" : "set", var.change_->value);
            var.change_.reset();
        } else if (enabled_ && var.lockValue_) {
            AppendChange(code, "lock", *var.lockValue_);
        } else {
            AppendChange(code, "read", 0);
        }
    }
    code += "}\n";
    code += kResourceLua;
    stop_ = std::stop_source{};
    discardReply_ = false;
    pending_ = std::async(std::launch::async, [code = std::move(code), pid = pid_, stop = stop_.get_token()] {
        return bridge::Execute(code, 1000, pid, stop);
    });
    nextPoll_ = now + kBridgePollMs;
}

void ResourceBridge::Complete()
{
    const bridge::Result result = pending_.get();
    if (discardReply_)
        return;
    if (result.wrongProcess) {
        mismatchedPid_ = pid_;
        retryBridgeAt_ = GetTickCount64() + kRetryMs;
        Select(pid_, false, enabled_);
        return;
    }
    if (!result.ok)
        nextPoll_ = GetTickCount64() + 1000;

    std::array<std::string_view, 6> fields;
    std::string_view remaining = result.text;
    bool valid = result.ok;
    for (size_t i = 0; valid && i < fields.size(); ++i) {
        const size_t tab = remaining.find('\t');
        valid = (tab == std::string_view::npos) == (i + 1 == fields.size());
        fields[i] = remaining.substr(0, tab);
        if (tab != std::string_view::npos)
            remaining.remove_prefix(tab + 1);
    }

    for (size_t i = 0; i < vars_.size(); ++i) {
        LuaVar& var = *vars_[i];
        const auto value = valid ? ParseValue(fields[2 * i]) : std::nullopt;
        const auto status = valid ? fields[2 * i + 1] : std::string_view{};
        var.state_ = value ? LuaVar::State::Ready : LuaVar::State::NotFound;
        if (value)
            var.value_ = *value;
        if (status == "changed" || status == "mirror")
            var.mirrorWarning_ = status == "mirror";
        if (!valid) {
            var.log_ = result.ok ? "Lua 브리지의 응답 형식이 올바르지 않습니다." : result.text;
        } else if (!value) {
            var.log_ = "게임의 _G에 숫자 값이 없습니다.";
        } else if (var.mirrorWarning_) {
            var.log_ = "정수는 변경됐지만 게임 화면의 애니메이션 값을 확인하지 못했습니다.";
        } else {
            var.log_ = "Lua 브리지로 _G 값을 읽었습니다.";
        }
        if (manual_[i]) {
            if (!valid || !value || (status != "changed" && status != "mirror")) {
                var.message_ = "값 변경을 확인하지 못했습니다. " + var.log_;
                var.queuedFailure_ = var.change_.has_value();
            } else if (!var.change_) {
                var.message_ = var.queuedFailure_ ? "이전 요청의 결과는 확인하지 못했습니다. " : "";
                var.message_ += status == "mirror" ? var.log_ : "값을 변경하고 다시 읽었습니다.";
                var.queuedFailure_ = false;
            }
        }
    }
}
