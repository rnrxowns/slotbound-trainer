#pragma once

#include <map>
#include <string>
#include <vector>

#include "lua_editor.h"

// 词条（random option）编辑：经游戏内的 slotbound_lua.dll 在游戏主线程执行 Lua。
//
// 游戏里的结构（src.data.randomoption / battlefield）：
//   RO.catalog[i]          词条定义：id、name、effect、rarity、source、variants{ {value='+10%', chance=0.05}, ... }
//   RO.rollForId(id)       生成一个词条实例 { id, name, effect, icon, rarity, source, variant = { value } }
//   unit.randomOptions     单位的词条列表；unit.randomOption 永远是第 1 个
//   tooltip.lockedTarget   当前点选（锁定）的目标；kind == 'roster' 时 .ref 是己方单位
//   battlefield.beginBattle 战斗开始时按 randomOptions 算属性倍率并缓存 → 战斗外改，下一场生效
class OptionEditor : public LuaEditor {
public:
    struct Variant {
        std::string value;  // "+10%"
        double chance = 0;  // 0.05
    };
    struct Def {
        std::string id, name, rarity, source, effect, notes; // effect 已把图标换成文字
        bool unlocked = true;
        std::vector<Variant> variants;
    };
    struct Slot {
        int index = 0; // 1 起
        std::string id, value;
    };
    // 某个词条对这个单位适不适用
    struct Fit {
        std::string reason; // 空 = 适用；否则是不适用的原因（游戏正常抽不到）
        int owned = 0;      // 这个单位已经有几个（重复会相乘叠加）
    };
    struct Unit {
        bool valid = false;
        std::string sym, grade;
        int rank = 0, rosterIndex = 0;
        bool locked = true; // false = 面板已关，用的是「最近选中」的单位
        std::vector<Slot> slots;
        std::map<std::string, Fit> fit; // 词条 id → 适用情况
    };

    // 把 slot（1 起；= 现有数量 + 1 表示新增）设成 id 的词条，variant 为空 = 随机档位
    void SetOption(int slot, const std::string& id, const std::string& variant);
    void RemoveOption(int slot);

    const std::vector<Def>& Catalog() const { return catalog_.All(); }
    const Def* Find(const std::string& id) const { return catalog_.Find(id); }
    const Unit& Selected() const { return unit_; }

private:
    std::string PollCode() const override;
    void OnPoll(const bridge::Result& r) override;
    const char* CatalogCode() const override;
    const char* CatalogName() const override { return "특성 목록"; }
    void OnCatalog(const std::string& text) override;

    DefTable<Def> catalog_;
    Unit unit_;
};
