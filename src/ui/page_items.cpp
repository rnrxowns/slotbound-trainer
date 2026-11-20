// 「物品」页：背包（改等级 / 删除）+ 全部物品（按品阶分组，直接放进背包）

#include <cstdio>

#include "labels.h"
#include "pages.h"
#include "widgets.h"

namespace ui {

namespace {

using Def = ItemEditor::Def;

void DrawInventory(ItemEditor& editor)
{
    const float scale = Scale();
    const auto& inv = editor.Inventory();
    SectionTitle("가방");
    ImGui::Text("아이템 %d개 / 가방 %d칸", static_cast<int>(inv.size()), editor.Capacity());
    if (static_cast<int>(inv.size()) > editor.Capacity()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kAmber, "가방 용량을 초과했습니다. 일부 아이템은 화면에 표시되지 않을 수 있습니다. "
                                   "상점에서는 보유 중인 아이템의 강화만 가능합니다.");
        ImGui::PopTextWrapPos();
    }
    if (ImGui::BeginTable("inv", 4, kSlotTable)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 150.0f * scale);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 70.0f * scale);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 40.0f * scale);
        for (const ItemEditor::Owned& o : inv) {
            const Def* d = editor.Find(o.id);
            ImGui::TableNextRow();
            ImGui::PushID(o.index);
            ImGui::TableNextColumn();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(d ? ItemRarity(d->rarity).color : kRed, "%s", d ? d->name.c_str() : o.id.c_str());
            ImGui::PopTextWrapPos();
            ImGui::TableNextColumn();
            int level = o.level;
            ImGui::BeginDisabled(editor.Busy());
            if (LevelCombo("##lv", level, o.maxLevel))
                editor.SetLevel(o.index, o.id, level);
            ImGui::EndDisabled();
            ImGui::TableNextColumn();
            if (d)
                ImGui::TextWrapped("%s", LevelText(d->levels, o.level).c_str());
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(editor.Busy());
            if (ImGui::SmallButton("삭제###remove"))
                editor.Remove(o.index, o.id);
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (inv.empty())
        ImGui::TextDisabled("가방이 비어 있습니다.");
    HintText(editor.Message());
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("아이템별 최대 레벨까지 설정할 수 있습니다. 삭제한 아이템의 정수는 돌려받을 수 없습니다. "
                        "변경은 전투 중에도 즉시 적용되며 저장 데이터에 기록됩니다.");
    ImGui::PopTextWrapPos();
}

void DrawCatalogRow(ItemEditor& editor, const Def& d, int& level)
{
    const int ownedLevel = editor.OwnedLevel(d.id);
    const RarityStyle style = ItemRarity(d.rarity);
    ImGui::TableNextRow();
    ImGui::PushID(d.id.c_str());
    ImGui::TableNextColumn();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(style.color, "%s", d.name.c_str());
    ImGui::TextDisabled("%s · %s · 정수 %d", d.type.c_str(), style.name, d.price);
    if (!d.unlocked)
        ImGui::TextColored(kAmber, "강화 트리 미해금");
    if (!d.stage.empty())
        ImGui::TextDisabled("스테이지 %s 아이템", d.stage.c_str());
    ImGui::PopTextWrapPos();
    ImGui::TableNextColumn();
    ImGui::TextWrapped("%s", LevelText(d.levels, level).c_str());
    if (d.levels.size() > 1)
        LevelsTooltip(d.levels);
    if (ownedLevel > 0)
        ImGui::TextColored(kGreen, "보유 중 · 레벨 %d", ownedLevel);
    ImGui::TableNextColumn();
    LevelCombo("##lv", level, d.maxLevel);
    ImGui::TableNextColumn();
    ImGui::BeginDisabled(editor.Busy() || ownedLevel == level);
    if (ImGui::SmallButton(ownedLevel > 0 ? "레벨 변경###set_level" : "가방에 추가###add"))
        editor.Add(d.id, level);
    ImGui::EndDisabled();
    ImGui::PopID();
}

void DrawCatalog(ItemEditor& editor, ItemsUi& ui)
{
    const float scale = Scale();
    SectionTitle("모든 아이템");
    ImGui::SetNextItemWidth(200.0f * scale);
    ImGui::InputTextWithHint("##filter", "이름 또는 효과 검색", ui.filter, sizeof(ui.filter));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("보유한 아이템은 선택한 레벨로 변경합니다. 미해금 아이템과 다른 스테이지의 아이템도 추가할 수 있습니다.");
    ImGui::PopTextWrapPos();
    if (editor.Catalog().empty()) {
        ImGui::TextDisabled("아이템 목록을 불러오는 중…");
        return;
    }

    // 每件物品当前选的等级（默认满级）；搜索按这一级的效果文字匹配
    auto pickedLevel = [&](const Def& d) -> int& { return ui.levelPick.try_emplace(d.id, d.maxLevel).first->second; };
    auto matches = [&](const Def& d) {
        return !ui.filter[0] || d.name.find(ui.filter) != std::string::npos ||
               LevelText(d.levels, pickedLevel(d)).find(ui.filter) != std::string::npos;
    };

    // 按品阶分组：传说 → 稀有 → 罕见 → 普通；搜索时只列有匹配的组，括号里是匹配数
    ImGui::BeginChild("icat", ImVec2(0.0f, 380.0f * scale));
    for (const char* rarity : kItemRarities) {
        int total = 0;
        for (const Def& d : editor.Catalog())
            total += d.rarity == rarity && matches(d);
        if (total == 0)
            continue;
        const RarityStyle style = ItemRarity(rarity);
        char id[16];
        snprintf(id, sizeof(id), "item_%s", rarity);
        if (!GroupHeader(style.name, style.color, total, id))
            continue;
        if (!ImGui::BeginTable(rarity, 4, kListTable))
            continue;
        ImGui::TableSetupColumn("아이템", ImGuiTableColumnFlags_WidthFixed, 150.0f * scale);
        ImGui::TableSetupColumn("효과", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("레벨", ImGuiTableColumnFlags_WidthFixed, 72.0f * scale);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 94.0f * scale);
        for (const Def& d : editor.Catalog())
            if (d.rarity == rarity && matches(d))
                DrawCatalogRow(editor, d, pickedLevel(d));
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

} // namespace

void DrawItemsPage(MenuContext& ctx)
{
    if (!DrawBridgeBar(ctx, "아이템 편집"))
        return;
    ctx.pages.shown.items = true;
    ItemEditor& editor = ctx.pages.items;
    ErrorText(editor.Error());
    if (!editor.Loaded()) {
        ImGui::TextDisabled("가방을 불러오는 중…");
        return;
    }
    DrawInventory(editor);
    DrawCatalog(editor, ctx.pages.itemsUi);
}

} // namespace ui
