// src/ui/cloud_panel.h
// 第 20 天：电子云的控制项（Atomotheca 模块第 6 天）。
//
// 和 orbital_panel 一样**不**新开窗口：控制面板里的一段折叠区。
// 屏幕上已经有面板 + 周期表 + 能级图，再开一个窗口只会互相遮挡。
//
// 分层：只依赖 ImGui 与 orbital/cloud.h（纯数学），不 include <glad/glad.h>。
// 建 VBO、开混合、画点都在 main.cpp —— 那是渲染层的事。

#pragma once

#include "orbital/cloud.h"

namespace ui
{

// 电子云的跨帧状态。
//
// 注意这里**没有**点坐标：几十万个点是 GPU 资源，跟着 state 走会让
// 面板的每一次字符串比较都拖着一大块内存。main.cpp 只比对下面这几个数，
// 发现变了才重新采样、重传 VBO。
struct CloudState
{
    bool visible = false;   // 默认关闭，不改变 Day 19 的默认行为

    int                n       = 3;                    // 主量子数
    orbital::ShapeKind angular = orbital::ShapeKind::Dz2;

    int   pointCount = 50000;   // 点数：少了像散沙，多了糊成一团
    float pointSize  = 1.00f;   // 点直径，占云半径的百分比
    float opacity    = 0.30f;   // 单点不透明度

    // 采样种子。同一个种子在任何平台上都得到同一套点（见 cloud.cpp），
    // 所以「重新采样」= seed + 1，不需要另存一份点。
    unsigned seed = 1u;

    // 画云时是否隐藏分子。默认隐藏，与 Day 18–19 的轨道形状保持一致：
    // 半透明的东西套在不透明的球上，两边的形状都看不清。
    bool hideMolecule = true;
};

// 在调用方已打开的窗口里画一组控件（不自带 Begin / End）。
// stats 是**上一次**采样的结果，只用于显示 ⟨r⟩ / r98 之类的数字；
// 状态刚改过时它会慢一帧，肉眼看不出来。
void drawCloudControls(CloudState& state, const orbital::CloudStats& stats);

} // namespace ui
