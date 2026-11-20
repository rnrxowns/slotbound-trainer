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
    for (const TrackedVar& t : ctx.vars) {
        SectionTitle(t.ui.label);
        ImGui::TextDisabled("Lua 변수 %s", t.var.Name());
        if (t.var.Ready())
            ImGui::Text("노드 0x%llX", static_cast<unsigned long long>(t.var.Node()));
        else
            ImGui::TextDisabled("%s", VarStateText(t.var));
        if (!t.var.Log().empty())
            ImGui::TextWrapped("%s", t.var.Log().c_str());
        locating |= t.var.GetState() == LuaVar::State::Locating;
    }
    ImGui::BeginDisabled(!game.Pid() || locating);
    if (ImGui::Button("메모리 위치 다시 검색###relocate"))
        for (TrackedVar& t : ctx.vars)
            t.var.Relocate();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("게임 메모리를 다시 검색합니다. 약 1초가 걸립니다.\n"
                          "메모리 위치가 바뀌면 자동으로 다시 검색합니다.");
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
