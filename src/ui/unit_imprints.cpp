// 「单位」页 →「刻印」子页

#include "pages.h"
#include "widgets.h"

namespace ui {

namespace {

using Def = ImprintEditor::Def;

// 刻印最多几级：按定义的等级表；查不到定义时按游戏的 5 级
int MaxLevel(const Def* d)
{
    if (!d)
        return 5;
    return d->levels.empty() ? 1 : static_cast<int>(d->levels.size());
}

void DrawSlotCount(ImprintEditor& editor, const ImprintEditor::Unit& unit)
{
    ImGui::AlignTextToFramePadding();
    ImGui::Text("각인 슬롯 %d개", unit.maxSlots);
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(420.0f * Scale());
        ImGui::TextUnformatted("게임 패널에 표시되는 각인 슬롯의 수입니다. 유닛을 흡수해 새 각인을 얻을 때도 이 수가 최대치로 적용됩니다.\n"
                              "유닛 등급이 바뀌면 등급에 맞게 재설정됩니다 (일반 1 / 고급 2 / 희귀·특수 3 / 전설 4 / 프리즘 5).");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(editor.Busy());
    if (ImGui::SmallButton(" - ") && unit.maxSlots > 1)
        editor.SetMaxSlots(unit.maxSlots - 1);
    ImGui::SameLine();
    if (ImGui::SmallButton(" + "))
        editor.SetMaxSlots(unit.maxSlots + 1);
    ImGui::EndDisabled();
    if (static_cast<int>(unit.slots.size()) > unit.maxSlots) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kAmber, "슬롯 수를 초과한 각인은 패널에 표시되지 않을 수 있습니다. 효과는 적용됩니다.");
        ImGui::PopTextWrapPos();
    }
}

void DrawSlots(ImprintEditor& editor, ImprintsUi& ui, const ImprintEditor::Unit& unit)
{
    const float scale = Scale();
    const int count = static_cast<int>(unit.slots.size());
    if (!ImGui::BeginTable("islots", 5, kSlotTable))
        return;
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 44.0f * scale);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 134.0f * scale);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 70.0f * scale);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 50.0f * scale);
    for (const ImprintEditor::Slot& s : unit.slots) {
        const Def* d = editor.Find(s.id);
        ImGui::TableNextRow();
        ImGui::PushID(s.index);
        ImGui::TableNextColumn();
        SlotPicker(ui.targetSlot, s.index, s.index);
        ImGui::TableNextColumn();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(d ? kGreen : kRed, "%s", d ? d->name.c_str() : s.id.c_str());
        ImGui::PopTextWrapPos();
        ImGui::TableNextColumn();
        int level = s.level;
        ImGui::BeginDisabled(editor.Busy() || !d);
        if (LevelCombo("##lv", level, MaxLevel(d)))
            editor.Set(s.index, s.id, level);
        ImGui::EndDisabled();
        ImGui::TableNextColumn();
        if (d)
            ImGui::TextWrapped("%s", LevelText(d->levels, s.level).c_str());
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(editor.Busy());
        if (ImGui::SmallButton("삭제###删除"))
            editor.Remove(s.index);
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::PushID("new");
    SlotPicker(ui.targetSlot, 0, count + 1);
    ImGui::PopID();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("추가 (기본값)");
    ImGui::TableNextColumn();
    ImGui::TableNextColumn();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("아래에서 ‘적용’을 누르면 각인이 추가됩니다. 필요한 슬롯은 자동으로 늘어납니다.");
    ImGui::PopTextWrapPos();
    ImGui::EndTable();
}

void DrawRow(ImprintEditor& editor, ImprintsUi& ui, const ImprintEditor::Unit& unit, const Def& d, int& level)
{
    int owned = 0;
    for (const ImprintEditor::Slot& s : unit.slots)
        owned += s.id == d.id;
    ImGui::TableNextRow();
    ImGui::PushID(d.id.c_str());
    ImGui::TableNextColumn();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(owned ? kGreen : kText, "%s", d.name.c_str());
    ImGui::TextDisabled("%s", d.id.c_str());
    ImGui::PopTextWrapPos();
    ImGui::TableNextColumn();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextWrapped("%s", LevelText(d.levels, level).c_str());
    LevelsTooltip(d.levels);
    if (d.id == unit.signature)
        ImGui::TextDisabled("이 유닛의 고유 각인");
    if (owned > 0)
        ImGui::TextColored(kAmber, "이미 %d개 보유 중입니다. 중복 효과의 계산 방식은 확인되지 않았습니다.", owned);
    ImGui::PopTextWrapPos();
    ImGui::TableNextColumn();
    LevelCombo("##lv", level, MaxLevel(&d));
    ImGui::TableNextColumn();
    ImGui::BeginDisabled(editor.Busy());
    if (ImGui::SmallButton("적용###用这个"))
        editor.Set(ui.targetSlot > 0 ? ui.targetSlot : static_cast<int>(unit.slots.size()) + 1, d.id, level);
    ImGui::EndDisabled();
    ImGui::PopID();
}

} // namespace

void DrawImprintsBody(ImprintEditor& editor, ImprintsUi& ui)
{
    const float scale = Scale();
    if (ImGui::SmallButton("각인 목록 새로 고침###重读刻印表"))
        editor.ReloadCatalog();
    ErrorText(editor.Error());

    const ImprintEditor::Unit& unit = editor.Selected();
    if (!unit.valid) {
        ImGui::TextDisabled("선택한 유닛의 각인을 불러오는 중…");
        return;
    }
    if (const Def* sig = editor.Find(unit.signature))
        ImGui::TextDisabled("이 병종과 등급의 고유 각인 · %s", sig->name.c_str());

    DrawSlotCount(editor, unit);
    FollowTargetSlot(ui.targetSlot, ui.unitKey, unit.sym, unit.rosterIndex, static_cast<int>(unit.slots.size()));
    DrawSlots(editor, ui, unit);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("드롭다운에서 레벨을 바로 바꿀 수 있습니다. 기본 동작은 추가이며, 교체하려면 왼쪽 원을 선택하세요. 전투가 끝난 뒤 변경하는 편이 안전합니다.");
    ImGui::PopTextWrapPos();
    HintText(editor.Message());

    SectionTitle("전체 각인");
    ImGui::SetNextItemWidth(200.0f * scale);
    ImGui::InputTextWithHint("##ifilter", "이름 / 효과 검색", ui.filter, sizeof(ui.filter));
    ImGui::SameLine();
    TargetSlotHint(ui.targetSlot);
    if (editor.Catalog().empty()) {
        ImGui::TextDisabled("각인 목록을 불러오는 중…");
        return;
    }

    ImGui::BeginChild("icatalog", ImVec2(0.0f, 380.0f * scale));
    if (ImGui::BeginTable("imprints", 4, kListTable)) {
        ImGui::TableSetupColumn("각인", ImGuiTableColumnFlags_WidthFixed, 142.0f * scale);
        ImGui::TableSetupColumn("효과", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("레벨", ImGuiTableColumnFlags_WidthFixed, 70.0f * scale);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 60.0f * scale);
        for (const Def& d : editor.Catalog()) {
            int& level = ui.levelPick.try_emplace(d.id, MaxLevel(&d)).first->second;
            if (ui.filter[0] && d.name.find(ui.filter) == std::string::npos &&
                LevelText(d.levels, level).find(ui.filter) == std::string::npos)
                continue;
            DrawRow(editor, ui, unit, d, level);
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

} // namespace ui
