// 「单位」页 →「词条」子页

#include <cfloat>
#include <cstdio>

#include "labels.h"
#include "pages.h"
#include "widgets.h"

namespace ui {

namespace {

using Def = OptionEditor::Def;

void DrawSlots(OptionEditor& editor, OptionsUi& ui, const OptionEditor::Unit& unit)
{
    const float scale = Scale();
    const int count = static_cast<int>(unit.slots.size());
    if (!ImGui::BeginTable("slots", 4, kSlotTable))
        return;
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 44.0f * scale);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 148.0f * scale);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 50.0f * scale);
    for (const OptionEditor::Slot& s : unit.slots) {
        const Def* d = editor.Find(s.id);
        ImGui::TableNextRow();
        ImGui::PushID(s.index);
        ImGui::TableNextColumn();
        SlotPicker(ui.targetSlot, s.index, s.index);
        ImGui::TableNextColumn();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(d ? OptionRarity(d->rarity).color : kRed, "%s %s", d ? d->name.c_str() : s.id.c_str(),
            s.value.c_str());
        ImGui::PopTextWrapPos();
        ImGui::TableNextColumn();
        if (d)
            ImGui::TextWrapped("%s", d->effect.c_str());
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(editor.Busy());
        if (ImGui::SmallButton("삭제###删除"))
            editor.RemoveOption(s.index);
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    // 游戏里没有词条数量上限：randomOptions 是普通列表，结算全用 ipairs 逐个算；
    // 只有一次抽词条的 RO.rollList(n) 会把 n 截到 5
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::PushID("new");
    SlotPicker(ui.targetSlot, 0, count + 1);
    ImGui::PopID();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("추가 (기본값)");
    ImGui::TableNextColumn();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("아래에서 ‘적용’을 누르면 특성이 추가됩니다.");
    if (count >= 5) {
        ImGui::TextColored(kAmber, "게임의 한 번 추첨 한도는 5개이며, 패널에 일부만 표시될 수 있습니다.");
    }
    ImGui::PopTextWrapPos();
    ImGui::EndTable();
}

// 档位下拉框；pick = -1 表示随机
void VariantCombo(const Def& d, int& pick)
{
    if (d.variants.empty()) {
        ImGui::TextDisabled("고정");
        return;
    }
    if (pick >= static_cast<int>(d.variants.size())) // 重读词条表后档位变少了
        pick = static_cast<int>(d.variants.size()) - 1;
    const char* preview = pick < 0 ? "무작위" : d.variants[pick].value.c_str();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (!ImGui::BeginCombo("##v", preview))
        return;
    if (ImGui::Selectable("무작위 (게임 확률 적용)###随机（按游戏概率）", pick < 0))
        pick = -1;
    for (int i = 0; i < static_cast<int>(d.variants.size()); ++i) {
        char probability[32];
        snprintf(probability, sizeof(probability), " · 확률 %.0f%%", d.variants[i].chance * 100.0);
        const std::string label = d.variants[i].value + probability;
        if (ImGui::Selectable(label.c_str(), pick == i))
            pick = i;
    }
    ImGui::EndCombo();
}

void DrawRow(OptionEditor& editor, OptionsUi& ui, const OptionEditor::Unit& unit, const Def& d, const std::string& reason,
    int owned)
{
    const int count = static_cast<int>(unit.slots.size());
    ImGui::TableNextRow();
    ImGui::PushID(d.id.c_str());
    ImGui::TableNextColumn();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(reason.empty() ? OptionRarity(d.rarity).color : kMuted, "%s", d.name.c_str());
    ImGui::TextDisabled("%s", d.id.c_str());
    ImGui::PopTextWrapPos();
    ImGui::TableNextColumn();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextWrapped("%s", d.effect.c_str());
    HintText(d.notes);
    if (!reason.empty())
        ImGui::TextColored(kRed, "이 유닛의 추첨 대상에서 제외됩니다. %s", reason.c_str());
    if (owned > 0)
        ImGui::TextColored(kAmber, "이미 %d개 보유 중입니다. 더 추가하면 효과가 곱연산으로 중첩됩니다.", owned);
    ImGui::PopTextWrapPos();
    ImGui::TableNextColumn();
    const int last = d.variants.empty() ? -1 : static_cast<int>(d.variants.size()) - 1;
    int& pick = ui.variantPick.try_emplace(d.id, last).first->second;
    VariantCombo(d, pick);
    ImGui::TableNextColumn();
    ImGui::BeginDisabled(editor.Busy());
    if (ImGui::SmallButton(reason.empty() ? "적용###用这个" : "강제 적용###强加")) {
        const bool valid = pick >= 0 && pick < static_cast<int>(d.variants.size());
        editor.SetOption(ui.targetSlot > 0 ? ui.targetSlot : count + 1, d.id, valid ? d.variants[pick].value : "");
    }
    if (!reason.empty() && ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(360.0f * Scale());
        ImGui::TextUnformatted("게임 규칙상 이 유닛은 이 특성을 뽑을 수 없습니다. 강제 적용 시 효과가 없거나 비정상적으로 작동할 수 있습니다.");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::EndDisabled();
    ImGui::PopID();
}

} // namespace

