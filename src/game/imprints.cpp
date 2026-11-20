#include "imprints.h"

#include <cstdlib>

#include "luaops.h"

namespace {

using namespace luaops;

// 刻印目录：id  名字  tag  各级效果（| 分隔）
constexpr const char* kCatalogLua = R"lua(
local C = require('src.data.constants')
local FS = '\31'
local ids = {}
for id in pairs(C.imprint.definitions) do ids[#ids+1] = id end
local names = {}
for _, id in ipairs(ids) do
  names[id] = textField(localizedText('imprint.name.' .. id, C.imprint.definitions[id].name) or id)
end
table.sort(ids, function(a, b) return names[a] < names[b] end)
local out = {}
for _, id in ipairs(ids) do
  local d = C.imprint.definitions[id]
  local lv = {}
  for L = 1, #(d.levels or {}) do
    lv[#lv+1] = textField(localizedText('imprint.desc.' .. id .. '_' .. L, d.levels[L] and d.levels[L].desc))
  end
  out[#out+1] = table.concat({ id, names[id], tostring(d.tag), table.concat(lv, '|') }, FS)
end
return table.concat(out, '\n')
)lua";

// 选中单位：第一行 sym  品级  等级  阵容序号  locked/last  槽数  天生刻印 id
//   S 行：槽位  id  等级
constexpr const char* kUnitLua = R"lua(
if not u then return 'none' end
local FS = '\31'
local sig = ''
local B = package.loaded['src.game.battlefield']
if B and B.signatureImprint then
  local ok, r = pcall(B.signatureImprint, u)
  if ok and r then sig = type(r) == 'table' and tostring(r.id or '') or tostring(r) end
end
local out = { table.concat({ tostring(u.sym), tostring(u.grade), tostring(u.rank or 0), tostring(u.index or 0), how,
  tostring(u.maxImprintSlots or 1), sig }, FS) }
for i, im in ipairs(u.imprints or {}) do
  out[#out+1] = table.concat({ 'S', tostring(i), tostring(im.id), tostring(im.level or 1) }, FS)
end
return table.concat(out, '\n')
)lua";

} // namespace

const char* ImprintEditor::CatalogCode() const
{
    static const std::string code = std::string(kLocalizedTextLua) + kCatalogLua;
    return code.c_str();
}

void ImprintEditor::OnCatalog(const std::string& text)
{
    std::vector<Def> defs;
    for (const std::string& line : Split(text, '\n')) {
        const auto f = Split(line, kFs);
        if (f.size() >= 4)
            defs.push_back({ f[0], f[1], f[2], LevelTexts(f[3]) });
    }
    catalog_.Assign(std::move(defs));
}

std::string ImprintEditor::PollCode() const
{
    return std::string(kResolveUnit) + kUnitLua;
}

void ImprintEditor::OnPoll(const bridge::Result& r)
{
    unit_ = {};
    if (!r.ok || r.text == "none")
        return;
    const auto lines = Split(r.text, '\n');
    const auto head = Split(lines[0], kFs);
    if (head.size() < 7)
        return;
    unit_.valid = true;
    unit_.sym = head[0];
    unit_.grade = head[1];
    unit_.rank = atoi(head[2].c_str());
    unit_.rosterIndex = atoi(head[3].c_str());
    unit_.locked = head[4] == "locked";
    unit_.maxSlots = atoi(head[5].c_str());
    unit_.signature = head[6];
    for (size_t i = 1; i < lines.size(); ++i) {
        const auto f = Split(lines[i], kFs);
        if (f.size() >= 4 && f[0] == "S")
            unit_.slots.push_back({ atoi(f[1].c_str()), f[2], atoi(f[3].c_str()) });
    }
}

void ImprintEditor::Set(int slot, const std::string& id, int level)
{
    if (Busy() || !unit_.valid)
        return;
    // 实例照游戏的做法生成：signatureImprint 的上值里有个 mk(定义, 稀有度)（只会生成 1 级），
    // 再把 level / stats / desc 换成目标等级的；找不到 mk 就按同样的字段自己拼
    std::string code = CheckSameUnit(unit_.sym, unit_.rosterIndex) +
        "local C = require('src.data.constants')\n"
        "local d = C.imprint.definitions[" + Quote(id) + "]\n"
        "if not d then return '해당 ID의 각인을 찾을 수 없습니다.' end\n"
        "local L = math.max(1, math.min(" + std::to_string(level) + ", #(d.levels or {})))\n"
        "local im = u.imprints or {}\n"
        "u.imprints = im\n"
        "local slot = math.min(" + std::to_string(slot) + ", #im + 1)\n"
        "local old = im[slot]\n"
        "local rarity = (old and old.rarity) or u.grade or 'normal'\n"
        "local mk\n"
        "local B = package.loaded['src.game.battlefield']\n"
        "if B and B.signatureImprint then\n"
        "  local fu = require('jit.util')\n"
        "  for n = 1, 16 do\n"
        "    local nm, f = debug.getupvalue(B.signatureImprint, n)\n"
        "    if not nm then break end\n"
        "    if type(f) == 'function' and fu.funcinfo(f).params == 2 then mk = f end\n"
        "  end\n"
        "end\n"
        "local o\n"
        "if mk then local ok, r = pcall(mk, d, rarity); if ok and type(r) == 'table' and r.id == d.id then o = r end end\n"
        "if not o then\n"
        "  o = {}\n"
        "  for k, v in pairs(d) do if k ~= 'levels' then o[k] = v end end\n"
        "  o.rarity = rarity\n"
        "  o.maxLevel = #(d.levels or {})\n"
        "end\n"
        "local lv = d.levels and d.levels[L]\n"
        "o.level = L\n"
        "if lv then o.stats = lv.stats; o.desc = lv.desc or o.desc; o.shortDesc = lv.shortDesc or o.shortDesc end\n"
        "im[slot] = o\n"
        // 游戏面板按 maxImprintSlots 画格子；不够就加，免得多出来的看不见
        "if (u.maxImprintSlots or 1) < #im then u.maxImprintSlots = #im end\n"
        "return 'ok'\n";
    const Def* d = Find(id);
    const std::string what = "「" + (d ? d->name : id) + "」 레벨 " + std::to_string(level);
    Act(std::move(code), slot > static_cast<int>(unit_.slots.size()) ? "각인을 추가했습니다. " + what
                                                                     : std::to_string(slot) + "번 각인을 변경했습니다. " + what);
}

void ImprintEditor::Remove(int slot)
{
    if (Busy() || !unit_.valid)
        return;
    std::string code = CheckSameUnit(unit_.sym, unit_.rosterIndex) +
        "if u.imprints and u.imprints[" + std::to_string(slot) + "] then table.remove(u.imprints, " +
        std::to_string(slot) + ") end\n"
        "return 'ok'\n";
    Act(std::move(code), std::to_string(slot) + "번 각인을 삭제했습니다.");
}

void ImprintEditor::SetMaxSlots(int slots)
{
    if (Busy() || !unit_.valid || slots < 1)
        return;
    std::string code = CheckSameUnit(unit_.sym, unit_.rosterIndex) +
        "u.maxImprintSlots = " + std::to_string(slots) + "\n"
        "u.imprints = u.imprints or {}\n"
        "return 'ok'\n";
    Act(std::move(code), "각인 슬롯 수를 변경했습니다. 현재 " + std::to_string(slots) + "개");
}
