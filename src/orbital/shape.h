// src/orbital/shape.h
// 第 19 天：s / p / d 轨道角向形状（Atomotheca 模块第 5 天）。
//
// 分层：和 element.h 一样只做纯数学，【不】include <glad/glad.h>。
// 输出「位置 + 法线」的三角形网格，由 main.cpp 负责建 VBO / VAO 并绘制。
//
// 所有形状都能用一句话描述：**半径 = f(方向) × extent**，f 是实球谐函数的角向部分。
//   s ：f = 1                    → 球                 （l = 0，0 个节面）
//   p ：f = |z|                  → 两个相切球          （l = 1，1 个节面）
//   d ：f = |2xy|, |x²−y²|, …    → 四瓣梅花 / 双瓣+腰环（l = 2，2 个节面）
//
// 第 18 天用「球拼」生成 s / p，因为那两种形状的曲面恰好就是球面（见 shape.cpp 开头
// 的推导），用球拼又快又精确。d 没有这种巧合，必须真的按 f 去变形一个球面，
// 所以今天新增一条通用路径 buildHarmonicShape()。两条路产出同一个 ShapeMesh，
// 上层（渲染 / UI）不需要知道自己拿到的是哪种。

#pragma once

#include <string>
#include <vector>

namespace orbital
{

// 支持的形状。顺序就是 UI 下拉框的顺序，也是数组下标。
enum class ShapeKind
{
    S = 0,
    Px,
    Py,
    Pz,
    Dxy,
    Dyz,
    Dxz,
    Dx2Y2,   // d(x²−y²)
    Dz2,     // d(z²)，两瓣 + 腰环
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

    // 每一瓣在 indices 里的连续区间：[lobeFirst[i], lobeFirst[i]+lobeSize[i])。
    // 第 18 天的 s / p 每瓣三角形数相同，用「总长 ÷ 瓣数」就能算出来；
    // d 的腰环和两极的瓣大小差很多，必须逐瓣记账。
    std::vector<int> lobeFirst;
    std::vector<int> lobeSize;

    int lobeCount() const { return static_cast<int>(lobeCenters.size() / 3); }

    float extent     = 1.0f;   // 原点到最远点的距离（瓣尖到原点的距离）
    float lobeRadius = 1.0f;   // 单瓣的「等效半径」：s 是球半径，d 取 extent
};

// "s" / "p_x" / "d_xy" / "d_z2" …
const char* shapeName(ShapeKind kind);

// 该形状的节面/对称面："-"（无）/"x" / "xy" / "z"
const char* shapeAxisName(ShapeKind kind);

// 角向部分的一句话写法："r = 1" / "r = |z|" / "r = |2xy|" …
// 面板提示和 --dump-orbital 都用它，避免同一个公式在两处各写一遍。
const char* shapeFormula(ShapeKind kind);

// 解析命令行参数：s / p / px / py / pz / dxy / dyz / dxz / dx2y2 / dz2
// 大小写不敏感，下划线、空格、^ 会被忽略，所以 dx2-y2 / dx^2-y^2 / dz^2 都认。
bool parseShapeKind(const std::string& text, ShapeKind* out);

// 形状的代表色：s 中性青，p_x 红 / p_y 绿 / p_z 蓝（对应坐标轴），
// d 用五个与 p 明显不同的颜色（避免和轴配色混淆）。
// 颜色放在这里而不是 UI 层，是为了让控制面板的小色块和 3D 场景里的
// 实际颜色只有一个来源，不会改了一边忘另一边。
struct Rgb
{
    float r = 1.0f, g = 1.0f, b = 1.0f;
};
Rgb shapeColor(ShapeKind kind);

// 生成网格。extent 是原点到最远点的距离，所有形状共用同一个 extent
// 才能保证切换形状时视觉大小不跳。
void buildShape(ShapeKind kind, float extent, ShapeMesh& out);

// 几何自检 + ASCII 剖面（--dump-orbital 用）。
// 检查项：顶点数 / 三角形数、包围盒、法线是否单位长度、
//          三角形绕序是否朝外、所有顶点是否都落在解析曲面上、
//          有向体积（散度定理）与解析体积是否吻合。
// 这些检查在窗口里肉眼看不出来，但绕序反了会让半透明轨道"内外翻转"，
// 所以必须有一个不需要显卡的验证入口。
std::string describeShape(ShapeKind kind);

} // namespace orbital
