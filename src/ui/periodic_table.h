// src/ui/periodic_table.h
// 第 16 天：元素周期表面板（Atomotheca 模块第 2 天）。
//
// 分层：
//   本文件与 periodic_table.cpp 只依赖 Dear ImGui 和元素数据层，
//   【不】include <glad/glad.h>，也不碰任何 OpenGL 状态。
//   UI 层只读数据库，不改数据 —— 数据错了要回 element.cpp 改，不在这里打补丁。

#pragma once

#include "orbital/element.h"

namespace ui
{

// 周期表面板的跨帧状态。
//
// 为什么单独开一个结构体，而不是继续往 main.cpp 加全局变量？
//   面板要记住「现在看哪个元素、看它的哪个离子」，这两件事只属于 UI，
//   放进 main.cpp 的全局变量里会让依赖继续变乱（第 20 天拆分时要还债）。
struct PeriodicTableState
{
    bool visible   = true;   // 面板是否显示（P 键切换）
    int  selectedZ = 8;      // 选中的原子序数，默认氧，和 --dump 的示例一致
    int  ionIndex  = 0;      // 0 = 中性原子；1..n 依次对应 commonIonCharges[i-1]

    // 当前选中的电荷：0 = 中性原子，否则取 commonIonCharges[ionIndex-1]。
    //
    // 能级图要跟着「现在看哪个元素的哪个离子」走，所以把这次换算放在状态自己身上。
    // 否则 main.cpp 得知道 ionIndex 是 1-based、还得自己判越界 —— 那是把内部表示漏出去。
    int selectedCharge(const orbital::Element& e) const;
};

// 绘制周期表窗口（网格 + 选中元素详情）。
// 数据库为空时只显示一行提示，不会崩，也不影响分子查看器。
void drawPeriodicTable(const orbital::ElementDatabase& db, PeriodicTableState& state);

// 以 ASCII 把周期表打到标准输出（--dump-table 用）。
//
// 存在的意义：窗口在这一层验证不了（开发环境没有 GL 上下文），
// 但「按 period / group 定位」的布局逻辑是纯计算，可以用终端输出核对。
// 输出约定：有元素打印符号，真实存在但数据库没收录的格子打印 '.'，不存在的格子留空。
void printPeriodicTableAscii(const orbital::ElementDatabase& db);

} // namespace ui
