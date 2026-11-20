#pragma once

// 经 Lua 桥在游戏里执行代码的公共小工具（各编辑器共用）

#include <chrono>
#include <cstdio>
#include <cstring>
#include <future>
#include <string>
#include <utility>
#include <vector>

#include "bridge/luaclient.h"

namespace luaops {

// 回传文本：字段分隔 \x1f，记录分隔 \n
inline constexpr char kFs = '\x1f';

inline std::vector<std::string> Split(const std::string& s, char sep)
{
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        const size_t pos = s.find(sep, start);
        out.push_back(s.substr(start, pos == std::string::npos ? std::string::npos : pos - start));
        if (pos == std::string::npos)
            break;
        start = pos + 1;
    }
    return out;
}

// Lua 字符串字面量
inline std::string Quote(const std::string& s)
{
    std::string out = "\"";
    for (char c : s) {
        if (c == '\\' || c == '"')
            out += '\\';
        if (c == '\n') {
            out += "\\n";
            continue;
        }
        out += c;
    }
    return out + "\"";
}

// Lua 数字字面量（也用在提示文字里，%.6g 够看）
inline std::string Number(double v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    return buf;
}

// 效果文字里的 {atk.png} 之类是游戏内图标，换成文字。解析目录时做一次，界面每帧直接用
inline std::string IconText(std::string s)
{
    static const std::pair<const char*, const char*> kIcons[] = {
        { "{atk.png}", "공격력" }, { "{hp.png}", "체력" }, { "{aspd.png}", "공격 속도" }, { "{dist.png}", "사거리" },
        { "{gold.png}", "골드" }, { "{essence.png}", "정수" },
    };
    if (s.find('{') == std::string::npos)
        return s;
    for (const auto& [from, to] : kIcons)
        for (size_t pos; (pos = s.find(from)) != std::string::npos;)
            s.replace(pos, std::strlen(from), to);
    return s;
}

// 各级效果（| 分隔）→ 图标换好的列表
inline std::vector<std::string> LevelTexts(const std::string& joined)
{
    std::vector<std::string> out = Split(joined, '|');
    for (std::string& s : out)
        s = IconText(std::move(s));
    return out;
}

inline bool Ready(std::future<bridge::Result>& f)
{
    return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

inline std::future<bridge::Result> Run(std::string code)
{
    return std::async(std::launch::async, [code = std::move(code)] { return bridge::Execute(code, 3000); });
}

// Use the game's Korean text without changing its selected language. The upstream
// game notes identify getText as a source of Korean text and definitions as a fallback.
inline constexpr const char* kLocalizedTextLua = R"lua(
local language
local function localizedText(key, original)
  language = language or require('src.data.language')
  local korean = type(language.ko) == 'table' and language.ko[key]
  if type(korean) == 'string' and korean ~= '' and korean ~= key then return korean end
  if type(language.getText) == 'function' then
    local ok, text = pcall(language.getText, key)
    if ok and type(text) == 'string' and text ~= '' and text ~= key then return text end
  end
  if original and original ~= '' then return original end
  local current = language[rawget(_G, 'currentGameLang') or 'zh']
  local chinese = language.zh
  return (type(current) == 'table' and current[key]) or (type(chinese) == 'table' and chinese[key]) or nil
end
local function textField(text)
  return (tostring(text or ''):gsub('[\r\n|\31]', ' '))
end
)lua";

// 公共前缀：取当前点选的己方单位 → 局部变量 u（没有时 nil），how = 'locked' / 'last'
inline constexpr const char* kResolveUnit = R"lua(
local RO = require('src.data.randomoption')
local t = rawget(_G, 'tooltip')
local tg = t and t.locked and (t.lockedTarget or t.target) or nil
local u = (tg and tg.kind == 'roster') and tg.ref or nil
local how = 'locked'
-- 面板关掉后 lockedTarget 就清了，退回到「最近选中」的单位（还在阵容里才算）
if not u and t and t.lastSelectedRef and rawget(_G, 'battlefield') then
  for _, r in ipairs(battlefield.rosterUnits or {}) do
    if r == t.lastSelectedRef then u = r; how = 'last'; break end
  end
