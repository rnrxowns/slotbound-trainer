#include "unitlevel.h"

#include <cstdlib>

#include "luaops.h"

namespace {

using namespace luaops;

// 一行：sym 品级 星级 阵容序号 locked/last 吸收LV 最高星 门槛(逗号) 待选职业 待升星 职业名 生命 攻击 攻速 射程
constexpr const char* kUnitLua = R"lua(
if not u then return 'none' end
local FS = '\31'
local E = require('src.data.constants').evolution or {}
local th = E.rankThresholds and E.rankThresholds[u.grade] or {}
local maxR = E.maxRankByGrade and E.maxRankByGrade[u.grade] or 1
local ths = {}
for r = 2, maxR do ths[#ths+1] = tostring(th[r] or 0) end
local st = u.stats or {}
return table.concat({ tostring(u.sym), tostring(u.grade), tostring(u.rank or 1), tostring(u.index or 0), how,
  tostring(u.absorbLV or 1), tostring(maxR), table.concat(ths, ','), u.needsClassSelect and '1' or '0',
  tostring(u.pendingRank or 0), textField(u.subclassName),
  tostring(st.hp or 0), tostring(st.atk or 0), tostring(st.aspd or 0), tostring(st.distance or 0),
  __sbSkillInfo(u) }, FS)
)lua";

// 选中单位对应的战场单位：战斗中、还在 allies 里才算 → a（可能是 nil）、live
constexpr const char* kLiveAllyLua = R"lua(
local a = u.allyRef
local live = false
if type(a) == 'table' and battlefield.mode == 'battle' then
  for _, x in ipairs(battlefield.allies or {}) do if x == a then live = true break end end
end
)lua";

// 技能信息：主动技能名 / 基础冷却 / 冷却倍率；被动技能只给名字
// 倍率 = 游戏原倍率（只看 22 号词条「技能加速」）× 修改器的 __sbSkillCdMul
// 输出 "active|名字|基础秒|总倍率|游戏原倍率|是否自定义"、"passive|名字|key:原值:现值;..."、"none"
// 被动的可改参数（定义 runtime 里有才列）：charges 触发所需普攻次数、chance 触发概率、internalCooldown 内置冷却
constexpr const char* kSkillInfoLua = R"lua(
local function __sbSkillInfo(u)
  local S = require('src.data.skill')
  local ok, list = pcall(S.getSkillsForUnit, u)
  if not ok or type(list) ~= 'table' or #list == 0 then return 'none' end
  local function nm(d) return textField(localizedText('skill.name.' .. tostring(d.id), d.name) or d.id) end
  for _, d in ipairs(list) do
    local base = tonumber(d.runtime and d.runtime.cooldown) or tonumber(d.cooldown)
    if d.activationType == 'active' and base and base > 0 then
      local H = rawget(_G, '__sbCdHook')
      local f = rawget(_G, '__slotGetSkillCooldownMul')
      local mul = type(f) == 'function' and tonumber(f(u)) or 1
      local nat = type(H) == 'table' and tonumber(H.orig(u)) or mul
      return 'active|' .. nm(d) .. '|' .. tostring(base) .. '|' .. tostring(mul) .. '|' .. tostring(nat) .. '|' ..
        (u.__sbSkillCdMul and '1' or '0')
    end
  end
  local d = list[1]
  local rt = type(d.runtime) == 'table' and d.runtime or {}
  local ov = type(u.__sbSkillRt) == 'table' and u.__sbSkillRt or {}
  local ps = {}
  for _, k in ipairs({ 'charges', 'chance', 'internalCooldown' }) do
    local base = tonumber(rt[k])
    if base then ps[#ps+1] = k .. ':' .. tostring(base) .. ':' .. tostring(tonumber(ov[k]) or base) end
  end
  return 'passive|' .. nm(d) .. '|' .. table.concat(ps, ';')
end
)lua";

} // namespace

std::string UnitLevelEditor::PollCode() const
{
    // 顺带装技能 hook（已装就跳过），读到的冷却倍率才含修改器的自定义
    return std::string(kCdHookLua) + kResolveUnit + kLocalizedTextLua + kSkillInfoLua + kUnitLua;
}

