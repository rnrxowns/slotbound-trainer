#include "labels.h"

#include <utility>

#include "widgets.h"

namespace ui {

std::string GradeName(const std::string& grade)
{
    static const std::pair<const char*, const char*> kGrades[] = {
        { "normal", "일반" }, { "advanced", "고급" }, { "rare", "희귀" },
        { "special", "특수" }, { "legendary", "전설" }, { "prism", "프리즘" },
    };
    for (const auto& [key, name] : kGrades)
        if (grade == key)
            return name;
    return grade;
}

RarityStyle OptionRarity(const std::string& rarity)
{
    if (rarity == "legend")
        return { "전설", ImVec4(1.0f, 0.78f, 0.3f, 1.0f) };
    if (rarity == "rare")
        return { "희귀", ImVec4(0.45f, 0.7f, 1.0f, 1.0f) };
    return { "일반", kText };
}

RarityStyle ItemRarity(const std::string& rarity)
{
    if (rarity == "L")
        return { "전설", ImVec4(1.0f, 0.7f, 0.3f, 1.0f) };
    if (rarity == "R")
        return { "희귀", ImVec4(0.45f, 0.7f, 1.0f, 1.0f) };
    if (rarity == "U")
        return { "고급", ImVec4(0.45f, 0.85f, 0.45f, 1.0f) };
    if (rarity == "C")
        return { "일반", kText };
    return { "?", kText };
}

const char* SlotSymbolName(char c)
{
    switch (c) {
    case 'W': return "전사";
    case 'A': return "궁수";
    case 'S': return "창병";
    case 'M': return "마법사";
    case 'H': return "기병";
    case 'D': return "결투가";
    case 'J': return "광대";
    case 'B': return "빈칸";
    }
    return "?";
}

} // namespace ui
