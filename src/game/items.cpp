#include "items.h"

#include <algorithm>
#include <cstdlib>

#include "luaops.h"

namespace {

using namespace luaops;

// 物品目录：id  名字  类型  稀有度  价格  满级  是否初始解锁  关卡  各级效果（| 分隔）
constexpr const char* kCatalogLua = R"lua(
local I = require('src.game.item')
local FS = '\31'
local out = {}
for _, d in ipairs(I.getAll()) do
  local id = tostring(d.id)
  local maxL = math.max(1, math.floor(tonumber(d.maxLevel) or 1))
  local lv = {}
  for L = 1, maxL do
    lv[#lv+1] = textField(localizedText('item.effect.' .. id .. '.' .. L, L == 1 and d.effect))
  end
  out[#out+1] = table.concat({ id, textField(localizedText('item.name.' .. id, d.name) or id), tostring(d.typeKo or ''),
    tostring(d.rarity or ''), tostring(d.price or 0), tostring(maxL), d.initiallyUnlocked == false and '0' or '1',
    tostring(d.stage or ''), table.concat(lv, '|') }, FS)
end
return table.concat(out, '\n')
)lua";

// 背包：第一行 容量；之后每行 序号  id  等级  满级
constexpr const char* kInventoryLua = R"lua(
if type(ui) ~= 'table' then return 'noui' end
local FS = '\31'
local cap = type(getItemInventoryCapacity) == 'function' and getItemInventoryCapacity() or 6
local out = { tostring(cap) }
for i, o in ipairs(ui._itemPurchased or {}) do
  out[#out+1] = table.concat({ tostring(i), tostring(o.id), tostring(o.level or 1), tostring(o.maxLevel or 1) }, FS)
end
return table.concat(out, '\n')
)lua";

// 核对背包第 index 件还是 id（防止点按钮前游戏里卖掉 / 买了别的）
std::string CheckSlot(int index, const std::string& id)
{
    return "local list = type(ui) == 'table' and ui._itemPurchased or {}\n"
           "local o = list[" + std::to_string(index) + "]\n"
           "if not o or tostring(o.id) ~= " + Quote(id) + " then return 'changed' end\n";
}

} // namespace

int ItemEditor::OwnedLevel(const std::string& id) const
{
    for (const Owned& o : inventory_)
        if (o.id == id)
            return o.level;
    return 0;
}

const char* ItemEditor::CatalogCode() const
{
    static const std::string code = std::string(kLocalizedTextLua) + kCatalogLua;
    return code.c_str();
}

void ItemEditor::OnCatalog(const std::string& text)
{
    std::vector<Def> defs;
    for (const std::string& line : Split(text, '\n')) {
        const auto f = Split(line, kFs);
        if (f.size() < 9)
            continue;
        Def d;
        d.id = f[0];
        d.name = f[1];
        d.type = f[2];
        d.rarity = f[3];
        d.price = atoi(f[4].c_str());
        d.maxLevel = std::max(1, atoi(f[5].c_str()));
        d.unlocked = f[6] == "1";
        d.stage = f[7] == "all" || f[7] == "nil" ? std::string() : f[7];
        d.levels = LevelTexts(f[8]);
        defs.push_back(std::move(d));
    }
    catalog_.Assign(std::move(defs));
}

std::string ItemEditor::PollCode() const
{
    return kInventoryLua;
}

void ItemEditor::OnPoll(const bridge::Result& r)
{
    if (!r.ok || r.text == "noui")
        return; // 读不到时保留上一次的背包
    const auto lines = Split(r.text, '\n');
    capacity_ = atoi(lines[0].c_str());
    inventory_.clear();
    for (size_t i = 1; i < lines.size(); ++i) {
        const auto f = Split(lines[i], kFs);
        if (f.size() >= 4)
            inventory_.push_back({ atoi(f[0].c_str()), f[1], atoi(f[2].c_str()), std::max(1, atoi(f[3].c_str())) });
    }
    loaded_ = true;
}

