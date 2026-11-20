// 「单位」页 →「觉醒」子页

#include <algorithm>

#include "pages.h"
#include "widgets.h"

namespace ui {

namespace {

using Def = AwakeningEditor::Def;
constexpr int kMax = AwakeningEditor::kMaxSelected;
constexpr const char* kRoman[] = { "", "I", "II", "III", "IV", "V" };

bool IsSelected(const AwakeningEditor::Unit& unit, const std::string& id)
{
    return std::find(unit.selected.begin(), unit.selected.end(), id) != unit.selected.end();
}

void DrawSelected(AwakeningEditor& editor, AwakeningUi& ui, const AwakeningEditor::Unit& unit, int shownLevel)
{
    const float scale = Scale();
    const int count = static_cast<int>(unit.selected.size());
    if (!ImGui::BeginTable("aslots", 4, kSlotTable))
        return;
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 44.0f * scale);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 142.0f * scale);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 50.0f * scale);
    for (int i = 0; i < count; ++i) {
        const Def* d = editor.Find(unit.selected[i]);
        ImGui::TableNextRow();
        ImGui::PushID(i);
        ImGui::TableNextColumn();
        SlotPicker(ui.targetSlot, i + 1, i + 1);
        ImGui::TableNextColumn();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(d ? kGreen : kRed, "%s", d ? d->name.c_str() : unit.selected[i].c_str());
        if (d && !d->inPool)
            ImGui::TextColored(kAmber, "다른 직업 전용");
        ImGui::PopTextWrapPos();
        ImGui::TableNextColumn();
        if (d)
            ImGui::TextWrapped("%s", LevelText(d->levels, shownLevel).c_str());
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(editor.Busy());
        if (ImGui::SmallButton("삭제###删除"))
            editor.Remove(i + 1);
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    if (count < kMax) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::PushID("new");
        SlotPicker(ui.targetSlot, 0, count + 1);
        ImGui::PopID();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("추가 (기본값)");
        ImGui::TableNextColumn();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("하나 더 추가하면 모든 각성이 %s레벨이 됩니다.", kRoman[count + 1]);
        ImGui::PopTextWrapPos();
    } else if (ui.targetSlot == 0) {
        ui.targetSlot = 1; // 满 5 个只能替换
    }
    ImGui::EndTable();
}

void DrawCandidates(AwakeningEditor& editor, AwakeningUi& ui, const AwakeningEditor::Unit& unit, int shownLevel)
{
    const float scale = Scale();
    const int count = static_cast<int>(unit.selected.size());
    // 选了以后的等级：新增 = 个数 + 1，替换 = 个数不变
    const int levelAfter = std::min(kMax, ui.targetSlot > 0 ? count : count + 1);
    if (!ImGui::BeginTable("acatalog", 3, kListTable))
        return;
    ImGui::TableSetupColumn("각성", ImGuiTableColumnFlags_WidthFixed, 142.0f * scale);
    ImGui::TableSetupColumn("선택 후 레벨의 효과", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 60.0f * scale);
    ImGui::TableHeadersRow();
    for (const Def& d : editor.Defs()) {
        if (!d.inPool)
            continue;
        const bool owned = IsSelected(unit, d.id);
        ImGui::TableNextRow();
        ImGui::PushID(d.id.c_str());
        ImGui::TableNextColumn();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(owned ? kGreen : kText, "%s", d.name.c_str());
        ImGui::TextDisabled("%s", d.id.c_str());
        ImGui::PopTextWrapPos();
        ImGui::TableNextColumn();
        ImGui::TextWrapped("%s", LevelText(d.levels, owned ? shownLevel : levelAfter).c_str());
        LevelsTooltip(d.levels, kRoman);
        ImGui::TableNextColumn();
        if (owned) {
            ImGui::TextDisabled("선택됨");
        } else {
            ImGui::BeginDisabled(editor.Busy() || (ui.targetSlot == 0 && count >= kMax));
            if (ImGui::SmallButton("적용###用这个"))
                editor.Set(ui.targetSlot > 0 ? ui.targetSlot : count + 1, d.id);
            ImGui::EndDisabled();
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

} // namespace

void DrawAwakeningBody(AwakeningEditor& editor, AwakeningUi& ui)
{
    ErrorText(editor.Error());
    const AwakeningEditor::Unit& unit = editor.Selected();
    if (!unit.valid) {
        ImGui::TextDisabled("선택한 유닛의 각성을 불러오는 중…");
        return;
    }
    if (!unit.hasAwakening) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("이 유닛은 아직 각성할 수 없습니다. 보통 3성에서 직업을 선택하면 각성할 수 있습니다.");
        ImGui::PopTextWrapPos();
        return;
    }
    const int count = static_cast<int>(unit.selected.size());
    const int shownLevel = unit.level >= 0 && unit.level <= kMax ? unit.level : 0;
    ImGui::Text("%d개 선택 · 모두 %s레벨", count, kRoman[shownLevel]);
    ImGui::SameLine();
    ImGui::TextDisabled("(게임 슬롯 %d개)", unit.capacity);
    if (unit.version == 10) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("각성 레벨은 선택한 각성 수와 같으며, 5개를 선택하면 모두 V레벨이 됩니다. 효과는 전투 중 즉시 적용되고 게임에 저장됩니다.");
        ImGui::PopTextWrapPos();
    }

    FollowTargetSlot(ui.targetSlot, ui.unitKey, unit.sym, unit.rosterIndex, count);
    DrawSelected(editor, ui, unit, shownLevel);
    HintText(editor.Message());

    SectionTitle("현재 직업의 각성");
    TargetSlotHint(ui.targetSlot);
    DrawCandidates(editor, ui, unit, shownLevel);
}

} // namespace ui