void UnitLevelEditor::OnPoll(const bridge::Result& r)
{
    unit_ = {};
    if (!r.ok || r.text == "none")
        return;
    const auto f = Split(r.text, kFs);
    if (f.size() < 16)
        return;
    unit_.valid = true;
    unit_.sym = f[0];
    unit_.grade = f[1];
    unit_.rank = atoi(f[2].c_str());
    unit_.rosterIndex = atoi(f[3].c_str());
    unit_.locked = f[4] == "locked";
    unit_.absorbLv = atoi(f[5].c_str());
    unit_.maxRank = atoi(f[6].c_str());
    if (!f[7].empty())
        for (const std::string& t : Split(f[7], ','))
            unit_.thresholds.push_back(atoi(t.c_str()));
    unit_.needsClassSelect = f[8] == "1";
    unit_.pendingRank = atoi(f[9].c_str());
    unit_.subclass = f[10];
    unit_.hp = atof(f[11].c_str());
    unit_.atk = atof(f[12].c_str());
    unit_.aspd = atof(f[13].c_str());
    unit_.range = atof(f[14].c_str());

    // 技能："active|名字|基础秒|总倍率|游戏原倍率|是否自定义"、"passive|名字|key:原值:现值;..."、"none"
    const auto s = Split(f[15], '|');
    if (s.size() >= 2)
        unit_.skillName = s[1];
    if (s[0] == "active" && s.size() >= 6) {
        unit_.skill = SkillKind::Active;
        unit_.skillBaseCd = atof(s[2].c_str());
        unit_.skillCdMul = atof(s[3].c_str());
        unit_.skillNaturalMul = atof(s[4].c_str());
        unit_.skillCdCustom = s[5] == "1";
    } else if (s[0] == "passive") {
        unit_.skill = SkillKind::Passive;
        if (s.size() >= 3 && !s[2].empty())
            for (const std::string& item : Split(s[2], ';')) {
                const auto p = Split(item, ':');
                if (p.size() >= 3)
                    unit_.skillParams.push_back({ p[0], atof(p[1].c_str()), atof(p[2].c_str()) });
            }
    }
}

void UnitLevelEditor::SetLevel(int level)
{
    if (Busy() || !unit_.valid || level < 1)
        return;
    std::string code = CheckSameUnit(unit_.sym, unit_.rosterIndex) +
        "local f = rawget(_G, '__slotSetAwakeningLevel')\n"
        "if type(f) ~= 'function' then return '게임에서 흡수 레벨 변경 함수(__slotSetAwakeningLevel)를 찾을 수 없습니다.' end\n"
        "f(u, " + std::to_string(level) + ")\n"
        "return 'ok'\n";
    Act(std::move(code), "흡수 레벨을 변경했습니다. 현재 레벨 " + std::to_string(level));
}

void UnitLevelEditor::SetStat(const char* key, double value, const char* label)
{
    if (Busy() || !unit_.valid)
        return;
    const std::string num = Number(value);
    // 开战时游戏的换算（反汇编 beginBattle）：maxhp = floor(hp + 0.5)、dmg = atk、distance = distance、
    // atkPeriod = 1 / max(0.05, aspd)、atkTicks = max(1, floor(atkPeriod * 60 + 0.5))
    std::string code = CheckSameUnit(unit_.sym, unit_.rosterIndex) +
        "local st = u.stats\n"
        "if type(st) ~= 'table' then return '이 유닛의 능력치 데이터가 없습니다.' end\n"
        "local key, new = " + Quote(key) + ", " + num + "\n"
        "local old = tonumber(st[key]) or 0\n"
        "st[key] = new\n"
        "if key == 'distance' then u.distance = new end\n" +
        kLiveAllyLua +
        "if live and old > 0 and not a.dying then\n"
        "  local r = new / old\n"
        "  if key == 'hp' then\n"
        "    local frac = (a.maxhp and a.maxhp > 0) and (a.hp or a.maxhp) / a.maxhp or 1\n"
        "    a.maxhp = math.max(1, math.floor((a.maxhp or new) * r + 0.5))\n"
        "    a.hp = math.max(1, math.min(a.maxhp, a.maxhp * frac))\n"
        "  elseif key == 'atk' then\n"
        "    a.dmg = (a.dmg or new) * r\n"
        "  elseif key == 'aspd' then\n"
        "    a.atkPeriod = (a.atkPeriod or 1 / new) / r\n"
        "    a.atkTicks = math.max(1, math.floor(a.atkPeriod * 60 + 0.5))\n"
        "  elseif key == 'distance' then\n"
        "    a.distance = (a.distance or new) * r\n"
        "  end\n"
        "end\n"
        "return 'ok'\n";
    Act(std::move(code), std::string(label) + " 변경 완료 (" + num + "). 전투 중에도 즉시 적용됩니다.");
}

