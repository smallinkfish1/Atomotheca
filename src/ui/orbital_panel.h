// src/ui/orbital_panel.h
// 第 18 天：轨道形状的控制项（Atomotheca 模块第 4 天）。
//
// 和 periodic_table.h / energy_diagram.h 的区别：这里**不**新开窗口。
// 默认窗口 1100×700，左侧控制面板 + 周期表(652) + 能级图已经把屏幕占满，
// 再加一个窗口只会互相遮挡。所以这一段是控制面板里的一个折叠区。
//
// 分层：只依赖 ImGui 与 orbital/shape.h（纯数学），不 include <glad/glad.h>。
// 真正的绘制在 main.cpp —— 建 VAO、开混合、排序，这些都属于渲染层。

#pragma once

#include "orbital/shape.h"

namespace ui
{

// 轨道形状的跨帧状态。
struct OrbitalState
{
    // 默认关闭：不改任何东西时，行为和 Day 17 完全一致
    bool visible = false;

    // 画哪个形状。颜色和对称轴由 orbital::shapeColor / shapeAxisName 决定，
    // 不在这里另存一份，避免改了 3D 里的颜色忘了改面板色块。
    orbital::ShapeKind kind = orbital::ShapeKind::S;

    // 基础不透明度。最终 alpha 还叠了 Fresnel 项（见 orbital.frag），
    // 所以这个值偏小时轮廓依然清楚。
    float alpha = 0.28f;

    // 画轨道时是否隐藏分子。
    // 半透明轨道套在原子上，两者互相遮挡基本看不清形状，所以默认隐藏；
    // 但保留开关，因为"轨道包住原子"本身也是有意义的观察角度。
    bool hideMolecule = true;
};

// 在调用方已打开的窗口里画一组控件（不自带 Begin / End）。
void drawOrbitalControls(OrbitalState& state);

} // namespace ui