std::string ItemEditor::ActionError(const bridge::Result& r) const
{
    if (r.ok && r.text == "changed")
        return "실패: 가방의 아이템이 바뀌었습니다. 대상 아이템을 확인한 뒤 다시 시도해 주세요.";
    if (!r.ok || r.text != "ok")
        return "실패: " + r.text;
    return {};
}

void ItemEditor::Add(const std::string& id, int level)
{
    if (Busy())
        return;
    // 照游戏购买的做法：复制定义、设等级、刷新效果文字、插到末尾、分配面板键 'p' .. _itemPanelNextId
    std::string code =
        "if type(ui) ~= 'table' then return '게임 화면이 아직 준비되지 않았습니다.' end\n"
        "local I = require('src.game.item')\n"
        "local d = I.getById(" + Quote(id) + ")\n"
        "if not d then return '해당 ID의 아이템을 찾을 수 없습니다.' end\n"
        "local maxL = math.max(1, math.floor(tonumber(d.maxLevel) or 1))\n"
        "local L = math.max(1, math.min(maxL, " + std::to_string(level) + "))\n"
        "ui._itemPurchased = ui._itemPurchased or {}\n"
        "for _, o in ipairs(ui._itemPurchased) do\n"
        "  if tostring(o.id) == tostring(d.id) then\n"
        "    o.level = L\n"
        "    if type(o.updateEffectString) == 'function' then pcall(o.updateEffectString, o) end\n"
        "    return 'ok'\n"
        "  end\n"
        "end\n"
        "local o = {}\n"
        "for k, v in pairs(d) do o[k] = v end\n"
        "o.level = L\n"
        "if type(o.updateEffectString) == 'function' then pcall(o.updateEffectString, o) end\n"
        "table.insert(ui._itemPurchased, o)\n"
        "ui._itemPurchasedKeys = ui._itemPurchasedKeys or {}\n"
        "local n = tonumber(ui._itemPanelNextId) or (#ui._itemPurchased + 100)\n"
        "table.insert(ui._itemPurchasedKeys, 'p' .. tostring(n))\n"
        "ui._itemPanelNextId = n + 1\n"
        "return 'ok'\n";
    const Def* d = Find(id);
    const std::string name = "「" + (d ? d->name : id) + "」";
    const std::string lv = "레벨 " + std::to_string(level);
    Act(std::move(code), OwnedLevel(id) > 0 ? name + " 레벨 변경 완료 (" + lv + ")"
                                          : "가방에 추가했습니다. " + name + " " + lv);
}

void ItemEditor::SetLevel(int index, const std::string& id, int level)
{
    if (Busy())
        return;
    std::string code = CheckSlot(index, id) +
        "local maxL = math.max(1, math.floor(tonumber(o.maxLevel) or 1))\n"
        "o.level = math.max(1, math.min(maxL, " + std::to_string(level) + "))\n"
        "if type(o.updateEffectString) == 'function' then pcall(o.updateEffectString, o) end\n"
        "return 'ok'\n";
    const Def* d = Find(id);
    Act(std::move(code), "「" + (d ? d->name : id) + "」 레벨 변경 완료 (레벨 " + std::to_string(level) + ")");
}

void ItemEditor::Remove(int index, const std::string& id)
{
    if (Busy())
        return;
    // 背包和面板键是两条并行的列表，一起删
    std::string code = CheckSlot(index, id) +
        "table.remove(list, " + std::to_string(index) + ")\n"
        "if type(ui._itemPurchasedKeys) == 'table' and ui._itemPurchasedKeys[" + std::to_string(index) + "] then\n"
        "  table.remove(ui._itemPurchasedKeys, " + std::to_string(index) + ")\n"
        "end\n"
        "return 'ok'\n";
    const Def* d = Find(id);
    Act(std::move(code), "가방에서 「" + (d ? d->name : id) + "」 아이템을 삭제했습니다. 정수는 환불되지 않습니다.");
}
