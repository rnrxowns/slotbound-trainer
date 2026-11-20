// 「诊断」「设置」两页

#include "pages.h"
#include "widgets.h"

namespace ui {

void DrawDiagnosticsPage(MenuContext& ctx)
{
    const GameProcess& game = ctx.game;
    if (game.Pid())
        ImGui::Text("프로세스 %s · PID %lu · 쓰기 권한 %s", ToUtf8(ctx.cfg.processName).c_str(), game.Pid(),
            game.CanWrite() ? "있음" : "없음");
    else
        ImGui::TextDisabled("게임에 연결되지 않았습니다.");
    ImGui::Text("Lua 브리지 %s", ctx.injected ? "주입됨" : "주입 안 됨");

    bool locating = false;
    bool anyBridge = false;
    bool anyMemory = false;
    for (const TrackedVar& t : ctx.vars) {
        const bool viaBridge = t.var.UsesBridge();
        SectionTitle(t.ui.label);
        ImGui::TextDisabled("Lua 변수 %s", t.var.Name());
        if (game.Pid())
            ImGui::TextDisabled("연결 방식 · %s", VarMethodText(t.var));
        if (t.var.Ready()) {
            ImGui::Text("현재 값 %.0f", t.var.Value());
            if (!viaBridge)
                ImGui::Text("메모리 노드 0x%llX", static_cast<unsigned long long>(t.var.Node()));
        } else {
            ImGui::TextDisabled("%s", VarStateText(t.var));
        }
        if (!t.var.Log().empty())
            ImGui::TextWrapped("%s", t.var.Log().c_str());
        locating |= t.var.GetState() == LuaVar::State::Locating;
        anyBridge |= viaBridge;
        anyMemory |= !viaBridge;
    }

    const char* buttonLabel = "메모리 위치 다시 검색###relocate";
    const char* tooltip = "게임 메모리를 다시 검색합니다. 약 1초가 걸립니다.\n"
                          "메모리 위치가 바뀌면 자동으로 다시 검색합니다.";
    if (anyBridge) {
        buttonLabel = "자원 값 다시 확인###relocate";
        tooltip = anyMemory ? "Lua 자원 값을 다시 요청하고 메모리 위치를 다시 검색합니다."
                            : "Lua 브리지를 통해 자원 값을 다시 요청합니다.";
    }
    ImGui::BeginDisabled(!game.Pid() || locating);
    if (ImGui::Button(buttonLabel))
        for (TrackedVar& t : ctx.vars)
            t.var.Relocate();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", tooltip);
}

void DrawSettingsPage(MenuContext& ctx)
{
    Config& cfg = ctx.cfg;
    KeyBinder("메뉴 열기/닫기", cfg.menuKey);
    KeyBinder("전체 기능 켜기/끄기", cfg.panicKey);
    KeyBinder("오버레이 숨기기/표시", cfg.hideKey);
    KeyBinder("트레이너 종료", cfg.exitKey);
    ToggleSwitch("왼쪽 위 상태 표시줄", &cfg.overlay);
    ImGui::TextDisabled("프로세스 이름 %s (INI 파일에서 변경)", ToUtf8(cfg.processName).c_str());
}

} // namespace ui
