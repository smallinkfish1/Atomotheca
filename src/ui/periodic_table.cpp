// src/ui/periodic_table.cpp
// 第 16 天：元素周期表面板实现。
//
// 这层只做三件事：把元素按 (period, group) 摆到网格里、响应点击、显示详情。
// 电子排布、离子推算一律调 element.h 的接口，不在这里重新算一遍 ——
// 同一份化学逻辑存在两处，早晚会不一致。

#include "ui/periodic_table.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

namespace ui
{
namespace
{

// ---------- 网格尺寸 ----------
// 18 族是周期表的固定列数；行数取数据里出现过的最大周期（当前是 4）。
// 不写死 7 行：没有数据的周期整行空地摆在那里，只会让人以为数据坏了。
constexpr int   kGroups   = 18;
constexpr float kCellW    = 32.0f;
constexpr float kCellH    = 34.0f;
constexpr float kCellGap  = 2.0f;
constexpr float kCellStep = kCellW + kCellGap;
constexpr float kLabelCol = 24.0f;   // 左侧「周期号」那一列的宽度
constexpr float kHeaderH  = 14.0f;   // 顶部「族号」那一行的最小高度，实际取字体行高与它的较大者

// 这个 (周期, 族) 在真实周期表里是否真的存在元素。
//
// 为什么要区分「不存在」和「没收录」：
//   第 1 周期只有 1、18 族；第 2、3 周期没有过渡金属（3–12 族本来就不存在）；
//   第 4 周期起 1–18 族完整。
//   「不存在」是化学结构决定的天花板，永远不会有数据；
//   「没收录」只是数据库还没写 —— 现在 1–36 号已补齐，第 4 周期没有空格，
//   但以后往 5–7 周期扩的时候（f 区、镧系锕系）这个分支还会用到。
bool cellExists(int period, int group)
{
    if (period <= 0 || group <= 0 || group > kGroups) return false;
    if (period == 1) return group == 1 || group == 18;
    if (period <= 3) return group <= 2 || group >= 13;
    return true;
}

// 按 (period, group) 找元素。
// 线性扫描比维护一张二维索引表更简单，也不会因为某格缺数据而越界 ——
// 缺格就是返回 nullptr。36 个元素 × 72 格的开销在 60 fps 下可以忽略。
const orbital::Element* elementAt(const orbital::ElementDatabase& db,
                                  int period, int group)
{
    for (const orbital::Element& e : db.elements())
    {
        if (e.period == period && e.group == group)
            return &e;
    }
    return nullptr;
}

int maxPeriod(const orbital::ElementDatabase& db)
{
    int period = 0;
    for (const orbital::Element& e : db.elements())
        period = std::max(period, e.period);
    return period;
}

// 离子按钮的文字：H+ / Fe2+ / O2-
// 和 orbital::Ion::label() 规则一致，但这里不构造整个离子排布 ——
// 按钮只需要一个字符串，没必要为它算一遍电子。
std::string ionLabel(const orbital::Element& e, int charge)
{
    std::string text = e.symbol;
    const int absCharge = charge < 0 ? -charge : charge;
    if (absCharge > 1)
        text += std::to_string(absCharge);
    text += (charge > 0) ? '+' : '-';
    return text;
}

// 底色是 CPK 配色，深浅不一（H 接近白色、C 是深灰）。
// 文字颜色按亮度反转，否则白底白字会看不见。
ImU32 inkOn(const std::array<float, 3>& c)
{
    const float lum = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
    return (lum > 0.55f) ? IM_COL32(20, 20, 26, 255)
                         : IM_COL32(245, 245, 250, 255);
}

} // namespace

// ============================================================
//  窗口
// ============================================================
void drawPeriodicTable(const orbital::ElementDatabase& db, PeriodicTableState& state)
{
    if (!state.visible) return;

    // 窗口宽度 = 周期号列 + 18 格 + 左右内边距
    const float winW = kLabelCol + kGroups * kCellStep + 16.0f;

    // 默认贴着右边放，避开左侧 340px 的控制面板。
    // 屏幕太窄时（比如 Web 上的小窗口）退回到左上角，至少还能拖动。
    const ImGuiIO& io = ImGui::GetIO();
    float posX = io.DisplaySize.x - winW - 16.0f;
    if (posX < 360.0f) posX = 10.0f;

    ImGui::SetNextWindowPos(ImVec2(posX, 60.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(winW, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.92f);

    if (!ImGui::Begin("Periodic Table", &state.visible))
    {
        ImGui::End();
        return;
    }

    if (db.empty())
    {
        ImGui::TextDisabled("Element database not loaded.");
        ImGui::End();
        return;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int rows = maxPeriod(db);

    // ---------- 族号表头 ----------
    // 每格用 Dummy 占位、再在矩形正中手写数字，而不是用 Text：
    // Text 的对齐受字体行高影响，和下方 32px 的格子算不到一块去；
    // 而 AddText 用同一套格坐标，表头数字必然和列对齐。
    //
    // 行首那个 1px 宽的 Dummy 是必须的：SameLine() 要有一个「上一格」才能定位。
    //
    // 高度不直接用 kHeaderH：14px 是默认字体下的下限，换字体或 HiDPI 缩放后
    // 字会比格子高、被挤出顶部。取「常量」与「实际行高」的较大者才算稳。
    const float headerH = std::max(kHeaderH, ImGui::GetTextLineHeight());

    ImGui::Dummy(ImVec2(1.0f, headerH));
    const ImU32 headerInk = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    for (int g = 1; g <= kGroups; ++g)
    {
        if (g == 1) ImGui::SameLine(kLabelCol, 0.0f);
        else        ImGui::SameLine(0.0f, kCellGap);

        ImGui::Dummy(ImVec2(kCellW, headerH));
        const ImVec2 a = ImGui::GetItemRectMin();
        const ImVec2 b = ImGui::GetItemRectMax();

        char gText[4];
        std::snprintf(gText, sizeof(gText), "%d", g);
        const ImVec2 ts = ImGui::CalcTextSize(gText);
        dl->AddText(ImVec2((a.x + b.x - ts.x) * 0.5f, a.y + (headerH - ts.y) * 0.5f),
                    headerInk, gText);
    }

    // ---------- 网格 ----------
    for (int p = 1; p <= rows; ++p)
    {
        ImGui::TextDisabled("%d", p);

        for (int g = 1; g <= kGroups; ++g)
        {
            // 第一格从固定列宽开始，各行才能严格对齐
            if (g == 1) ImGui::SameLine(kLabelCol, 0.0f);
            else        ImGui::SameLine(0.0f, kCellGap);

            const orbital::Element* el     = elementAt(db, p, g);
            const bool              exists = cellExists(p, g);

            if (el == nullptr)
            {
                // 位置上根本没有元素：留白，只占布局宽度
                if (!exists)
                {
                    ImGui::Dummy(ImVec2(kCellW, kCellH));
                    continue;
                }

                // 真实存在但没收录：淡框占位 + 悬停说明
                ImGui::PushID(p * 100 + g);
                ImGui::InvisibleButton("##noData", ImVec2(kCellW, kCellH));
                const ImVec2 a = ImGui::GetItemRectMin();
                const ImVec2 b = ImGui::GetItemRectMax();
                dl->AddRect(a, b, IM_COL32(130, 135, 150, 70), 3.0f);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("period %d, group %d\nnot in database", p, g);
                ImGui::PopID();
                continue;
            }

            ImGui::PushID(el->atomicNumber);
            ImGui::InvisibleButton("##cell", ImVec2(kCellW, kCellH));

            const ImVec2 a      = ImGui::GetItemRectMin();
            const ImVec2 b      = ImGui::GetItemRectMax();
            const bool   hovered = ImGui::IsItemHovered();
            const bool   isSel   = (el->atomicNumber == state.selectedZ);

            if (ImGui::IsItemClicked())
            {
                state.selectedZ = el->atomicNumber;
                state.ionIndex  = 0;   // 换元素时回到中性原子
            }

            // 底色：CPK 配色，选中时最实、悬停次之、平时最淡
            const std::array<float, 3>& c = el->cpkColor;
            const float alpha = isSel ? 0.92f : (hovered ? 0.72f : 0.52f);
            dl->AddRectFilled(a, b,
                              ImGui::GetColorU32(ImVec4(c[0], c[1], c[2], alpha)),
                              3.0f);

            const ImU32 border = isSel
                  ? IM_COL32(255, 214, 90, 255)
                  : ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, hovered ? 0.55f : 0.16f));
            dl->AddRect(a, b, border, 3.0f, 0, isSel ? 2.0f : 1.0f);

            const ImU32 ink = inkOn(c);

            // 第一行：原子序数（小数字，靠左上）
            char zText[8];
            std::snprintf(zText, sizeof(zText), "%d", el->atomicNumber);
            dl->AddText(ImVec2(a.x + 3.0f, a.y + 1.0f), ink, zText);

            // 第二行：元素符号，水平居中
            const ImVec2 ts = ImGui::CalcTextSize(el->symbol.c_str());
            dl->AddText(ImVec2(a.x + (kCellW - ts.x) * 0.5f, a.y + 15.0f), ink,
                        el->symbol.c_str());

            if (hovered)
                ImGui::SetTooltip("%s  %s\nZ = %d   period %d   group %d",
                                  el->symbol.c_str(), el->name.c_str(),
                                  el->atomicNumber, el->period, el->group);

            ImGui::PopID();
        }
    }

    // ---------- 收录统计 ----------
    // 一行小字，用来确认「数据补到哪了」：第 4 周期应当是 36 个元素、18 格满。
    ImGui::Spacing();
    ImGui::TextDisabled("%d elements  ·  periods 1-%d",
                        static_cast<int>(db.elements().size()), rows);

    // ---------- 选中元素详情 ----------
    ImGui::Separator();

    const orbital::Element* sel = db.findByZ(state.selectedZ);
    if (sel == nullptr)                    // 数据文件换过、Z 失效时兜底
        sel = &db.elements().front();

    ImGui::TextColored(ImVec4(0.62f, 0.85f, 1.0f, 1.0f), "%s   %s",
                       sel->symbol.c_str(), sel->name.c_str());
    ImGui::Text("Z %d    mass %.3f", sel->atomicNumber, sel->atomicMass);
    ImGui::Text("period %d    group %d    %s", sel->period, sel->group,
                sel->category.c_str());
    ImGui::Spacing();

    // 离子按钮：第 0 个是中性的（Atom），后面按 JSON 里 commonIonCharges 的顺序
    const int ionCount = static_cast<int>(sel->commonIonCharges.size());
    if (state.ionIndex < 0 || state.ionIndex > ionCount)
        state.ionIndex = 0;

    for (int i = 0; i <= ionCount; ++i)
    {
        if (i > 0) ImGui::SameLine();
        const std::string label = (i == 0) ? std::string("Atom")
                                           : ionLabel(*sel, sel->commonIonCharges[i - 1]);
        ImGui::RadioButton(label.c_str(), &state.ionIndex, i);
    }

    ImGui::Spacing();

    if (state.ionIndex == 0)
    {
        ImGui::Text("electrons %d", sel->electronCount());
        ImGui::TextWrapped("%s", sel->configString().c_str());

        // 填充次序和书写顺序不同时才多打一行，否则读者会以为算错了
        const std::string fill = orbital::formatConfigOrdered(sel->energyOrderedSubshells());
        if (fill != sel->configString())
            ImGui::TextDisabled("fill order  %s", fill.c_str());
    }
    else
    {
        // makeIon 每次返回一个新的 vector。这里只在「选中离子」时算一次，
        // 量级只有几十字节，属于 UI 面板的正常开销；
        // 铁律 3 禁止动态分配针对的是 3D 渲染循环，不是这种一次性小对象。
        const orbital::Ion ion = sel->makeIon(sel->commonIonCharges[state.ionIndex - 1]);
        ImGui::Text("electrons %d    charge %+d", ion.electronCount(), ion.charge);
        ImGui::TextWrapped("%s", ion.configString().c_str());
    }

    ImGui::End();
}

// ============================================================
//  ASCII 版（--dump-table）
// ============================================================
void printPeriodicTableAscii(const orbital::ElementDatabase& db)
{
    if (db.empty())
    {
        std::printf("元素数据库为空\n");
        return;
    }

    const int rows = maxPeriod(db);

    // 每格 4 字符宽：有元素印符号、真存在但没收录印 '.'、不存在印空格。
    // 表头是族号，行首是周期号。
    std::printf("\n    ");
    for (int g = 1; g <= kGroups; ++g)
        std::printf(" %-2d ", g);
    std::printf("\n");

    for (int p = 1; p <= rows; ++p)
    {
        std::printf("%-2d  ", p);

        for (int g = 1; g <= kGroups; ++g)
        {
            const orbital::Element* el = elementAt(db, p, g);
            if (el != nullptr)
                std::printf(" %-2s ", el->symbol.c_str());
            else if (cellExists(p, g))
                std::printf(" %-2s ", ".");
            else
                std::printf("    ");
        }
        std::printf("\n");
    }

    std::printf("\n图例: 符号=已收录   . =真实存在但未收录   空白=该位置没有元素\n");
}

} // namespace ui
