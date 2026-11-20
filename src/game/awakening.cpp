#include "awakening.h"

#include <cstdlib>

#include "luaops.h"

namespace {

using namespace luaops;

// 第一行：H  sym  阵容序号  有觉醒(1/0)  职业 id  版本  容量  当前等级  已选(逗号)
// D 行：  D  id  名字  类别  是否本职业候选  各级效果（| 分隔）
constexpr const char* kUnitLua = R"lua(
if not u then return 'none' end
local FS = '\31'
local A = require('src.game.awakening')
local aw = type(u.awakening) == 'table' and u.awakening or {}
local ver = tonumber(aw.version or u.awakeningVersion) or 10
local okId, ident = pcall(A.identity, u)
ident = okId and ident or nil
local okPool, pool = pcall(A.pool, u)
pool = okPool and type(pool) == 'table' and pool or {}
local okCat, cat = pcall(A.catalog, u)
local cands = okCat and type(cat) == 'table' and cat.candidates or {}
local sel = type(aw.selected) == 'table' and aw.selected or {}
local okCap, cap = pcall(A.capacity, u)
local lv = 0
if sel[1] then local ok, l = pcall(A.level, u, sel[1]); lv = ok and tonumber(l) or 0 end
local out = { table.concat({ 'H', tostring(u.sym), tostring(u.index or 0), (ident and #pool > 0) and '1' or '0',
  tostring(ident or ''), tostring(ver), tostring(okCap and cap or 0), tostring(lv), table.concat(sel, ',') }, FS) }
local listed = {}
local function emit(id, inPool)
  if listed[id] then return end
  listed[id] = true
  local c = cands[id] or {}
  local pre = 'awakening.' .. ver .. '.' .. id
  local levels = {}
  for L = 1, 5 do
    local st = c.stages and c.stages[L]
    levels[#levels+1] = textField(localizedText(pre .. '.' .. L .. '.text', st and st.text))
  end
  out[#out+1] = table.concat({ 'D', id, textField(localizedText(pre .. '.name', c.name) or id), textField(c.category),
    inPool and '1' or '0', table.concat(levels, '|') }, FS)
end
for _, id in ipairs(pool) do emit(tostring(id), true) end
for _, id in ipairs(sel) do emit(tostring(id), false) end
return table.concat(out, '\n')
)lua";

// 改完 selected 后：让游戏自己规整（去重、最多 5 个、重设 stage / levels），
// 战场单位若有自己的一份觉醒表也同步（同一张表就不用）
constexpr const char* kFinishLua = R"lua(
local A = require('src.game.awakening')
pcall(A.normalize, u)
local a = u.allyRef
if type(a) == 'table' and battlefield.mode == 'battle' and type(a.awakening) == 'table' and a.awakening ~= u.awakening then
  local s = {}
  for i, id in ipairs(u.awakening.selected) do s[i] = id end
  a.awakening.selected = s
  pcall(A.normalize, a)
end
return 'ok'
)lua";

} // namespace

std::string AwakeningEditor::PollCode() const
{
    return std::string(kResolveUnit) + kLocalizedTextLua + kUnitLua;
}

void AwakeningEditor::OnPoll(const bridge::Result& r)
{
    unit_ = {};
    std::vector<Def> defs;
    if (r.ok && r.text != "none") {
        const auto lines = Split(r.text, '\n');
        const auto head = Split(lines[0], kFs);
        if (head.size() >= 9 && head[0] == "H") {
            unit_.valid = true;
            unit_.sym = head[1];
            unit_.rosterIndex = atoi(head[2].c_str());
            unit_.hasAwakening = head[3] == "1";
            unit_.identity = head[4];
            unit_.version = atoi(head[5].c_str());
            unit_.capacity = atoi(head[6].c_str());
            unit_.level = atoi(head[7].c_str());
            if (!head[8].empty())
                unit_.selected = Split(head[8], ',');
            for (size_t i = 1; i < lines.size(); ++i) {
                const auto f = Split(lines[i], kFs);
                if (f.size() >= 6 && f[0] == "D")
                    defs.push_back({ f[1], f[2], f[3], LevelTexts(f[5]), f[4] == "1" });
            }
        }
    }
    defs_.Assign(std::move(defs));
}

void AwakeningEditor::Set(int slot, const std::string& id)
{
    if (Busy() || !unit_.valid || !unit_.hasAwakening)
        return;
    std::string code = CheckSameUnit(unit_.sym, unit_.rosterIndex) +
        "local A = require('src.game.awakening')\n"
        "pcall(A.normalize, u)\n"
        "local aw = u.awakening\n"
        "if type(aw) ~= 'table' then return '이 유닛의 각성 데이터가 없습니다.' end\n"
        "aw.selected = aw.selected or {}\n"
        "local id = " + Quote(id) + "\n"
        "local cat = A.catalog(u)\n"
        "if not (cat and cat.candidates and cat.candidates[id]) then return '각성 목록에 해당 ID가 없습니다. ID ' .. id end\n"
        "local slot = math.min(" + std::to_string(slot) + ", #aw.selected + 1)\n"
        "for i, x in ipairs(aw.selected) do if x == id and i ~= slot then return '이미 적용된 각성입니다.' end end\n"
        "if slot > #aw.selected and #aw.selected >= " + std::to_string(kMaxSelected) + " then return '각성은 최대 5개까지 적용할 수 있습니다.' end\n"
        "aw.selected[slot] = id\n"
        "aw.seen = aw.seen or {}\n"
        "aw.seen[id] = true\n" +
        kFinishLua;
    const Def* d = Find(id);
    const std::string what = "「" + (d ? d->name : id) + "」";
    Act(std::move(code), slot > static_cast<int>(unit_.selected.size()) ? what + " 각성을 추가했습니다."
                                                                         : std::to_string(slot) + "번 각성을 교체했습니다. " + what);
}

void AwakeningEditor::Remove(int slot)
{
    if (Busy() || !unit_.valid || !unit_.hasAwakening)
        return;
    std::string code = CheckSameUnit(unit_.sym, unit_.rosterIndex) +
        "local aw = u.awakening\n"
        "if type(aw) ~= 'table' or type(aw.selected) ~= 'table' then return '이 유닛의 각성 데이터가 없습니다.' end\n"
        "if aw.selected[" + std::to_string(slot) + "] then table.remove(aw.selected, " + std::to_string(slot) + ") end\n" +
        kFinishLua;
    Act(std::move(code), std::to_string(slot) + "번 각성을 삭제했습니다.");
}
