// 「资源」页（金币 / 旋转次数 / 水晶：外部读写，不注入）和左上角状态栏

#include <cstdio>

#include "pages.h"
#include "widgets.h"

namespace ui {

namespace {

constexpr const char* kWriteFailed = "값을 변경하지 못했습니다. 메모리 위치를 다시 찾는 중입니다.";

void DrawStatusVar(const Config& cfg, const VarConfig& vc, const LuaVar& var, const char* label)
{
    if (!var.Ready()) {
        ImGui::TextDisabled("%s · %s", label, VarStateText(var));
        return;
    }
    ImGui::Text("%s %.0f", label, var.Value());
    if (cfg.enabled && vc.lock) {
        ImGui::SameLine();
        ImGui::TextColored(kGreen, "  고정값 %d", vc.lockValue);
    }
}

// 输入框：不许负数
void NonNegativeInput(const char* label, int& value, const VarUi& ui)
{
    ImGui::SetNextItemWidth(160.0f * Scale());
    ImGui::InputInt(label, &value, ui.step, ui.stepFast);
    if (value < 0)
        value = 0;
}

// 一个数值的区块：当前值、改成 / 加、锁定、快捷键
void DrawVarSection(const Config& cfg, VarConfig& vc, LuaVar& var, VarUi& ui)
{
    ImGui::PushID(ui.label);
    SectionTitle(ui.label);
    if (!var.Ready())
        ImGui::TextDisabled("%s", VarStateText(var));
    else
        ImGui::Text("현재 값 %.0f", var.Value());

    ImGui::BeginDisabled(!var.Ready() || !cfg.enabled);
    NonNegativeInput("##set", ui.setValue, ui);
    ImGui::SameLine();
    if (ImGui::Button("값 적용###set_value"))
        ui.message = var.Set(ui.setValue) ? "값을 변경했습니다." : kWriteFailed;
    for (int add : ui.quickAdd) {
        ImGui::SameLine();
        char label[24];
        snprintf(label, sizeof(label), "+%d", add);
        if (ImGui::Button(label))
            ui.message = var.Set(var.Value() + add) ? "값을 늘렸습니다." : kWriteFailed;
    }
    ImGui::EndDisabled();
    HintText(ui.message);

    char text[64];
    snprintf(text, sizeof(text), "%s 고정", ui.label);
    ToggleSwitch(text, &vc.lock, "자원을 사용하면 고정값으로 즉시 복구합니다. 끄면 현재 값에서 차감됩니다.");
    NonNegativeInput("고정값###lock_value", vc.lockValue, ui);
    KeyBinder("고정 단축키", vc.lockKey, true);
    NonNegativeInput("단축키로 늘릴 양###add_amount", vc.addAmount, ui);
    KeyBinder("증가 단축키", vc.addKey, true);
    ImGui::PopID();
}

} // namespace

const char* VarStateText(const LuaVar& var)
{
    switch (var.GetState()) {
    case LuaVar::State::NotConnected: return "게임에 연결되지 않았습니다";
    case LuaVar::State::Locating: return "값을 찾는 중…";
    case LuaVar::State::NotFound: return "검색 실패 (메뉴 또는 로딩 중일 수 있음, 5초 후 재검색)";
    case LuaVar::State::Ready: return "";
    }
    return "";
}

void DrawStatusBar(const Config& cfg, const GameProcess& game, std::span<const TrackedVar> vars)
{
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav;
    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f));
    ImGui::SetNextWindowBgAlpha(0.65f);
    ImGui::Begin("##status", nullptr, flags);

    ImGui::TextUnformatted("Slotbound 트레이너");
    ImGui::SameLine();
    ImGui::TextColored(cfg.enabled ? kGreen : kRed, cfg.enabled ? "[켜짐]" : "[꺼짐]");

    if (!game.Pid()) {
        ImGui::TextDisabled("%s 실행 대기 중…", ToUtf8(cfg.processName).c_str());
    } else {
        for (const TrackedVar& t : vars)
            DrawStatusVar(cfg, cfg.*t.cfg, t.var, t.ui.label);
    }

    ImGui::TextDisabled("%s 메뉴  %s 전체 켜기/끄기  %s 숨기기  %s 종료", KeyName(cfg.menuKey).c_str(), KeyName(cfg.panicKey).c_str(),
        KeyName(cfg.hideKey).c_str(), KeyName(cfg.exitKey).c_str());
    ImGui::End();
}

void DrawResourcesPage(Config& cfg, std::span<TrackedVar> vars)
{
    for (TrackedVar& t : vars)
        DrawVarSection(cfg, cfg.*t.cfg, t.var, t.ui);
    ImGui::Spacing();
    if (!cfg.enabled)
        ImGui::TextColored(kAmber, "전체 기능이 꺼져 있어 값을 변경하거나 고정할 수 없습니다.");
    ImGui::TextDisabled("트레이너를 종료해도 변경한 값은 유지됩니다.");
}

} // namespace ui
