#pragma once

// 菜单各页的画法（menu.cpp 按标签页调用）。每页一个 .cpp：
//   page_resources.cpp  资源（金币 / 旋转 / 水晶）+ 状态栏
//   page_unit.cpp       单位：等级、属性、技能；下挂 unit_options / unit_imprints / unit_awakening 三个子页
//   page_items.cpp      物品
//   page_slot.cpp       老虎机
//   page_system.cpp     诊断、设置

#include <string>

#include "menu.h"

namespace ui {

void DrawResourcesPage(MenuContext& ctx);
const char* VarStateText(const LuaVar& var);
const char* VarMethodText(const LuaVar& var);

// 需要注入的页顶部：没注入时给注入按钮并返回 false；已注入时显示状态和「卸载注入」
bool DrawBridgeBar(const MenuContext& ctx, const char* feature);

void DrawUnitPage(MenuContext& ctx);
void DrawOptionsBody(OptionEditor& editor, OptionsUi& ui);
void DrawImprintsBody(ImprintEditor& editor, ImprintsUi& ui);
void DrawAwakeningBody(AwakeningEditor& editor, AwakeningUi& ui);

void DrawItemsPage(MenuContext& ctx);
void DrawSlotPage(MenuContext& ctx);
void DrawDiagnosticsPage(MenuContext& ctx);
void DrawSettingsPage(MenuContext& ctx);

// 词条 / 刻印 / 觉醒的目标槽位：0 = 新增（默认）。换了单位、或选中的槽被删掉了，都回到新增
void FollowTargetSlot(int& target, std::string& unitKey, const std::string& sym, int rosterIndex, int count);

// 单位的唯一键：兵种 + 阵容序号
inline std::string UnitKey(const std::string& sym, int rosterIndex)
{
    return sym + "#" + std::to_string(rosterIndex);
}

} // namespace ui
