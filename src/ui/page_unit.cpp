// 「单位」页：选中单位的星级、吸收 LV、基础属性、技能冷却 / 触发条件；下面分「词条」「刻印」「觉醒」三个子页

#include <cstdio>

#include "bridge/embedded_dll.h"
#include "bridge/injector.h"
#include "labels.h"
#include "pages.h"
#include "widgets.h"

namespace ui {

namespace {

using Unit = UnitLevelEditor::Unit;

// 换了单位：输入框重置成它的当前值
void SyncInputs(UnitInputs& in, const Unit& unit)
{
    const std::string key = UnitKey(unit.sym, unit.rosterIndex);
    if (key == in.unitKey)
        return;
    in.unitKey = key;
    in.level = unit.absorbLv;
    in.stats[0] = unit.hp;
    in.stats[1] = unit.atk;
    in.stats[2] = unit.aspd;
    in.stats[3] = unit.range;
    in.cooldown = unit.SkillCd();
    for (bool& b : in.paramInit)
        b = false;
}

void DrawHeader(const Unit& unit)
{
    const char* symbol = unit.sym.size() == 1 ? SlotSymbolName(unit.sym.front()) : "?";
    std::string identity = symbol[0] == '?' ? unit.sym : symbol;
    if (!unit.subclass.empty())
        identity += " (" + unit.subclass + ")";
    ImGui::TextWrapped("%s · 희귀도 %s · 편성 %d번", identity.c_str(), GradeName(unit.grade).c_str(), unit.rosterIndex);
    if (unit.locked)
        ImGui::TextColored(kGreen, "선택 중");
    else
        ImGui::TextDisabled("최근에 선택한 유닛 (게임 패널 닫힘)");
    ImGui::TextDisabled("체력 %.0f   공격력 %.1f   공격 속도 %.2f   사거리 %.0f", unit.hp, unit.atk, unit.aspd, unit.range);
}

void DrawRank(UnitLevelEditor& editor, const Unit& unit)
{
    ImGui::AlignTextToFramePadding();
    ImGui::Text("별 %d / %d", unit.rank, unit.maxRank);
    ImGui::BeginDisabled(editor.Busy() || unit.needsClassSelect);
    for (int r = unit.rank + 1; r <= unit.maxRank; ++r) {
        const int idx = r - 2;
        const int need = idx >= 0 && idx < static_cast<int>(unit.thresholds.size()) ? unit.thresholds[idx] : 0;
        char label[48];
        snprintf(label, sizeof(label), "%d성으로 승급###r%d", r, r);
        ImGui::SameLine();
        if (ImGui::Button(label))
            editor.RaiseRank(r);
        ImGui::SetItemTooltip("게임의 운명의 돌 승급 절차를 사용합니다.\n"
                              "흡수 레벨이 %d 미만이면 %d까지 올립니다.\n"
                              "게임에서 직업을 선택하면 승급이 완료됩니다.",
            need, need);
    }
    ImGui::EndDisabled();
    if (unit.rank >= unit.maxRank) {
        ImGui::SameLine();
        ImGui::TextDisabled("이 희귀도의 최대 별에 도달했습니다.");
    }
    if (unit.needsClassSelect)
        ImGui::TextColored(kAmber, "%d성으로 승급하려면 게임에서 직업을 선택하세요.", unit.pendingRank);
}

void DrawAbsorbLevel(UnitLevelEditor& editor, const Unit& unit, UnitInputs& in)
{
    ImGui::AlignTextToFramePadding();
    ImGui::Text("흡수 레벨 %d", unit.absorbLv);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130.0f * Scale());
    ImGui::InputInt("##lv", &in.level, 1, 10);
    if (in.level < 1)
        in.level = 1;
    ImGui::BeginDisabled(editor.Busy());
    ImGui::SameLine();
    if (ImGui::Button("적용###set_level"))
        editor.SetLevel(in.level);
    for (int add : { 1, 5, 10 }) {
        char label[16];
        snprintf(label, sizeof(label), "+%d", add);
        ImGui::SameLine();
        if (ImGui::Button(label))
            editor.SetLevel(unit.absorbLv + add);
    }
    ImGui::EndDisabled();
    std::string gates;
    for (size_t i = 0; i < unit.thresholds.size(); ++i)
        gates += (i ? ", " : "") + std::to_string(i + 2) + "성 레벨 " + std::to_string(unit.thresholds[i]);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("흡수 레벨을 올리거나 내릴 수 있습니다. 레벨이 높을수록 능력치가 오르며 30레벨 이후에는 성장량이 절반으로 줄어듭니다.");
    ImGui::TextDisabled("승급 기준 · %s. 승급하려면 게임에서 직업을 선택해야 합니다.", gates.empty() ? "없음" : gates.c_str());
    ImGui::PopTextWrapPos();
}

// 属性表一行的开头：第 1 列标签，然后停在第 2 列、设好输入框宽度（后面是两列按钮）
void RowLabel(const char* text)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(140.0f * Scale());
}

