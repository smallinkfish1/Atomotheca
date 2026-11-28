// src/ui/cloud_panel.cpp
// 第 20 天：电子云的控制项。

#include "cloud_panel.h"

#include <imgui.h>

namespace ui
{
namespace
{

// 这个 n 下允许的角向部分。l ≤ n−1 是硬约束：
// 1s 只有 s，2s/2p，3 起才有 d。列表在这里生成，UI 里就不会出现 2d 这种组合。
bool angularAllowed(int n, orbital::ShapeKind kind)
{
    return orbital::angularL(kind) <= n - 1;
}

} // namespace

void drawCloudControls(CloudState& state, const orbital::CloudStats& stats)
{
    if (!ImGui::CollapsingHeader("ELECTRON CLOUD  (C)", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    ImGui::Checkbox("Show in 3D view", &state.visible);

    // ---- 主量子数 ----
    static const char* kNLabels[] = { "1", "2", "3", "4", "5", "6" };
    int nIndex = state.n - 1;
    if (ImGui::Combo("n", &nIndex, kNLabels, IM_ARRAYSIZE(kNLabels)))
    {
        state.n = nIndex + 1;

        // 换了 n 之后，原来的角向部分可能不存在了（3d → 1d）。
        // 就地退回 s，而不是让 main.cpp 拿到一对非法量子数后默默不画。
        if (!angularAllowed(state.n, state.angular))
            state.angular = orbital::ShapeKind::S;
    }

    // ---- 角向部分：只列出当前 n 允许的 ----
    if (ImGui::BeginCombo("Orbital", orbital::shapeName(state.angular)))
    {
        for (int i = 0; i < orbital::SHAPE_COUNT; ++i)
        {
            const orbital::ShapeKind k = static_cast<orbital::ShapeKind>(i);
            if (!angularAllowed(state.n, k)) continue;

            const bool selected = (state.angular == k);

            if (ImGui::Selectable(orbital::shapeName(k), selected))
                state.angular = k;
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::TextDisabled("%s   radial nodes %d", orbital::cloudLabel(state.n, state.angular).c_str(),
                        orbital::radialNodeCount(state.n, orbital::angularL(state.angular)));

    if (!state.visible)
    {
        ImGui::TextDisabled("press C to show it in the 3D view");
        return;
    }

    // 点数用对数刻度：1 万 和 10 万在视觉上的差别不是线性的
    ImGui::SliderInt("Points", &state.pointCount, 10000, 200000, "%d",
                     ImGuiSliderFlags_Logarithmic);

    ImGui::SliderFloat("Point size", &state.pointSize, 0.30f, 3.00f, "%.2f %%");
    ImGui::SliderFloat("Opacity", &state.opacity, 0.05f, 1.00f, "%.2f");

    if (ImGui::Button("Resample"))
        ++state.seed;

    ImGui::Checkbox("Hide molecule", &state.hideMolecule);

    // ---- 上一次采样的物理量 ----
    // mean r 与 r98 的单位是玻尔半径：n 越大壳层越外扩，
    // 但 3D 里看到的尺寸是被 r98 归一过的，所以「看起来一样大」并不代表一样大。
    ImGui::TextDisabled("mean r %.2f a0   r98 %.1f a0", stats.meanR, stats.r98);
    ImGui::TextDisabled("red  psi > 0    blue  psi < 0");
}

} // namespace ui
