#include "lua_editor.h"

#include <windows.h>

#include "luaops.h"

namespace {

constexpr ULONGLONG kPollMs = 500;          // 选中单位 / 背包的刷新间隔
constexpr ULONGLONG kCatalogRetryMs = 2000; // 目录读失败后多久再试（之前是每帧重发）

} // namespace

std::string LuaEditor::ActionError(const bridge::Result& r) const
{
    return luaops::ActionError(r);
}

void LuaEditor::Act(std::string code, std::string message)
{
    if (action_.valid())
        return;
    message_ = std::move(message);
    action_ = luaops::Run(std::move(code));
}

void LuaEditor::Update(bool visible)
{
    using luaops::Ready;
    if (Ready(catalogJob_)) {
        const bridge::Result r = catalogJob_.get();
        if (r.ok) {
            OnCatalog(r.text);
            catalogLoaded_ = true;
            catalogError_.clear();
        } else {
            catalogError_ = std::string(CatalogName()) + " 불러오기 실패: " + r.text;
        }
    }
    if (Ready(pollJob_)) {
        const bridge::Result r = pollJob_.get();
        if (r.ok)
            error_.clear();
        else
            error_ = r.text;
        OnPoll(r);
    }
    if (Ready(action_)) {
        const std::string err = ActionError(action_.get());
        if (!err.empty())
            message_ = err;
        lastPoll_ = 0; // 马上刷新显示
    }

    if (!visible)
        return;
    const ULONGLONG now = GetTickCount64();
    if (const char* code = CatalogCode(); code && !catalogLoaded_ && !catalogJob_.valid() &&
        now - lastCatalogTry_ >= kCatalogRetryMs) {
        lastCatalogTry_ = now;
        catalogJob_ = luaops::Run(code);
    }
    if (!pollJob_.valid() && !action_.valid() && now - lastPoll_ >= kPollMs) {
        lastPoll_ = now;
        pollJob_ = luaops::Run(PollCode());
    }
}