end
)lua";

// 技能 hook（两个，都是「已包过就跳过」，修改器不开时没人包 = 原版）。设置都存在阵容单位表上，跟存档一起保存
// （run_save.copySerializable 不过滤字段）。传进来的可能是战场单位：靠阵容单位的 allyRef 反查（__sbUnitField）。
//
// 1) 主动技能冷却：包全局 __slotGetSkillCooldownMul（算 skill.cd、面板显示冷却都每次现查 _G），
//    在游戏原倍率上再乘单位的 __sbSkillCdMul。__sbCdHook.orig = 游戏原函数（算「不改时」的倍率）。
// 2) 技能参数（被动技能的触发条件）：包 awakening.runtime（各技能模块每次都 require 再调它取参数），
//    单位有覆盖表 __sbSkillRt = { charges = 1, ... } 时返回「原参数 + 覆盖」的副本；原表是同技能单位共用的，不能改。
//    副本按 (单位, 原表, 覆盖表) 缓存，改设置时整张换新覆盖表让缓存失效。
inline constexpr const char* kCdHookLua = R"lua(
do
  local function unitField(x, key)
    if type(x) ~= 'table' then return nil end
    local v = rawget(x, key)
    if v ~= nil then return v end
    local bf = rawget(_G, 'battlefield')
    for _, r in ipairs(bf and bf.rosterUnits or {}) do
      if r.allyRef == x then return r[key] end
    end
    return nil
  end
  rawset(_G, '__sbUnitField', unitField)

  local H = rawget(_G, '__sbCdHook')
  local cur = rawget(_G, '__slotGetSkillCooldownMul')
  if type(cur) == 'function' and not (type(H) == 'table' and H.wrapper == cur) then
    local orig = cur
    H = { orig = orig }
    function H.wrapper(x, ...)
      local m = orig(x, ...)
      local k = tonumber(unitField(x, '__sbSkillCdMul'))
      if k and type(m) == 'number' then m = m * k end
      return m
    end
    rawset(_G, '__sbCdHook', H)
    rawset(_G, '__slotGetSkillCooldownMul', H.wrapper)
  end

  local A = package.loaded['src.game.awakening']
  local R = rawget(_G, '__sbRtHook')
  if type(A) == 'table' and type(A.runtime) == 'function' and not (type(R) == 'table' and R.wrapper == A.runtime) then
    local orig = A.runtime
    R = { orig = orig, cache = setmetatable({}, { __mode = 'k' }) }
    function R.wrapper(x, ...)
      local r = orig(x, ...)
      local ov = unitField(x, '__sbSkillRt')
      if type(ov) ~= 'table' or type(r) ~= 'table' or next(ov) == nil then return r end
      local c = R.cache[x]
      if c and c.base == r and c.ov == ov then return c.value end
      local v = {}
      for k, val in pairs(r) do v[k] = val end
      for k, val in pairs(ov) do v[k] = val end
      R.cache[x] = { base = r, ov = ov, value = v }
      return v
    end
    rawset(_G, '__sbRtHook', R)
    A.runtime = R.wrapper
  end
end
)lua";

// 核对选中的还是界面上显示的那个单位（防止点按钮前游戏里换了选中）
inline std::string CheckSameUnit(const std::string& sym, int rosterIndex)
{
    return std::string(kResolveUnit) + "if not u then return 'none' end\n" +
        "if tostring(u.sym) ~= " + Quote(sym) + " or (u.index or 0) ~= " + std::to_string(rosterIndex) +
        " then return 'changed' end\n";
}

// 改动类请求的回复 → 界面提示；成功返回空串
inline std::string ActionError(const bridge::Result& r)
{
    if (!r.ok)
        return "실패: " + r.text;
    if (r.text == "none")
        return "실패: 선택한 유닛이 없습니다.";
    if (r.text == "changed")
        return "실패: 선택한 유닛이 바뀌었습니다. 대상을 확인한 뒤 다시 시도해 주세요.";
    if (r.text != "ok")
        return "실패: " + r.text;
    return {};
}

} // namespace luaops
