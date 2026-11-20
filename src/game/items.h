#pragma once

#include <string>
#include <vector>

#include "lua_editor.h"

// 物品（商店里买的那些，背包在战场下方）编辑：经游戏内的 slotbound_lua.dll 执行 Lua。
//
// 游戏里的结构（反汇编 love.mousepressed 的购买段、restorePurchasedItems，见 docs/findings.md「物品」）：
//   require('src.game.item').getAll() / getById(id)   63 件定义：id、price、rarity C/U/R/L、maxLevel、typeKo、各级数值表
//   ui._itemPurchased[i]       背包：定义的浅拷贝 + level；ui._itemPurchasedKeys[i] = 'p' .. ui._itemPanelNextId（面板动画用的键）
//   买新的：复制、level = 1、updateEffectString、插到末尾；买重复的：level = min(maxLevel, level + 1)
//   getItemInventoryCapacity() = min(8, 6 + 加成)：游戏买新物品时按它判断满没满
//   效果都是战斗中实时查 _itemPurchased（没有 apply 一类的钩子）→ 直接改列表就生效，存档按 id + level 恢复
class ItemEditor : public LuaEditor {
public:
    struct Def {
        std::string id, name, type, rarity;
        std::string stage;               // 只在某一关出的物品：关卡号；所有关都出 = 空
        int price = 0, maxLevel = 1;
        bool unlocked = true;            // initiallyUnlocked（没解锁的商店里不出）
        std::vector<std::string> levels; // Level effects with text in place of icons. Index 0 is level 1.
    };
    struct Owned {
        int index = 0;                   // 1 起
        std::string id;
        int level = 1, maxLevel = 1;
    };

    // 放进背包：已有同 id 的就把它改成 level 级，没有就新增一件
    void Add(const std::string& id, int level);
    void SetLevel(int index, const std::string& id, int level);
    void Remove(int index, const std::string& id);

    bool Loaded() const { return loaded_; }
    const std::vector<Def>& Catalog() const { return catalog_.All(); }
    const Def* Find(const std::string& id) const { return catalog_.Find(id); }
    const std::vector<Owned>& Inventory() const { return inventory_; }
    int Capacity() const { return capacity_; }
    // 背包里这件物品的等级；没有 = 0
    int OwnedLevel(const std::string& id) const;

private:
    std::string PollCode() const override;
    void OnPoll(const bridge::Result& r) override;
    const char* CatalogCode() const override;
    const char* CatalogName() const override { return "아이템 목록"; }
    void OnCatalog(const std::string& text) override;
    std::string ActionError(const bridge::Result& r) const override;

    DefTable<Def> catalog_;
    std::vector<Owned> inventory_;
    int capacity_ = 6;
    bool loaded_ = false;
};