void UnitLevelEditor::SetSkillCooldown(double seconds)
{
    if (Busy() || !unit_.valid || unit_.skill != SkillKind::Active || unit_.skillBaseCd <= 0)
        return;
    const bool reset = seconds < 0; // 负数 = 恢复原版
    if (!reset && seconds < kMinCdSeconds)
        seconds = kMinCdSeconds;
    const std::string num = Number(seconds);
    // 游戏原倍率（技能加速词条，词条数值被游戏规整到 8~16% 几档，没法任意定）× 我们的 __sbSkillCdMul（见 luaops kCdHookLua）。
    // 按「想要的秒数 ÷ (基础 × 游戏原倍率)」算出 __sbSkillCdMul 存在单位上 → 跟存档走，一直有效。
    std::string code = std::string(kCdHookLua) + CheckSameUnit(unit_.sym, unit_.rosterIndex) +
        "local H = rawget(_G, '__sbCdHook')\n"
        "if type(H) ~= 'table' then return '게임에서 재사용 대기시간 계산 함수(__slotGetSkillCooldownMul)를 찾을 수 없습니다.' end\n" +
        (reset ? std::string("u.__sbSkillCdMul = nil\n")
               : "local nat = tonumber(H.orig(u)) or 1\n"
                 "local m = " + num + " / (" + Number(unit_.skillBaseCd) + " * nat)\n"
                 "if math.abs(m - 1) < 0.0005 then u.__sbSkillCdMul = nil else u.__sbSkillCdMul = m end\n") +
        // 战斗中：让游戏按新倍率重算战场单位的 skill.cd，剩余冷却不超过新冷却
        kLiveAllyLua +
        "if live and type(a.skill) == 'table' then\n"
        "  local apply = rawget(_G, '__slotApplySkillCooldownOption')\n"
        "  if type(apply) == 'function' then apply(a) end\n"
        "  local cd = tonumber(a.skill.cd)\n"
        "  if cd and tonumber(a.skill.cdT) and a.skill.cdT > cd then a.skill.cdT = cd end\n"
        "end\n"
        "return 'ok'\n";
    Act(std::move(code), reset ? std::string("스킬 재사용 대기시간을 기본값으로 복원했습니다.")
                               : "스킬 재사용 대기시간 변경 완료 (" + num + "초). 트레이너가 실행 중이면 저장 후에도 유지됩니다.");
}

void UnitLevelEditor::SetSkillParam(const std::string& key, double value, bool reset, const std::string& label)
{
    if (Busy() || !unit_.valid || unit_.skill != SkillKind::Passive)
        return;
    const std::string num = Number(value);
    // 整张换新覆盖表：awakening.runtime 包装按覆盖表身份缓存副本，换表 = 缓存失效（战斗中也马上用新值）
    std::string code = std::string(kCdHookLua) + CheckSameUnit(unit_.sym, unit_.rosterIndex) +
        "local ov = {}\n"
        "if type(u.__sbSkillRt) == 'table' then for k, v in pairs(u.__sbSkillRt) do ov[k] = v end end\n"
        "ov[" + Quote(key) + "] = " + (reset ? std::string("nil") : num) + "\n"
        "u.__sbSkillRt = next(ov) and ov or nil\n"
        "return 'ok'\n";
    Act(std::move(code), reset ? label + " 기본값 복원 완료"
                               : label + " 변경 완료 (" + num + "). 트레이너가 실행 중이면 저장 후에도 유지됩니다.");
}

void UnitLevelEditor::RaiseRank(int rank)
{
    if (Busy() || !unit_.valid || rank <= unit_.rank)
        return;
    std::string code = CheckSameUnit(unit_.sym, unit_.rosterIndex) +
        "local f = rawget(_G, '__slotStoneRaiseRank')\n"
        "if type(f) ~= 'function' then return '게임에서 승급 함수(__slotStoneRaiseRank)를 찾을 수 없습니다.' end\n"
        "f(u, " + std::to_string(rank) + ")\n"
        "return 'ok'\n";
    Act(std::move(code), std::to_string(rank) + "성 승급을 요청했습니다. 게임에서 직업을 선택하면 적용됩니다.");
}
