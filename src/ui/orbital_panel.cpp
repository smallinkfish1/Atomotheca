// src/ui/orbital_panel.cpp
// 第 19 天：轨道形状的控制项（9 个形状，改下拉框）。

#include "orbital_panel.h"

#include <imgui.h>

namespace ui
{

void drawOrbitalControls(OrbitalState& state)
{
    if (!ImGui::CollapsingHeader("ORBITAL SHAPE  (O)", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    ImGui::Checkbox("Show in 3D view", &state.visible);

    // 第 18 天是一排单选按钮，4 个刚好放得下；9 个会把面板挤成两行还超宽，
    // 所以改成下拉框。颜色仍然取自 orbital::shapeColor —— 面板里的名字和
    // 场景里的颜色是同一份数据，不会出现"面板写红、场景画蓝"。
    if (ImGui::BeginCombo("Shape", orbital::shapeName(state.kind)))
    {
        for (int i = 0; i < orbital::SHAPE_COUNT; ++i)
        {
            const orbital::ShapeKind k = static_cast<orbital::ShapeKind>(i);
            const orbital::Rgb       c = orbital::shapeColor(k);
            const bool               selected = (state.kind == k);

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.r, c.g, c.b, 1.0f));
            if (ImGui::Selectable(orbital::shapeName(k), selected))
                state.kind = k;
            ImGui::PopStyleColor();

            // 键盘上下键滚动下拉框时，焦点要跟着当前项走
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    // 角向部分的一句话公式，和 --dump-orbital 打印的是同一个来源
    ImGui::TextDisabled("angular part: %s", orbital::shapeFormula(state.kind));
    ImGui::TextDisabled("drawn at molecule centre");

    if (state.visible)
    {
        ImGui::SliderFloat("Opacity", &state.alpha, 0.05f, 0.900f, "%.2f");
        ImGui::Checkbox("Hide molecule", &state.hideMolecule);
    }
    else
    {
        ImGui::TextDisabled("press O to show it in the 3D view");
    }
}

} // namespace ui
