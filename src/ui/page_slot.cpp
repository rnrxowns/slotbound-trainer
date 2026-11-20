// 「老虎机」页：大奖模式

#include "labels.h"
#include "pages.h"
#include "widgets.h"

namespace ui {

namespace {

void DrawSymbolPicker(Config& cfg)
{
    ImGui::TextDisabled("병종 선택");
    // J = 小丑：万能符号；整条线都是小丑时出「小丑」单位，9 格全小丑 = 8 条小丑线 + 大奖
    static const char* kSymbols[] = { "W", "A", "S", "M", "H", "D", "J", "random" };
    for (const char* s : kSymbols) {
        const bool random = s[1] != '\0';
        const char* label = random ? "매회 무작위" : SlotSymbolName(s[0]);
        SameLineIfFits(label);
        ImGui::PushID(s);
        if (ImGui::RadioButton(label, cfg.slotSymbol == s))
            cfg.slotSymbol = s;
        if (s[0] == 'J')
            ImGui::SetItemTooltip("광대는 어떤 기호로도 사용할 수 있습니다. 한 줄이 모두 광대면 광대 유닛이 나옵니다.\n"
                                  "9칸이 모두 광대면 광대 8줄이 완성되어 잭팟이 됩니다.");
        if (random)
            ImGui::SetItemTooltip("회전할 때마다 일반 병종 6개 중 하나를 무작위로 선택합니다. 광대는 제외됩니다.");
        ImGui::PopID();
    }
}

void DrawStatus(const Config& cfg, const SlotControl& slot)
{
    if (!slot.Hooked())
        ImGui::TextDisabled("슬롯머신에 연결하는 중… 메인 메뉴에 있다면 게임을 시작하세요.");
    else if (!cfg.enabled)
        ImGui::TextColored(kAmber, "전체 기능이 꺼져 있어 잭팟 모드가 적용되지 않습니다.");
    else if (cfg.slotJackpot)
        ImGui::TextColored(kGreen, "다음 회전에 잭팟이 나옵니다. 이번 게임 적용 %d회", slot.Hits());
    else
        ImGui::TextDisabled("잭팟 모드가 꺼져 있습니다.");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("레버를 당기는 순간 결과가 결정됩니다. 다음 회전부터 적용되며, 운명의 돌로 돌릴 때도 잭팟이 나옵니다.");
    ImGui::PopTextWrapPos();
}

void DrawGrid(const std::string& g)
{
    if (g.size() != 9 || !ImGui::BeginTable("grid", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TextDisabled("—");
        return;
    }
    for (int r = 0; r < 3; ++r) {
        ImGui::TableNextRow();
        for (int c = 0; c < 3; ++c) {
            ImGui::TableNextColumn();
            ImGui::Dummy(ImVec2(82.0f * Scale(), 0.0f));
            ImGui::TextUnformatted(SlotSymbolName(g[r * 3 + c]));
        }
    }
    ImGui::EndTable();
}

} // namespace

void DrawSlotPage(MenuContext& ctx)
{
    if (!DrawBridgeBar(ctx, "슬롯머신 조작"))
        return;
    const SlotControl& slot = ctx.pages.slot;
    ErrorText(slot.Error());

    SectionTitle("잭팟 모드");
    ToggleSwitch("매회 잭팟", &ctx.cfg.slotJackpot,
        "켜면 회전할 때마다 9칸에 같은 병종이 나옵니다. 끄면 게임의 기본 방식으로 돌아갑니다.");
    DrawSymbolPicker(ctx.cfg);
    DrawStatus(ctx.cfg, slot);

    SectionTitle("현재 결과");
    DrawGrid(slot.Grid());
}

} // namespace ui
