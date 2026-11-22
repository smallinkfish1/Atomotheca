// src/ui/orbital_panel.cpp
// 第 18 天：轨道形状的控制项。

#include "orbital_panel.h"

#include <imgui.h>

namespace ui
{

void drawOrbitalControls(OrbitalState& state)
{
    if (!ImGui::CollapsingHeader("ORBITAL SHAPE  (O)", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    ImGui::Checkbox("Show in 3D view", &state.visible);

    // 单选按钮的标签用形状自己的颜色：p_x 红 / p_y 绿 / p_z 蓝。
    // 这样面板上的颜色和场景里看到的颜色是同一份数据，
    // 不会出现"面板写红、场景画蓝"这种只靠肉眼才能发现的不一致。
    for (int i = 0; i < orbital::SHAPE_COUNT; ++i)
    {
        const orbital::ShapeKind k = static_cast<orbital::ShapeKind>(i);
        const orbital::Rgb       c = orbital::shapeColor(k);

        if (i > 0)
            ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.r, c.g, c.b, 1.0f));
        if (ImGui::RadioButton(orbital::shapeName(k), state.kind == k))
            state.kind = k;
        ImGui::PopStyleColor();
    }

    if (state.visible)
    {
        ImGui::SliderFloat("Opacity", &state.alpha, 0.05f, 0.900f, "%.2f");
        ImGui::Checkbox("Hide molecule", &state.hideMolecule);
        ImGui::TextDisabled("drawn at molecule centre");
        ImGui::TextDisabled("angular part only: r = |cos theta|");
    }
    else
    {
        ImGui::TextDisabled("press O to show it in the 3D view");
        ImGui::TextDisabled("angular part only: r = |cos theta|");
    }
}

} // namespace ui
