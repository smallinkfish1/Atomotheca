// src/orbital/cloud.h
// 第 20 天：氢样轨道的电子云点云（Atomotheca 模块第 6 天）。
//
// 第 19 天的 ShapeMesh 画的是 **角向部分** |Y(方向)|：所以 1s / 2s / 3s 是同一颗球，
// 因为它们长得一样——径向部分被丢掉了。今天补上径向函数 R_nl(r)：
//
//     ψ(r, 方向) = R_nl(r) · Y(方向)
//     ρ(r, 方向) = |ψ|²
//
// 按 ρ 撒点（密度正比于出现概率），每点一个像素，就自然出现了：
//   · 壳层——2s 是「一个球套一个球」，节面在 r = 2 a₀ 处是空的；
//   · 径向节面数 = n − l − 1（3s 有两个，3d 没有）；
//   · 角向节面——瓣与瓣之间的空白，和 Day 19 的 d 轨道一致。
//
// 分层：纯数学，【不】include <glad/glad.h>。
// 输出一组以玻尔半径 a₀ 为单位的点坐标 + 波函数符号，由 main.cpp 上传显存画出。

#pragma once

#include <string>
#include <vector>

#include "orbital/shape.h"   // 复用 ShapeKind 作为「角向部分」的选择，避免两套枚举

namespace orbital
{

// 一个采样点。
// 坐标单位是玻尔半径 a₀（Z = 1 的氢样轨道），phase = ψ 的符号（+1 / −1）。
// 相位不是装饰：同一个轨道相邻瓣的波函数符号相反，染色之后节面会自己显形。
struct CloudPoint
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float phase = 1.0f;
};

// 采样过程的全部可核对数字。--dump-cloud 把它们打出来，
// 面板只用到 r98（决定渲染缩放）。
struct CloudStats
{
    int    points      = 0;   // 实际点数（= 请求点数）
    long long dirTrials = 0;  // 角向拒绝采样的尝试次数，效率 = points / dirTrials

    // 抽样得到的径向矩 vs 闭式解，单位 a₀
    //   ⟨r⟩  = (3n² − l(l+1)) / 2
    //   ⟨r²⟩ = n²(5n² + 1 − 3l(l+1)) / 2
    double meanR = 0.0, analyticMeanR = 0.0;
    double meanR2 = 0.0, analyticMeanR2 = 0.0;

    // 角向自检：⟨cos²θ⟩ 的样本值 vs 数值积分值（与径向无关的独立量）
    double meanCos2 = 0.0, analyticMeanCos2 = 0.0;

    // 径向 CDF 的最大偏差（Kolmogorov–Smirnov 统计量）与 95% 显著性的参考上限
    double ksDeviation = 0.0, ksCritical = 0.0;

    int radialNodes         = 0;   // 在 R_nl(r) 上数出来的符号变化次数
    int expectedRadialNodes = 0;   // n − l − 1

    double tailFraction = 0.0;   // 抽样区间 [0, rMax] 之外被截掉的概率
    float  rMax = 0.0f;          // 抽样半径上界（a₀）
    float  r98  = 0.0f;          // 包含 98% 概率的半径（a₀），渲染缩放按它归一
};

// 角向部分的 l（0 = s，1 = p，2 = d）
int angularL(ShapeKind kind);

// 径向节面数 = n − l − 1。3s 有 2 个、3p 有 1 个、3d 有 0 个。
int radialNodeCount(int n, int l);

// 量子数合法性：1 ≤ n ≤ 6，且 l ≤ n − 1（2d、1p 这种组合不存在）。
// 不合法返回 false，why 里写清原因（面板与命令行都直接显示这句话）。
bool validOrbital(int n, ShapeKind kind, std::string* why);

// 概率密度 |ψ|²，单位 a₀⁻³。参数是**物理坐标**（不是单位方向）。
// 给 --dump-cloud 画剖面用，也方便将来做点云之外的验证。
double probabilityDensity(int n, ShapeKind kind, double x, double y, double z);

// 撒点。count 是目标点数，out 里一定是 count 个点（顺序随机，不打乱）。
// 同一个 seed 在任何平台上都产出同一套点（只用 mt19937 + 自己做的定点抽样，
// 不用 std::uniform_real_distribution —— 它的实现随编译器变，文档里的数字就不成立了）。
void buildCloud(int n, ShapeKind kind, int count, unsigned seed,
                std::vector<CloudPoint>& out, CloudStats& stats);

// "1s" / "2p_x" / "3d_z2"
std::string cloudLabel(int n, ShapeKind kind);

// 解析 "3dz2" / "4p_x" / "1s" 这种「主量子数 + 角向形状」的写法。
// 形状部分直接交给 parseShapeKind，所以下划线、^、连字符都容忍。
bool parseOrbitalSpec(const std::string& text, int* n, ShapeKind* kind);

// 相位配色：ψ > 0 暖红、ψ < 0 冷蓝。
// 和 Day 19 的 shapeColor 分开：那边一个形状一种颜色（表达"这是哪条轨道"），
// 这边用相位（表达"这里波函数是正还是负"），两件事不要混。
Rgb phaseColor(float phase);

// 文本输出（--dump-cloud）
std::string describeCloud(int n, ShapeKind kind, int count);      // 详细：统计 + 数值表 + ASCII 剖面
std::string cloudSummaryLine(int n, ShapeKind kind, int count);   // 一行摘要，多轨道横向对比用

} // namespace orbital
