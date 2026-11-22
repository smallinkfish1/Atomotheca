// src/orbital/shape.h
// 第 18 天：s / p 轨道的三维角向形状（Atomotheca 模块第 4 天）。
//
// 分层：和 element.h 一样只做纯数学，【不】include <glad/glad.h>。
// 输出「位置 + 法线」的三角形网格，由 main.cpp 负责建 VBO / VAO 并绘制。
//
// 今天画的是**角向部分**，不是完整的 ψ² 等值面：
//   s 层：角向部分是常数              → 球（半径 R）
//   p 层：角向部分是 |cos θ|          → r = |cos θ| 在极坐标里正好是
//                                       两个半径 R/2、中心在 ±R/2 的相切球
// 完整的电子云还要乘上径向函数 R(r)，那是 Day 20 的点云。

#pragma once

#include <string>
#include <vector>

namespace orbital
{

// 支持的形状。顺序就是 UI 单选按钮的顺序，也是数组下标。
enum class ShapeKind
{
    S = 0,
    Px,
    Py,
    Pz,
    Count
};

constexpr int SHAPE_COUNT = static_cast<int>(ShapeKind::Count);

// 一个可直接上传 GPU 的网格。
//
// vertices 的排布和 sphere.h 完全一致：每组 6 个 float
//   [px, py, pz, nx, ny, nz]
// 这样 main.cpp 建 VAO 的代码可以和原子球共用同一套顶点属性设置。
struct ShapeMesh
{
    std::vector<float>        vertices;      // 位置 + 法线
    std::vector<unsigned int> indices;

    // 每一「瓣」的中心（3 个 float 一组）。
    // 半透明渲染必须从远到近画，而排序的单位是瓣而不是整个形状：
    // p 层的两瓣一个在前一个在后，整形状只用一个距离是排不出来的。
    std::vector<float> lobeCenters;

    float extent     = 1.0f;   // 原点到最远点的距离（s 是球半径，p 是瓣尖距离）
    float lobeRadius = 1.0f;   // 单瓣半径（s 就等于 extent，p 是 extent/2）
};

// "s" / "p_x" / "p_y" / "p_z"
const char* shapeName(ShapeKind kind);

// 对称轴："-" / "x" / "y" / "z"
const char* shapeAxisName(ShapeKind kind);

// 解析命令行参数：s / p / px / p_x / py / pz（大小写不敏感，p 默认 pz）
bool parseShapeKind(const std::string& text, ShapeKind* out);

// 形状的代表色：s 用中性青，p_x 红 / p_y 绿 / p_z 蓝（对应三个坐标轴）。
// 颜色放在这里而不是 UI 层，是为了让控制面板的小色块和 3D 场景里的
// 实际颜色只有一个来源，不会改了一边忘另一边。
struct Rgb
{
    float r = 1.0f, g = 1.0f, b = 1.0f;
};
Rgb shapeColor(ShapeKind kind);

// 生成网格。extent 是原点到最远点的距离，所有形状共用同一个 extent
// 才能保证切换 s / p 时视觉大小不跳。
void buildShape(ShapeKind kind, float extent, ShapeMesh& out);

// 几何自检 + ASCII 剖面（--dump-orbital 用）。
// 检查项：顶点数 / 三角形数、包围盒、法线是否单位长度、
//          三角形绕序是否朝外、所有顶点是否都落在解析曲面上。
// 这些检查在窗口里肉眼看不出来，但绕序反了会让半透明轨道"内外翻转"，
// 所以必须有一个不需要显卡的验证入口。
std::string describeShape(ShapeKind kind);

} // namespace orbital
