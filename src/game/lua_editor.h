#pragma once

// 经 Lua 桥工作的编辑器的公共骨架：词条 / 刻印 / 觉醒 / 等级 / 物品都是同一个套路——
//   目录（可选）：第一次可见时读一次，失败隔 2 秒再试
//   轮询：可见时每 0.5 秒读一次当前状态（选中单位 / 背包）
//   改动：一次只跑一个，跑完马上轮询一次刷新界面
// 子类只管 Lua 代码和解析。

#include <future>
#include <string>
#include <unordered_map>
#include <vector>

#include "bridge/luaclient.h"

class LuaEditor {
public:
    virtual ~LuaEditor() = default;
    LuaEditor(const LuaEditor&) = delete;
    LuaEditor& operator=(const LuaEditor&) = delete;

    // 每帧调用：收后台结果；visible 时按节奏发请求
    void Update(bool visible);

    // 下次可见时重读目录
    void ReloadCatalog() { catalogLoaded_ = false; }

    bool Busy() const { return action_.valid(); }
    const std::string& Message() const { return message_; }
    // 目录出错优先显示（目录读不到时别的也没法用）
    const std::string& Error() const { return catalogError_.empty() ? error_ : catalogError_; }

protected:
    LuaEditor() = default;

    // 轮询的 Lua 代码；回复交给 OnPoll（成功、失败都给，失败时 Error() 已经是原因）
    virtual std::string PollCode() const = 0;
    virtual void OnPoll(const bridge::Result& r) = 0;

    // 目录：没有目录的编辑器不用管。CatalogCode 返回 nullptr = 没有目录
    virtual const char* CatalogCode() const { return nullptr; }
    virtual const char* CatalogName() const { return "목록"; }
    virtual void OnCatalog(const std::string& text) { (void)text; }

    // 改动的回复 → 失败提示；空串 = 成功
    virtual std::string ActionError(const bridge::Result& r) const;

    // 发一个改动；message 是成功时显示的提示（失败会被替换成原因）
    void Act(std::string code, std::string message);

private:
    std::future<bridge::Result> catalogJob_;
    std::future<bridge::Result> pollJob_;
    std::future<bridge::Result> action_;
    bool catalogLoaded_ = false;
    unsigned long long lastCatalogTry_ = 0;
    unsigned long long lastPoll_ = 0;
    std::string message_;
    std::string error_;
    std::string catalogError_;
};

// 按 id 查找的定义表（目录、单位的候选觉醒）。Def 要有 std::string id
template <class Def>
class DefTable {
public:
    void Assign(std::vector<Def> defs)
    {
        defs_ = std::move(defs);
        index_.clear();
        for (size_t i = 0; i < defs_.size(); ++i)
            index_.emplace(defs_[i].id, i);
    }
    void Clear() { Assign({}); }

    const std::vector<Def>& All() const { return defs_; }
    bool Empty() const { return defs_.empty(); }
    const Def* Find(const std::string& id) const
    {
        const auto it = index_.find(id);
        return it == index_.end() ? nullptr : &defs_[it->second];
    }

private:
    std::vector<Def> defs_;
    std::unordered_map<std::string, size_t> index_;
};
