// src/ui/energy_diagram.h
// 第 17 天：2D 能级图（Atomotheca 模块第 3 天）。
//
// 分层与 periodic_table.h 一致：
//   只依赖 Dear ImGui 与元素数据层，【不】include <glad/glad.h>。
//
// 画的是「电子在亚层里怎么坐」，不是真实的能量数值：
//   纵向只表示能量高低的**次序**（Madelung 顺序），没有刻度。
//   所以 4s 画在 3d 下方，因为 4s 能量更低、先填。

#pragma once

#include "orbital/element.h"

#include <vector>

namespace ui
{

// 能级图上的一行（一个亚层）。
//
// 电子分布只存两个数字，不存每轨道的占用表 —— 洪特规则下分布是唯一确定的：
//   n 个电子铺进 k 个轨道 → 先每个轨道一个，剩下的才两两配对。
// 所以 paired 和 single 就能完全还原画面，也省掉每帧一个 vector。
struct LevelRow
{
    orbital::Subshell sub;           // n / l / 电子数
    int  orbitals    = 1;            // 轨道数 = 2l + 1
    int  capacity    = 2;            // 最多装 2(2l+1) 个电子
    int  paired      = 0;            // 双占轨道数（画 ↑↓）
    int  single      = 0;            // 单占轨道数（画 ↑）
    bool placeholder = false;        // 数据里没有，只为显示「下一个填这里」而补的空层
};

// 能级图的跨帧状态。
struct EnergyDiagramState
{
    // 默认关闭：默认窗口 1100×700，左侧控制面板 340 + 周期表 652 已经占满，
    // 三个面板同时开必然叠在一起。想看的按 L。
    bool visible = false;

    // ---- 缓存 ----
    // 能级行依赖「哪个元素 + 哪个电荷」，和帧号无关。
    // 每帧重算会白白复制 vector（技术债里记着这条），所以只在输入变化时重建。
    int  cachedZ      = -1;
    int  cachedCharge = 0;
    std::vector<LevelRow> rows;      // 从低能量到高能量

    // 输入没变就直接返回；变了才重建 rows。
    void refresh(const orbital::Element& element, int charge);
};

// 画能级图窗口。
// 元素和电荷由调用方给（跟着周期表的选择走），面板自己不做选择 ——
// 同一份「当前看什么」存在两处，迟早会不一致。
void drawEnergyDiagram(const orbital::ElementDatabase& db,
                       int atomicNumber, int charge,
                       EnergyDiagramState& state);

// 把同一份能级按文本打到标准输出（--dump-levels 用）。
// 窗口在这一层验证不了，但「谁在谁上方」是纯数据，可以用终端核对：
// 输出从上到下能量递减，所以 4s 那一行必须出现在 3d 行【之后】。
void printEnergyDiagramAscii(const orbital::ElementDatabase& db,
                             int atomicNumber, int charge);

} // namespace ui