void DrawStatRows(UnitLevelEditor& editor, const Unit& unit, UnitInputs& in)
{
    struct StatRow {
        const char* key;
        const char* label;
        double current;
        double minValue;
        const char* format;
    };
    const StatRow rows[] = {
        { "hp", "체력", unit.hp, 1.0, "%.0f" },
        { "atk", "공격력", unit.atk, 0.0, "%.1f" },
        { "aspd", "공격 속도", unit.aspd, 0.05, "%.2f" },
        { "distance", "사거리", unit.range, 1.0, "%.0f" },
    };
    for (int i = 0; i < 4; ++i) {
        const StatRow& row = rows[i];
        double& input = in.stats[i];
        ImGui::PushID(row.key);
        char label[64], current[32];
        snprintf(current, sizeof(current), row.format, row.current);
        snprintf(label, sizeof(label), "%s %s", row.label, current);
        RowLabel(label);
        ImGui::InputDouble("##v", &input, 0.0, 0.0, row.format);
        if (input < row.minValue)
            input = row.minValue;
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(editor.Busy());
        if (ImGui::Button("적용###set"))
            editor.SetStat(row.key, input, row.label);
        ImGui::TableNextColumn();
        if (ImGui::Button("×2")) {
            input = row.current * 2.0;
            editor.SetStat(row.key, input, row.label);
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
}

void DrawCooldownRow(UnitLevelEditor& editor, const Unit& unit, UnitInputs& in)
{
    if (!unit.HasActiveCd())
        return;
    constexpr double kMin = UnitLevelEditor::kMinCdSeconds;
    ImGui::PushID("skillcd");
    char label[64];
    snprintf(label, sizeof(label), "스킬 재사용 대기시간 %.2f초%s", unit.SkillCd(), unit.skillCdCustom ? "*" : "");
    RowLabel(label);
    ImGui::InputDouble("##v", &in.cooldown, 0.0, 0.0, "%.2f");
    if (in.cooldown < kMin)
        in.cooldown = kMin;
    ImGui::TableNextColumn();
    ImGui::BeginDisabled(editor.Busy());
    if (ImGui::Button("적용###set"))
        editor.SetSkillCooldown(in.cooldown);
    ImGui::TableNextColumn();
    if (ImGui::Button("최소###minimum")) {
        in.cooldown = kMin;
        editor.SetSkillCooldown(kMin);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!unit.skillCdCustom);
    if (ImGui::Button("기본값###reset")) {
        in.cooldown = unit.NaturalCd();
        editor.SetSkillCooldown(-1.0);
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::PopID();
}

// 被动技能的触发条件
void DrawParamRows(UnitLevelEditor& editor, const Unit& unit, UnitInputs& in)
{
    struct ParamUi {
        const char* key;
        const char* label;
        const char* unitText;
        double scale;      // 显示值 = 游戏值 × scale（概率显示成 %）
        double minValue;   // 显示值的范围
        double maxValue;
        const char* format;
        bool integer;      // 次数取整
    };
    static const ParamUi kParams[3] = {
        { "charges", "발동에 필요한 기본 공격", "회", 1.0, 1.0, 1000.0, "%.0f", true },
        { "chance", "발동 확률", "%", 100.0, 0.0, 100.0, "%.0f", false },
        { "internalCooldown", "내부 재사용 대기시간", "초", 1.0, 0.0, 600.0, "%.2f", false },
    };
    for (const UnitLevelEditor::Param& p : unit.skillParams) {
        int idx = -1;
        for (int i = 0; i < 3; ++i)
            if (p.key == kParams[i].key)
                idx = i;
        if (idx < 0)
            continue;
        const ParamUi& ui = kParams[idx];
        double& input = in.params[idx];
        if (!in.paramInit[idx]) {
            input = p.current * ui.scale;
            in.paramInit[idx] = true;
        }
        const bool custom = p.current != p.base;
        ImGui::PushID(ui.key);
        char cur[32], label[96];
        snprintf(cur, sizeof(cur), ui.format, p.current * ui.scale);
        snprintf(label, sizeof(label), "%s %s%s%s", ui.label, cur, ui.unitText, custom ? "*" : "");
        RowLabel(label);
        ImGui::InputDouble("##v", &input, 0.0, 0.0, ui.format);
        if (input < ui.minValue)
            input = ui.minValue;
        if (input > ui.maxValue)
            input = ui.maxValue;
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(editor.Busy());
        if (ImGui::Button("적용###set")) {
            double v = input / ui.scale;
            if (ui.integer)
                v = static_cast<double>(static_cast<long long>(v + 0.5));
            editor.SetSkillParam(ui.key, v, false, ui.label);
        }
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(!custom);
        if (ImGui::Button("기본값###reset")) {
            input = p.base * ui.scale;
            editor.SetSkillParam(ui.key, 0, true, ui.label);
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ImGui::PopID();
    }
}

void DrawSkillNote(const Unit& unit)
{
    using Kind = UnitLevelEditor::SkillKind;
    const char* name = unit.skillName.c_str();
    ImGui::PushTextWrapPos(0.0f);
    if (unit.HasActiveCd()) {
        ImGui::TextDisabled("스킬 ‘%s’의 기본 재사용 대기시간은 %.2f초입니다(기본 %.1f초에 스킬 가속 특성 적용). 최솟값은 0.1초입니다.%s",
            name, unit.NaturalCd(), unit.skillBaseCd, unit.skillCdCustom ? " *는 변경한 값입니다." : "");
        ImGui::TextDisabled("변경한 값은 유닛에 저장되며 Lua 브리지가 주입된 동안 적용됩니다.");
    } else if (unit.skill == Kind::Passive && !unit.skillParams.empty()) {
        ImGui::TextDisabled("패시브 스킬 ‘%s’의 발동 조건을 변경할 수 있습니다. *는 변경한 값입니다.", name);
        ImGui::TextDisabled("변경한 값은 유닛에 저장되며 Lua 브리지가 주입된 동안 적용됩니다. 전투 중에도 즉시 적용됩니다.");
    } else if (unit.skill == Kind::Passive) {
        ImGui::TextDisabled("패시브 스킬 ‘%s’의 발동 방식(처치, 충전, 부활)은 변경할 수 없습니다.", name);
    } else {
        ImGui::TextDisabled("현재 이 유닛에는 스킬이 없습니다. 일부 스킬은 특정 별에 도달하면 해금됩니다.");
    }
    ImGui::PopTextWrapPos();
}

void DrawSubTabs(LuaPages& pages)
{
    if (!ImGui::BeginTabBar("unitTabs"))
        return;
    if (ImGui::BeginTabItem("특성###options")) {
        pages.shown.options = true;
        DrawOptionsBody(pages.options, pages.optionsUi);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("각인###imprints")) {
        pages.shown.imprints = true;
        DrawImprintsBody(pages.imprints, pages.imprintsUi);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("각성###awakening")) {
        pages.shown.awakening = true;
        DrawAwakeningBody(pages.awakening, pages.awakeningUi);
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

} // namespace

void FollowTargetSlot(int& target, std::string& unitKey, const std::string& sym, int rosterIndex, int count)
{
    const std::string key = UnitKey(sym, rosterIndex);
    if (key != unitKey) {
        unitKey = key;
        target = 0;
    }
    if (target < 0 || target > count)
        target = 0;
}

bool DrawBridgeBar(const MenuContext& ctx, const char* feature)
{
    std::string& injectError = ctx.pages.injectError;
    if (!ctx.game.Pid()) {
        ImGui::TextDisabled("게임에 연결되지 않았습니다.");
        return false;
    }
    if (!ctx.injected) {
        ImGui::TextWrapped("%s 기능을 사용하려면 게임에 slotbound_lua.dll을 주입해야 합니다. "
                           "Lua 브리지는 love.dll의 가져오기 테이블에서 함수 포인터 하나를 교체합니다. "
                           "‘주입 해제’를 누르거나 게임을 종료하면 원래 상태로 복원됩니다.", feature);
        if (ImGui::Button("DLL 주입 (유닛·아이템·슬롯머신 기능 활성화)###inject")) {
            injectError.clear();
            // DLL 内嵌在修改器 exe 里，先释放到 %LOCALAPPDATA%\slotbound-trainer\ 再注入
            const std::wstring dll = bridge::ExtractEmbeddedDll(injectError);
            if (dll.empty() || !bridge::Inject(ctx.game.Pid(), dll, injectError))
                injectError = "DLL 주입 실패 · " + injectError;
        }
        ErrorText(injectError);
        return false;
    }
    ImGui::TextColored(kGreen, "Lua 브리지 주입됨");
    ImGui::SameLine();
    if (ImGui::SmallButton("주입 해제###unload"))
        bridge::Unload();
    return true;
}

void DrawUnitPage(MenuContext& ctx)
{
    if (!DrawBridgeBar(ctx, "유닛 편집"))
        return;
    LuaPages& pages = ctx.pages;
    pages.shown.unit = true;
    UnitLevelEditor& editor = pages.unit;
    ErrorText(editor.Error());

    const Unit& unit = editor.Selected();
    SectionTitle("선택한 유닛");
    if (!unit.valid) {
        ImGui::TextColored(kAmber, "게임에서 아군 유닛을 선택하면 레벨, 특성, 각인을 볼 수 있습니다.");
        return;
    }
    SyncInputs(pages.unitInputs, unit);
    DrawHeader(unit);
    DrawRank(editor, unit);
    DrawAbsorbLevel(editor, unit, pages.unitInputs);

    // 基础属性：unit.stats，每场开战按它生成战场单位
    SectionTitle("기본 능력치");
    if (ImGui::BeginTable("stats", 4, ImGuiTableFlags_SizingFixedFit)) {
        DrawStatRows(editor, unit, pages.unitInputs);
        DrawCooldownRow(editor, unit, pages.unitInputs);
        DrawParamRows(editor, unit, pages.unitInputs);
        ImGui::EndTable();
    }
    DrawSkillNote(unit);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("변경한 기본 능력치는 저장되며 전투 중에도 즉시 반영됩니다. 특성, 각인, 아이템의 보너스는 그대로 적용됩니다. "
                        "공격 속도는 초당 공격 횟수입니다.");
    ImGui::PopTextWrapPos();
    HintText(editor.Message());

    ImGui::Spacing();
    DrawSubTabs(pages);
}

} // namespace ui
