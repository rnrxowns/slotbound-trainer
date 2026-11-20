#include "options.h"

#include <cstdlib>

#include "luaops.h"

namespace {

using namespace luaops;

// 词条目录：id  名字  稀有度  来源  效果  已解锁  备注  档位(value:chance|...)
constexpr const char* kCatalogLua = R"lua(
local RO = require('src.data.randomoption')
local FS = '\31'
local out = {}
for _, d in ipairs(RO.catalog) do
  local id = tostring(d.id)
  local notes = {}
  local src = ({ Spawn = '생성 시', Promotion = '승급 시', Both = '생성 및 승급 시' })[tostring(d.source)]
  if src then notes[#notes+1] = src end
  if d.requiresBasicAttack then notes[#notes+1] = '기본 공격 필요' end
  if d.requiresRangedBasicAttack then notes[#notes+1] = '원거리 기본 공격 필요' end
  if d.requiresActiveSkill then notes[#notes+1] = '액티브 스킬 필요' end
  if d.requiresAttackSkill then notes[#notes+1] = '공격 스킬 필요' end
  if d.requiresBountySource then notes[#notes+1] = '현상금 획득 가능 유닛 전용' end
  if d.requiresNudge then notes[#notes+1] = '이번 판에서 미세 조정 기능 필요' end
  if d.requiresDeploymentRoom then notes[#notes+1] = '빈 유닛 배치 슬롯 필요' end
  if d.incompatibleWith then notes[#notes+1] = '동시 적용 불가 (특성 ID ' .. tostring(d.incompatibleWith) .. ')' end
  if type(d.permittedGrades) == 'table' then
    local g = {}
    for k, v in pairs(d.permittedGrades) do g[#g+1] = type(k) == 'number' and tostring(v) or tostring(k) end
    table.sort(g)
    local grades = { normal = '일반', advanced = '고급', rare = '희귀', special = '특수', legendary = '전설', prism = '프리즘' }
    for i, x in ipairs(g) do g[i] = grades[x] or x end
    notes[#notes+1] = table.concat(g, ' / ') .. ' 등급 유닛 전용'
  end
  local vars = {}
  for _, v in ipairs(d.variants or {}) do
    if v.value ~= nil then vars[#vars+1] = tostring(v.value) .. ':' .. tostring(v.chance or 0) end
  end
  -- 要传定义表：传字符串 id 时游戏函数查不到 initiallyUnlocked，永远返回 true
  local ok, unlocked = pcall(RO.isUnlocked, d)
  out[#out+1] = table.concat({ id,
    textField(localizedText('random_option.name.' .. id, d.name) or id),
    tostring(d.rarity), tostring(d.source),
    textField(localizedText('random_option.effect.' .. id, d.effect)),
    (ok and unlocked == false) and '0' or '1',
    table.concat(notes, ', '), table.concat(vars, '|') }, FS)
end
return table.concat(out, '\n')
)lua";

// 选中单位：第一行 sym  品级  等级  阵容序号  locked/last
//   S 行：槽位  id  档位值
//   E 行：词条 id  不适用原因（空 = 适用）  这个单位已有几个
// 适用条件照抄游戏抽词条（RO.rollForSpawn / 晋升时）用的检查：品级、普攻、远程普攻、主动技能、攻击技能、赏金、微调、互斥、解锁
constexpr const char* kUnitLua = R"lua(
if not u then return 'none' end
local FS = '\31'
local Skill = require('src.data.skill')
local function try(f, ...) if type(f) ~= 'function' then return nil end local ok, r = pcall(f, ...) if ok then return r end return nil end
local out = { table.concat({ tostring(u.sym), tostring(u.grade), tostring(u.rank or 0), tostring(u.index or 0), how }, FS) }
local have = {}
for i, o in ipairs(u.randomOptions or {}) do
  out[#out+1] = table.concat({ 'S', tostring(i), tostring(o.id), o.variant and tostring(o.variant.value) or '' }, FS)
  have[tostring(o.id)] = (have[tostring(o.id)] or 0) + 1
end
local caps = {
  basic = try(RO.canBasicAttack, u), ranged = try(RO.canRangedBasicAttack, u), bounty = try(RO.canEarnBounty, u),
  active = try(rawget(_G, '__slotChoiceHasActiveSkill'), u), attack = try(Skill.hasAttackSkillForUnit, u),
  nudge = try(RO.isNudgeEnabledForRun),
}
for _, d in ipairs(RO.catalog) do
  local id = tostring(d.id)
  local why = {}
  if try(RO.isUnlocked, d) == false then why[#why+1] = '업그레이드 트리에서 미해금' end
  if type(d.permittedGrades) == 'table' then
    local ok = false
    for k, v in pairs(d.permittedGrades) do if (type(k) == 'number' and v or k) == u.grade then ok = true end end
    if not ok then why[#why+1] = '적용 대상 등급이 아님' end
  end
  if d.requiresBasicAttack and caps.basic == false then why[#why+1] = '기본 공격 없음' end
  if d.requiresRangedBasicAttack and caps.ranged == false then why[#why+1] = '원거리 기본 공격 없음' end
  if d.requiresActiveSkill and caps.active == false then why[#why+1] = '액티브 스킬 없음' end
  if d.requiresAttackSkill and caps.attack == false then why[#why+1] = '공격 스킬 없음' end
  if d.requiresBountySource and caps.bounty == false then why[#why+1] = '현상금 획득 불가' end
  if d.requiresNudge and caps.nudge == false then why[#why+1] = '이번 판에서 미세 조정이 꺼짐' end
  if d.incompatibleWith and have[tostring(d.incompatibleWith)] then why[#why+1] = '이미 적용된 특성과 충돌 (ID ' .. tostring(d.incompatibleWith) .. ')' end
  out[#out+1] = table.concat({ 'E', id, table.concat(why, ', '), tostring(have[id] or 0) }, FS)
end
return table.concat(out, '\n')
)lua";

} // namespace

const char* OptionEditor::CatalogCode() const
{
    static const std::string code = std::string(kLocalizedTextLua) + kCatalogLua;
    return code.c_str();
}

void OptionEditor::OnCatalog(const std::string& text)
{
    std::vector<Def> defs;
    for (const std::string& line : Split(text, '\n')) {
        const auto f = Split(line, kFs);
        if (f.size() < 8)
            continue;
        Def d{ f[0], f[1], f[2], f[3], IconText(f[4]), f[6], f[5] != "0", {} };
        if (!f[7].empty())
            for (const std::string& v : Split(f[7], '|')) {
                const size_t colon = v.rfind(':');
                d.variants.push_back({ v.substr(0, colon), colon == std::string::npos ? 0 : atof(v.c_str() + colon + 1) });
            }
        defs.push_back(std::move(d));
    }
    catalog_.Assign(std::move(defs));
}

std::string OptionEditor::PollCode() const
{
    return std::string(kResolveUnit) + kUnitLua;
}

void OptionEditor::OnPoll(const bridge::Result& r)
{
    unit_ = {};
    if (!r.ok || r.text == "none")
        return;
    const auto lines = Split(r.text, '\n');
    const auto head = Split(lines[0], kFs);
    if (head.size() < 4)
        return;
    unit_.valid = true;
    unit_.sym = head[0];
    unit_.grade = head[1];
    unit_.rank = atoi(head[2].c_str());
    unit_.rosterIndex = atoi(head[3].c_str());
    unit_.locked = head.size() < 5 || head[4] == "locked";
    for (size_t i = 1; i < lines.size(); ++i) {
        const auto f = Split(lines[i], kFs);
        if (f.size() >= 4 && f[0] == "S")
            unit_.slots.push_back({ atoi(f[1].c_str()), f[2], f[3] });
        else if (f.size() >= 4 && f[0] == "E")
            unit_.fit[f[1]] = { f[2], atoi(f[3].c_str()) };
    }
}

void OptionEditor::SetOption(int slot, const std::string& id, const std::string& variant)
{
    if (Busy() || !unit_.valid)
        return;
    // 先核对选中的还是界面上显示的那个单位，再生成词条实例放进槽位
    std::string code = CheckSameUnit(unit_.sym, unit_.rosterIndex) +
        "local o = RO.rollForId(" + Quote(id) + ")\n" +
        // rollForId 对升级树里没解锁的词条返回 nil → 照它的格式用目录定义自己拼一个实例
        "if not o then\n"
        "  local d\n"
        "  for _, x in ipairs(RO.catalog) do if tostring(x.id) == " + Quote(id) + " then d = x end end\n"
        "  if not d then return '해당 ID의 특성을 찾을 수 없습니다.' end\n"
        "  o = { id = d.id, name = d.name, effect = d.effect, icon = d.icon, rarity = d.rarity, source = d.source }\n"
        "  local vs, total = d.variants or {}, 0\n"
        "  for _, v in ipairs(vs) do total = total + (tonumber(v.chance or v.weight) or 0) end\n"
        "  local r, pick = math.random() * total, vs[#vs]\n"
        "  for _, v in ipairs(vs) do\n"
        "    r = r - (tonumber(v.chance or v.weight) or 0)\n"
        "    if r <= 0 then pick = v break end\n"
        "  end\n"
        // 和游戏一致：有档位表就一定有 variant；档位没有值（24/25/32 这类）时是空表 {}
        "  if pick then o.variant = { value = pick.value } end\n"
        "end\n";
    if (!variant.empty())
        code += "o.variant = { value = " + Quote(variant) + " }\n";
    code += "u.randomOptions = u.randomOptions or {}\n"
            "local slot = math.min(" + std::to_string(slot) + ", #u.randomOptions + 1)\n"
            "u.randomOptions[slot] = o\n"
            "u.randomOption = u.randomOptions[1]\n"
            "return 'ok'\n";
    const Def* d = Find(id);
    const std::string what = "「" + (d ? d->name : id) + (variant.empty() ? "" : " " + variant) + "」";
    const bool adding = slot > static_cast<int>(unit_.slots.size());
    Act(std::move(code), adding ? "특성을 추가했습니다. " + what : std::to_string(slot) + "번 특성을 교체했습니다. " + what);
}

void OptionEditor::RemoveOption(int slot)
{
    if (Busy() || !unit_.valid)
        return;
    std::string code = CheckSameUnit(unit_.sym, unit_.rosterIndex) +
        "if u.randomOptions and u.randomOptions[" + std::to_string(slot) + "] then table.remove(u.randomOptions, " +
        std::to_string(slot) + ") end\n" +
        "u.randomOption = u.randomOptions and u.randomOptions[1] or nil\n"
        "return 'ok'\n";
    Act(std::move(code), std::to_string(slot) + "번 특성을 삭제했습니다.");
}