void DrawOptionsBody(OptionEditor& editor, OptionsUi& ui)
{
    const float scale = Scale();
    if (ImGui::SmallButton("특성 목록 새로 고침###重读词条表"))
        editor.ReloadCatalog();
    ErrorText(editor.Error());

    const OptionEditor::Unit& unit = editor.Selected();
    if (!unit.valid) {
        ImGui::TextDisabled("선택한 유닛의 특성을 불러오는 중…");
        return;
    }
    FollowTargetSlot(ui.targetSlot, ui.unitKey, unit.sym, unit.rosterIndex, static_cast<int>(unit.slots.size()));
    DrawSlots(editor, ui, unit);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("기본 동작은 추가입니다. 교체하려면 왼쪽 원을 선택하세요. 전투 중 변경한 능력치는 다음 전투부터 적용됩니다.");
    ImGui::PopTextWrapPos();
    HintText(editor.Message());

    SectionTitle("전체 특성");
    ImGui::SetNextItemWidth(200.0f * scale);
    ImGui::InputTextWithHint("##filter", "이름 / 효과 검색", ui.filter, sizeof(ui.filter));
    ImGui::SameLine();
    TargetSlotHint(ui.targetSlot);
    if (editor.Catalog().empty()) {
        ImGui::TextDisabled("특성 목록을 불러오는 중…");
        return;
    }
    constexpr const char* kOnlyFit = "적용 가능만 보기###只看适用的";
    SameLineIfFits(kOnlyFit);
    ImGui::Checkbox(kOnlyFit, &ui.onlyFit);
    ImGui::SetItemTooltip("병종·등급 제한이나 상호 배타 조건으로 이 유닛이 뽑을 수 없는 특성을 숨깁니다.");

    // 按稀有度分组：传说 → 稀有 → 普通
    ImGui::BeginChild("catalog", ImVec2(0.0f, 380.0f * scale));
    for (const char* rarity : kOptionRarities) {
        int total = 0;
        for (const Def& d : editor.Catalog())
            total += d.rarity == rarity;
        const RarityStyle style = OptionRarity(rarity);
        if (!GroupHeader(style.name, style.color, total, rarity))
            continue;
        if (!ImGui::BeginTable(rarity, 4, kListTable))
            continue;
        ImGui::TableSetupColumn("특성", ImGuiTableColumnFlags_WidthFixed, 142.0f * scale);
        ImGui::TableSetupColumn("효과", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("수치", ImGuiTableColumnFlags_WidthFixed, 110.0f * scale);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 90.0f * scale);
        for (const Def& d : editor.Catalog()) {
            if (d.rarity != rarity)
                continue;
            if (ui.filter[0] && d.name.find(ui.filter) == std::string::npos && d.effect.find(ui.filter) == std::string::npos)
                continue;
            const auto fit = unit.fit.find(d.id);
            static const OptionEditor::Fit kNoInfo;
            const OptionEditor::Fit& f = fit != unit.fit.end() ? fit->second : kNoInfo;
            if (ui.onlyFit && !f.reason.empty())
                continue;
            DrawRow(editor, ui, unit, d, f.reason, f.owned);
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

} // namespace ui
