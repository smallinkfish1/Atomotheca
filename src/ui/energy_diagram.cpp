// src/ui/energy_diagram.cpp
// 第 17 天：2D 能级图实现。
//
// 三件事：把亚层按能量排成一列、在每个亚层里画轨道方框、按洪特规则往方框里放箭头。
// 电子排布一律来自 element.h —— 这里不重新算一遍化学，只负责「怎么画」。

#include "ui/energy_diagram.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>

namespace ui
{
namespace
{

// ---------- 尺寸 ----------
constexpr float kRowH     = 30.0f;                    // 一行（一个亚层）的高度
constexpr float kBoxW     = 16.0f;                    // 一个轨道方框
constexpr float kBoxGap   = 4.0f;
constexpr float kLabelW   = 38.0f;                    // 左侧 "3d" 标签列
constexpr float kPadX     = 12.0f;
constexpr int   kWidestL  = 2;                        // d 层 5 个框是最宽的
constexpr int   kMaxBoxes = 2 * kWidestL + 1;

const ImU32 kSpinUp   = IM_COL32(64, 132, 220, 255);  // ↑ 自旋 +1/2
const ImU32 kSpinDown = IM_COL32(224, 112, 64, 255);  // ↓ 自旋 -1/2

// 轨道数：s 1 个、p 3 个、d 5 个、f 7 个
int orbitalCount(int l) { return 2 * l + 1; }

// 洪特规则：电子先每个轨道占一个，剩下的才成对。
// 所以只要知道「有几对」，单占数自然就出来了 —— 不用逐个轨道模拟。
//   3d6 → 3 对，0 个单占
//   2p4 → 1 对，2 个单占
//   2p3 → 0 对，3 个单占
void splitElectrons(int electrons, int orbitals, int* paired, int* single)
{
    *paired = std::max(0, electrons - orbitals);
    *single = electrons - 2 * (*paired);
}

// 画一个自旋箭头：一根竖线 + 顶端三角。
//   up = true  → ↑（base 在下方，尖端朝上）
//   up = false → ↓（base 在上方，尖端朝下）
// 为什么不用字符 "↑"：ImGui 默认字体只有 ASCII，U+2191 会显示成方块。
void drawArrow(ImDrawList* dl, ImVec2 base, float height, ImU32 color, bool up)
{
    const float dir = up ? -1.0f : 1.0f;
    const ImVec2 tip(base.x, base.y + dir * height);
    dl->AddLine(base, tip, color, 1.6f);

    const float head = height * 0.35f;
    const float half = height * 0.25f;
    dl->AddTriangleFilled(tip,
                          ImVec2(tip.x - half, tip.y - dir * head),
                          ImVec2(tip.x + half, tip.y - dir * head),
                          color);
}

} // namespace

// ============================================================
//  缓存
// ============================================================
void EnergyDiagramState::refresh(const orbital::Element& element, int charge)
{
    // 只有元素或电荷变了才重建。每帧重建等于每帧复制一遍 vector，
    // 是技术债里记着的那条「按值返回 vector」的具体来源。
    if (cachedZ == element.atomicNumber && cachedCharge == charge)
        return;

    cachedZ      = element.atomicNumber;
    cachedCharge = charge;

    const std::vector<orbital::Subshell> subs =
        (charge == 0) ? element.subshells : element.makeIon(charge).subshells;

    // 已有亚层按能量高低排好（低能量的在前），直接铺成行。
    // 注意这里包含「电子数为 0 的亚层」：Fe2+ 的 4s 就是 0 个电子，
    // 它仍然占一个能量位次，抹掉反而看不出「4s 先失去」这件事。
    const std::vector<orbital::Subshell> ordered = orbital::sortByEnergy(subs);

    rows.clear();
    for (const orbital::Subshell& s : ordered)
    {
        LevelRow r;
        r.sub      = s;
        r.orbitals = orbitalCount(s.l);
        r.capacity = 2 * r.orbitals;
        splitElectrons(s.electrons, r.orbitals, &r.paired, &r.single);
        rows.push_back(r);
    }

    // ---- 补一个「下一个填这里」的空层 ----
    //
    // 只有【所有已占据的亚层都满了】才补。
    // 反例（曾经写错过）：Fe 的 3d 是 6/10，下一个电子仍然进 3d，
    // 这时候标一个空的 4p 是化学错误 —— 4p 要等 3d 满了才轮到。
    // 同理 H+（1s 空了）下一个该填的是 1s，不该凭空冒出 2s。
    bool allFull = true;
    for (const LevelRow& r : rows)
    {
        if (r.sub.electrons < r.capacity) { allFull = false; break; }
    }

    if (allFull)
    {
        for (const std::pair<int, int>& nl : orbital::energySequence())
        {
            const bool exists = std::any_of(
                ordered.begin(), ordered.end(),
                [&nl](const orbital::Subshell& s) { return s.n == nl.first && s.l == nl.second; });
            if (exists) continue;

            // 填充是连续的，第一个空缺的层一定比现有层能量都高，直接追加到末尾
            LevelRow r;
            r.sub.n         = nl.first;
            r.sub.l         = nl.second;
            r.sub.electrons = 0;
            r.orbitals      = orbitalCount(nl.second);
            r.capacity      = 2 * r.orbitals;
            r.placeholder   = true;
            rows.push_back(r);
            break;
        }
    }
}

// ============================================================
//  窗口
// ============================================================
void drawEnergyDiagram(const orbital::ElementDatabase& db,
                       int atomicNumber, int charge,
                       EnergyDiagramState& state)
{
    if (!state.visible) return;

    const orbital::Element* el = db.findByZ(atomicNumber);
    if (el == nullptr) return;                     // main 已保证只在数据库非空时调用

    state.refresh(*el, charge);

    const int rowCount = static_cast<int>(state.rows.size());
    if (rowCount == 0) return;

    const float gridW    = kMaxBoxes * (kBoxW + kBoxGap);
    // 右侧留 80px：够放 "10/10" 这列计数，也让图例那几行英文不用换行。
    // 留窄了 ImGui 不会自动折行，文字直接被窗口边缘裁掉。
    const float contentW = kLabelW + gridW + 80.0f;
    const float winW     = contentW + kPadX * 2.0f;

    // 放在左侧控制面板右边、周期表左边；屏幕窄时会重叠，但用户可以拖开
    ImGui::SetNextWindowPos(ImVec2(360.0f, 60.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(winW, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.92f);

    if (!ImGui::Begin("Energy Levels", &state.visible))
    {
        ImGui::End();
        return;
    }

    // ---------- 标题 ----------
    // 强调色和周期表面板保持一致（应用固定 StyleColorsDark，见 main.cpp）。
    // 两个面板挨着显示，标题颜色不一样会显得像两个程序。
    ImGui::TextColored(ImVec4(0.62f, 0.85f, 1.0f, 1.0f), "%s   %s",
                       el->symbol.c_str(), el->name.c_str());
    if (charge == 0)
        ImGui::TextDisabled("Z=%d   neutral atom", el->atomicNumber);
    else
        ImGui::TextDisabled("Z=%d   charge %+d", el->atomicNumber, charge);

    ImGui::TextDisabled("energy increases upward");
    ImGui::Spacing();

    // ---------- 画布 ----------
    // 整块区域做成一个 InvisibleButton：既能拿 hover，又不用为每个方框生成 ID。
    // 鼠标落在哪一行靠坐标反算（行高固定，算得出来，不必逐行判矩形）。
    const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    const float  canvasH   = rowCount * kRowH;

    ImGui::InvisibleButton("##canvas", ImVec2(contentW, canvasH));

    const bool   hovered = ImGui::IsItemHovered();
    const ImVec2 mouse   = ImGui::GetIO().MousePos;

    int hoverRow = -1;                             // 0 = 最低能量那一行
    if (hovered)
    {
        const int fromTop = static_cast<int>((mouse.y - canvasPos.y) / kRowH);
        if (fromTop >= 0 && fromTop < rowCount)
            hoverRow = rowCount - 1 - fromTop;     // rows[0] 画在最下面
    }

    ImDrawList*  dl     = ImGui::GetWindowDrawList();
    const ImU32  dimInk = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const ImU32  ink    = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32  frame  = ImGui::GetColorU32(ImGuiCol_Border);
    const ImU32  slotBg = ImGui::GetColorU32(ImGuiCol_FrameBg, 0.55f);
    const ImU32  hlBg   = ImGui::GetColorU32(ImGuiCol_Header, 0.18f);

    const float boxH     = kRowH - 12.0f;
    const float countX   = canvasPos.x + kLabelW + gridW + 10.0f;

    for (int i = 0; i < rowCount; ++i)
    {
        const LevelRow& row = state.rows[i];
        // 能量高的画在上面：i 越大 → 越靠近画布顶部
        const float rowTop = canvasPos.y + (rowCount - 1 - i) * kRowH;

        if (i == hoverRow)
            dl->AddRectFilled(ImVec2(canvasPos.x, rowTop),
                              ImVec2(canvasPos.x + contentW, rowTop + kRowH),
                              hlBg, 3.0f);

        // ---- 亚层名（4s / 3d …） ----
        const std::string name = orbital::subshellName(row.sub);
        const ImVec2      ts   = ImGui::CalcTextSize(name.c_str());
        dl->AddText(ImVec2(canvasPos.x + 6.0f, rowTop + (kRowH - ts.y) * 0.5f),
                    row.placeholder ? dimInk : ink, name.c_str());

        // ---- 轨道方框 ----
        const float boxTop = rowTop + (kRowH - boxH) * 0.5f;
        const float x0     = canvasPos.x + kLabelW;

        for (int k = 0; k < row.orbitals; ++k)
        {
            const float  bx = x0 + k * (kBoxW + kBoxGap);
            const ImVec2 a(bx, boxTop);
            const ImVec2 b(bx + kBoxW, boxTop + boxH);

            if (!row.placeholder)
                dl->AddRectFilled(a, b, slotBg, 2.0f);   // 空框也要有底，否则像没画
            dl->AddRect(a, b, row.placeholder ? frame : dimInk, 2.0f);

            if (row.placeholder) continue;               // 空层不放电子

            // 洪特规则：前 paired 个框双占，接着 single 个框单占
            int e = 0;
            if (k < row.paired)                        e = 2;
            else if (k < row.paired + row.single)      e = 1;
            if (e == 0) continue;

            const float cx = bx + kBoxW * 0.5f;
            if (e == 2)
            {
                // 一对自旋方向相反：↑ 靠左、↓ 靠右，两个箭头并排
                drawArrow(dl, ImVec2(cx - 3.5f, boxTop + boxH - 3.0f), boxH - 6.0f,
                          kSpinUp, true);
                drawArrow(dl, ImVec2(cx + 3.5f, boxTop + 3.0f), boxH - 6.0f,
                          kSpinDown, false);
            }
            else
            {
                drawArrow(dl, ImVec2(cx, boxTop + boxH - 3.0f), boxH - 6.0f,
                          kSpinUp, true);
            }
        }

        // ---- 右侧电子计数 ----
        char count[16];
        std::snprintf(count, sizeof(count), "%d/%d", row.sub.electrons, row.capacity);
        const ImVec2 cs = ImGui::CalcTextSize(count);
        dl->AddText(ImVec2(countX, rowTop + (kRowH - cs.y) * 0.5f),
                    row.placeholder ? dimInk : ImGui::GetColorU32(ImGuiCol_Text), count);
    }

    // ---------- 悬停说明 ----------
    if (hoverRow >= 0)
    {
        const LevelRow& row = state.rows[hoverRow];
        const std::string name = orbital::subshellName(row.sub);
        if (row.placeholder)
            ImGui::SetTooltip("%s is empty here\nthis is the next subshell to fill", name.c_str());
        else
            ImGui::SetTooltip("%s   %d / %d electrons\n%d orbital(s)   %d paired   %d unpaired",
                              name.c_str(), row.sub.electrons, row.capacity,
                              row.orbitals, row.paired, row.single);
    }

    // ---------- 图例 ----------
    ImGui::Spacing();
    ImGui::Separator();
    // 不用 "↑↓" 字符：默认字体没有这些字形，会显示成方块
    ImGui::TextDisabled("two arrows in a box = pair");
    ImGui::TextDisabled("one arrow = unpaired electron");
    ImGui::TextDisabled("dim box = next subshell to fill");
    ImGui::TextDisabled("follows table selection");

    ImGui::End();
}

// ============================================================
//  ASCII 版（--dump-levels）
// ============================================================
void printEnergyDiagramAscii(const orbital::ElementDatabase& db,
                             int atomicNumber, int charge)
{
    const orbital::Element* el = db.findByZ(atomicNumber);
    if (el == nullptr)
    {
        std::printf("数据库里没有原子序数 %d 的元素\n", atomicNumber);
        return;
    }

    EnergyDiagramState state;
    state.refresh(*el, charge);

    std::printf("%s  %s   Z=%d", el->symbol.c_str(), el->name.c_str(), el->atomicNumber);
    if (charge != 0) std::printf("   charge %+d", charge);
    std::printf("\n\n");

    std::printf("  energy ^\n");
    std::printf("         |\n");

    // 从高能量往低能量打印：文本从上到下即能量从高到低。
    // 所以 4s 的行必须出现在 3d 之后 —— 这就是本日要验证的东西。
    for (int i = static_cast<int>(state.rows.size()) - 1; i >= 0; --i)
    {
        const LevelRow& row  = state.rows[i];
        const std::string name = orbital::subshellName(row.sub);

        std::printf("  %-3s    ", name.c_str());

        int printed = 0;
        for (int k = 0; k < row.orbitals; ++k)
        {
            int e = 0;
            if (!row.placeholder)
            {
                if (k < row.paired)                   e = 2;
                else if (k < row.paired + row.single) e = 1;
            }
            std::printf("%s", e == 2 ? "[^v]" : (e == 1 ? "[^ ]" : "[  ]"));
            printed += 4;
        }
        // 补到最宽那一行（5 个框 = 20 字符），让右侧计数对齐
        for (; printed < kMaxBoxes * 4; ++printed) std::printf(" ");

        std::printf("  %d/%d", row.sub.electrons, row.capacity);
        if (row.placeholder) std::printf("   (next to fill)");
        std::printf("\n");
    }

    std::printf("         |\n");
    std::printf("\n图例: [^v]=一对电子   [^ ]=单电子   [  ]=空轨道   ^=自旋向上   v=自旋向下\n");
}

} // namespace ui
