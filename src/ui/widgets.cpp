#include "widgets.h"

#include <windows.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>

namespace ui {

std::string ToUtf8(const std::wstring& s)
{
    if (s.empty())
        return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len, nullptr, nullptr);
    return out;
}

std::string KeyName(int vk)
{
    switch (vk) {
    case 0: return "미지정";
    case VK_LBUTTON: return "마우스 왼쪽";
    case VK_RBUTTON: return "마우스 오른쪽";
    case VK_MBUTTON: return "마우스 가운데";
    case VK_XBUTTON1: return "마우스 측면 1";
    case VK_XBUTTON2: return "마우스 측면 2";
    case VK_SHIFT: return "Shift";
    case VK_CONTROL: return "Ctrl";
    case VK_MENU: return "Alt";
    case VK_CAPITAL: return "CapsLock";
    case VK_INSERT: return "Insert";
    case VK_DELETE: return "Delete";
    case VK_HOME: return "Home";
    case VK_END: return "End";
    case VK_PRIOR: return "PageUp";
    case VK_NEXT: return "PageDown";
    }
    char buf[32];
    if (vk >= VK_F1 && vk <= VK_F24)
        snprintf(buf, sizeof(buf), "F%d", vk - VK_F1 + 1);
    else if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z'))
        snprintf(buf, sizeof(buf), "%c", vk);
    else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
        snprintf(buf, sizeof(buf), "숫자패드 %d", vk - VK_NUMPAD0);
    else
        snprintf(buf, sizeof(buf), "0x%02X", vk);
    return buf;
}

bool ToggleSwitch(const char* label, bool* v, const char* tooltip)
{
    const float scale = Scale();
    const float height = ImGui::GetFrameHeight() * 0.78f;
    const float width = height * 1.9f;
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const float labelWidth = std::max(200.0f * scale, textSize.x + ImGui::GetStyle().ItemSpacing.x);
    ImGui::PushID(label);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float rowHeight = ImGui::GetFrameHeight();
    const bool clicked = ImGui::InvisibleButton("##toggle", ImVec2(labelWidth + width, rowHeight));
    if (clicked)
        *v = !*v;
    const bool hovered = ImGui::IsItemHovered();
    if (tooltip)
        ImGui::SetItemTooltip("%s", tooltip);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec4* colors = ImGui::GetStyle().Colors;
    draw->AddText(ImVec2(start.x, start.y + (rowHeight - textSize.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), label);

    const ImVec2 p0(start.x + labelWidth, start.y + (rowHeight - height) * 0.5f);
    const ImVec2 p1(p0.x + width, p0.y + height);
    const ImVec4 off = hovered ? colors[ImGuiCol_FrameBgHovered] : colors[ImGuiCol_FrameBg];
    const ImVec4 on = hovered ? colors[ImGuiCol_SliderGrabActive] : colors[ImGuiCol_CheckMark];
    draw->AddRectFilled(p0, p1, ImGui::GetColorU32(*v ? on : off), height * 0.5f);
    const float r = height * 0.5f - 2.0f * scale;
    const float knobX = *v ? p1.x - height * 0.5f : p0.x + height * 0.5f;
    draw->AddCircleFilled(ImVec2(knobX, p0.y + height * 0.5f), r, IM_COL32(245, 247, 250, 255));
    ImGui::PopID();
    return clicked;
}

void SectionTitle(const char* text)
{
    ImGui::Spacing();
    ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_CheckMark], "%s", text);
    ImGui::Separator();
}

void KeyBinder(const char* label, int& vk, bool optional)
{
    static const char* waitingFor = nullptr;
    static bool waitRelease = false;           // 点按钮的那下左键还没松开，先不收键
    static const char* suppressClick = nullptr; // 用左键绑定后，松开那一下会被按钮当成点击，忽略掉
    const bool leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    ImGui::PushID(label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(std::max(200.0f * Scale(),
        ImGui::CalcTextSize(label).x + ImGui::GetStyle().ItemSpacing.x));

    if (waitingFor == label) {
        ImGui::Button("키를 누르세요 (Esc로 취소)");
        if (waitRelease && !leftDown)
            waitRelease = false;
        for (int key = VK_LBUTTON; key <= 0xFE && !waitRelease; ++key) {
            if (!(GetAsyncKeyState(key) & 0x8000))
                continue;
            if (key != VK_ESCAPE)
                vk = key;
            waitingFor = nullptr;
            suppressClick = label;
            break;
        }
    } else if (ImGui::Button(KeyName(vk).c_str()) && suppressClick != label) {
        waitingFor = label;
        waitRelease = true;
    }
    if (suppressClick == label && !leftDown)
        suppressClick = nullptr;
    if (optional && vk && waitingFor != label) {
        ImGui::SameLine();
        if (ImGui::SmallButton("지우기"))
            vk = 0;
    }
    ImGui::PopID();
}

bool LevelCombo(const char* id, int& level, int maxLevel)
{
    char preview[24];
    snprintf(preview, sizeof(preview), "%d레벨", level);
    bool changed = false;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo(id, preview)) {
        for (int i = 1; i <= maxLevel; ++i) {
            char label[24];
            snprintf(label, sizeof(label), "%d레벨", i);
            if (ImGui::Selectable(label, level == i) && level != i) {
                level = i;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

void SameLineIfFits(const char* nextLabel)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const float width = ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(nextLabel, nullptr, true).x;
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    ImGui::SameLine();
    if (ImGui::GetCursorScreenPos().x + width > right)
        ImGui::NewLine();
}

void ErrorText(const std::string& text)
{
    if (!text.empty()) {
        ImGui::PushTextWrapPos();
        ImGui::TextColored(kRed, "%s", text.c_str());
        ImGui::PopTextWrapPos();
    }
}

void HintText(const std::string& text)
{
    if (!text.empty()) {
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled("%s", text.c_str());
        ImGui::PopTextWrapPos();
    }
}

const std::string& LevelText(const std::vector<std::string>& levels, int level)
{
    static const std::string kEmpty;
    return level >= 1 && level <= static_cast<int>(levels.size()) ? levels[level - 1] : kEmpty;
}

void LevelsTooltip(const std::vector<std::string>& levels, const char* const* levelNames)
{
    if (levels.empty() || !ImGui::IsItemHovered())
        return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(420.0f * Scale());
    for (int i = 1; i <= static_cast<int>(levels.size()); ++i) {
        if (levelNames)
            ImGui::Text("%s단계:%s", levelNames[i], levels[i - 1].c_str());
        else
            ImGui::Text("%d단계:%s", i, levels[i - 1].c_str());
    }
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

bool GroupHeader(const char* name, const ImVec4& color, int count, const char* id)
{
    char header[96];
    snprintf(header, sizeof(header), "%s (%d)###%s", name, count, id);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    const bool open = ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor();
    return open;
}

void SlotPicker(int& target, int value, int number)
{
    ImGui::RadioButton("##slot", &target, value);
    ImGui::SameLine();
    ImGui::Text("%d", number);
}

void TargetSlotHint(int target)
{
    if (target > 0)
        ImGui::TextColored(kAmber, "적용하면 %d번 슬롯을 교체합니다.", target);
    else
        ImGui::TextDisabled("적용하면 새 슬롯에 추가합니다.");
}

} // namespace ui
