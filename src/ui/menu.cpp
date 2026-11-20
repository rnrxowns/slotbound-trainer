// 主菜单窗口：标签页切换 + 底部的配置文件栏；以及 Lua 桥各页的刷新调度

#include "menu.h"

#include <algorithm>

#include "pages.h"
#include "widgets.h"

namespace ui {

void LuaPages::Update(bool menuOpen, bool injected, const Config& cfg)
{
    // 메뉴가 닫혀 있으면 슬롯머신을 제외한 게임 데이터를 읽지 않습니다.
    const bool ready = menuOpen && injected;
    unit.Update(ready && shown.unit);
    options.Update(ready && shown.unit && shown.options);
    imprints.Update(ready && shown.unit && shown.imprints);
    awakening.Update(ready && shown.unit && shown.awakening);
    items.Update(ready && shown.items);
    // 메뉴가 닫혀 있어도 슬롯머신 상태를 확인합니다.
    slot.Update(injected, cfg.enabled && cfg.slotJackpot, cfg.slotSymbol);
}

bool DrawMenu(MenuContext& ctx)
{
    bool keepRunning = true;
    const float scale = Scale();
    ctx.pages.shown = {};
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float margin = 12.0f * scale;
    const float width = std::clamp(display.x - 2.0f * margin, 1.0f, 740.0f * scale);
    const float left = std::max(margin, std::min(80.0f * scale, display.x - width - margin));
    const float top = std::clamp(display.y * 0.18f, margin, 160.0f * scale);
    ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(left, top), ImGuiCond_FirstUseEver);
    ImGui::Begin("Slotbound 트레이너", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);

    ToggleSwitch("전체 기능", &ctx.cfg.enabled, "끄면 값 잠금이 해제되며 수정 버튼을 사용할 수 없습니다.");

    struct Tab {
        const char* name;
        void (*draw)(MenuContext&);
    };
    static const Tab kTabs[] = {
        { "자원", [](MenuContext& c) { DrawResourcesPage(c.cfg, c.vars); } },
        { "유닛", DrawUnitPage },
        { "아이템", DrawItemsPage },
        { "슬롯머신", DrawSlotPage },
        { "진단", DrawDiagnosticsPage },
        { "설정", DrawSettingsPage },
    };
    if (ImGui::BeginTabBar("tabs")) {
        for (const Tab& tab : kTabs) {
            if (ImGui::BeginTabItem(tab.name)) {
                tab.draw(ctx);
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    ImGui::Separator();
    ImGui::PushTextWrapPos();
    ImGui::TextDisabled("%s%s", ToUtf8(ctx.cfgPath).c_str(), ctx.cfgFileFound ? "" : " (설정 파일 없음. 기본값 사용)");
    ImGui::TextDisabled("변경 사항은 자동으로 저장됩니다.");
    ImGui::PopTextWrapPos();
    if (ImGui::Button("설정 다시 불러오기"))
        ctx.cfgFileFound = LoadConfig(ctx.cfgPath, ctx.cfg);
    ImGui::SameLine();
    if (ImGui::Button("트레이너 종료"))
        keepRunning = false;

    ImGui::End();
    return keepRunning;
}

} // namespace ui
