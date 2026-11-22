// src/orbital/shape.cpp
// 第 19 天：s / p / d 轨道角向形状的网格生成与自检。
//
// ============================================================
//  一句话概括：所有形状都是「半径 = f(方向) × extent」
// ============================================================
//
//   s ：f = 1                       球，没有节面
//   p ：f = |x| / |y| / |z|         两个相切球，1 个节面
//   d ：f = |2xy| / |2yz| / |2xz|   四瓣梅花，2 个节面
//       f = |x²−y²|                 四瓣梅花，只是转了 45°
//       f = |(3z²−1)/2|             两极两瓣 + 赤道腰环，2 个锥形节面
//
// f 的系数（2、3、1/2）不是随便写的：它们是实球谐函数的归一化因子，
// 选成让 max|f| = 1，这样所有形状的瓣尖都正好落在 extent 上，
// 切换形状时视觉大小不跳。
//
// ------------------------------------------------------------
//  为什么 s / p 用「球拼」，d 必须真的变形
// ------------------------------------------------------------
//   p 的角向部分是 |cos θ|（θ 从对称轴量起）。把它换成直角坐标：
//       z = r cos θ = cos² θ
//       ρ = r sin θ = sin θ cos θ      （ρ 是到对称轴的距离）
//   消去 θ 得 ρ² + (z − 1/2)² = 1/4 —— 正好是「半径 1/2、球心在 z = 1/2 的球」。
//   所以「两个相切的球」不是近似，是 |cos θ| 的精确形状，用球拼又快又准。
//   d 没有这种巧合（四瓣梅花不是球面拼出来的），只能老老实实按 f 去变形一个球面。
//   两条路产出同一个 ShapeMesh，上层不需要知道自己拿到的是哪种。
//
// ------------------------------------------------------------
//  d 的法线怎么来：不对方程求导，直接算隐函数的梯度
// ------------------------------------------------------------
//   曲面写成隐函数 F(p) = |p| − f(p̂)·extent = 0，
//   那么 ∇F 就是朝外的法线方向（梯度指向 F 增大的方向）。
//   用中心差分算六个采样点的 F 就够了，不用手推 ∂/∂θ、∂/∂φ 的解析式 ——
//   后者在节面附近会除零，差分不会。
//
//   绕序也靠它纠正：如果某个三角形的几何法线（叉积）和顶点法线反向，
//   就把索引后两个换个位置。这样「从外看逆时针」是算出来的，不是靠手工摆索引。

#include "shape.h"

#include "sphere.h"   // 复用 UV 球生成（纯数学，src/ 在 include 路径里）

#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <vector>

namespace orbital
{
namespace
{

constexpr float kPi = 3.14159265358979f;

// s / p 用球拼：分子球体是 32×16，轨道大一些（半透明表面轮廓要干净）
constexpr int kSector = 48;
constexpr int kStack  = 24;

// d 要直接变形球面：四瓣梅花的分界比球的经纬线更挑分段数，所以加密一倍。
// 96×48 = 4608 个格子 = 9216 个三角形，五个 d 加起来 46k，对显卡是零负担。
constexpr int kHarmSector = 96;
constexpr int kHarmStack  = 48;

// ------------------------------------------------------------
//  f(方向)：所有形状的角向部分，输入必须是单位向量
// ------------------------------------------------------------
float angularAbs(ShapeKind kind, float x, float y, float z)
{
    switch (kind)
    {
        case ShapeKind::Px: return std::fabs(x);
        case ShapeKind::Py: return std::fabs(y);
        case ShapeKind::Pz: return std::fabs(z);

        // d：l = 2 的五个实球谐函数（已归一化到 max = 1）
        case ShapeKind::Dxy:   return std::fabs(2.0f * x * y);
        case ShapeKind::Dyz:   return std::fabs(2.0f * y * z);
        case ShapeKind::Dxz:   return std::fabs(2.0f * x * z);
        case ShapeKind::Dx2Y2: return std::fabs(x * x - y * y);
        case ShapeKind::Dz2:   return std::fabs(0.5f * (3.0f * z * z - 1.0f));

        default: return 1.0f;   // s：常数
    }
}

// 任意点 p 对应的「半径」：先把方向归一化，再查 f。
// 这样调用方不用关心 p 在不在单位球上。
float radiusAtPoint(ShapeKind kind, float x, float y, float z)
{
    const float len = std::sqrt(x * x + y * y + z * z);
    if (len < 1e-9f) return 0.0f;
    return angularAbs(kind, x / len, y / len, z / len);
}

// F(p) = |p| − f(p̂)·extent；曲面就是 F = 0
float levelFunction(ShapeKind kind, float x, float y, float z, float extent)
{
    const float len = std::sqrt(x * x + y * y + z * z);
    return len - radiusAtPoint(kind, x, y, z) * extent;
}

// ∇F 归一化 = 朝外法线。差分步长取 extent 的千分之一：
// 太大抹平细节，太小被 float 的舍入吃掉。
bool gradientNormal(ShapeKind kind, const float p[3], float extent, float n[3])
{
    const float h = extent * 1e-3f;

    const float gx = levelFunction(kind, p[0] + h, p[1], p[2], extent)
                   - levelFunction(kind, p[0] - h, p[1], p[2], extent);
    const float gy = levelFunction(kind, p[0], p[1] + h, p[2], extent)
                   - levelFunction(kind, p[0], p[1] - h, p[2], extent);
    const float gz = levelFunction(kind, p[0], p[1], p[2] + h, extent)
                   - levelFunction(kind, p[0], p[1], p[2] - h, extent);

    const float len = std::sqrt(gx * gx + gy * gy + gz * gz);
    if (len < 1e-12f)
        return false;

    n[0] = gx / len;
    n[1] = gy / len;
    n[2] = gz / len;
    return true;
}

// 把 generateSphere 生成的「原点球」平移到一个瓣心，并按 lobeRadius 缩放。
//
// 法线不用重算：这是均匀缩放 + 平移，不改变方向，
// generateSphere 给出的本来就是单位法线。
void appendSphereLobe(float lobeRadius, float cx, float cy, float cz, ShapeMesh& out)
{
    std::vector<float>        verts;
    std::vector<unsigned int> idx;
    generateSphere(lobeRadius, kSector, kStack, verts, idx);

    // 索引要整体后移，否则第二个瓣会重复引用第一个瓣的顶点
    const unsigned int base = static_cast<unsigned int>(out.vertices.size() / 6);

    for (std::size_t i = 0; i + 5 < verts.size(); i += 6)
    {
        out.vertices.push_back(verts[i + 0] + cx);
        out.vertices.push_back(verts[i + 1] + cy);
        out.vertices.push_back(verts[i + 2] + cz);
        out.vertices.push_back(verts[i + 3]);   // 法线：不动
        out.vertices.push_back(verts[i + 4]);
        out.vertices.push_back(verts[i + 5]);
    }

    // 这一瓣在索引数组里是连续一段，记下起点和长度（d 的瓣大小不等，必须记账）
    out.lobeFirst.push_back(static_cast<int>(out.indices.size()));
    out.lobeSize.push_back(static_cast<int>(idx.size()));

    for (unsigned int i : idx)
        out.indices.push_back(base + i);

    out.lobeCenters.push_back(cx);
    out.lobeCenters.push_back(cy);
    out.lobeCenters.push_back(cz);
}

// ------------------------------------------------------------
//  d：把一个 UV 球面按 f 做径向变形
// ------------------------------------------------------------
void buildHarmonicShape(ShapeKind kind, float extent, ShapeMesh& out)
{
    // ---------- 1. 瓣心 ----------
    // 排序的单位是瓣。四瓣梅花的瓣心在对角线上，d_z2 比较特殊：
    // 两极各一瓣，中间那圈腰环以原点为中心，所以它自己就是一瓣。
    const float d  = extent * 0.55f;
    const float q  = 0.70710678f;   // √2 / 2

    auto addCenter = [&out](float ux, float uy, float uz) {
        out.lobeCenters.push_back(ux + 0.0f);   // +0 把 -0.0 规整成 +0.0
        out.lobeCenters.push_back(uy + 0.0f);
        out.lobeCenters.push_back(uz + 0.0f);
    };

    switch (kind)
    {
        case ShapeKind::Dxy:
            addCenter(-q * d, -q * d, 0.0f);
            addCenter(-q * d,  q * d, 0.0f);
            addCenter( q * d, -q * d, 0.0f);
            addCenter( q * d,  q * d, 0.0f);
            break;
        case ShapeKind::Dyz:
            addCenter(0.0f, -q * d, -q * d);
            addCenter(0.0f, -q * d,  q * d);
            addCenter(0.0f,  q * d, -q * d);
            addCenter(0.0f,  q * d,  q * d);
            break;
        case ShapeKind::Dxz:
            addCenter(-q * d, 0.0f, -q * d);
            addCenter(-q * d, 0.0f,  q * d);
            addCenter( q * d, 0.0f, -q * d);
            addCenter( q * d, 0.0f,  q * d);
            break;
        case ShapeKind::Dx2Y2:
            addCenter(-d, 0.0f, 0.0f);
            addCenter( d, 0.0f, 0.0f);
            addCenter(0.0f, -d, 0.0f);
            addCenter(0.0f,  d, 0.0f);
            break;
        default:   // Dz2
            addCenter(0.0f, 0.0f, -extent * 0.60f);
            addCenter(0.0f, 0.0f,  extent * 0.60f);
            addCenter(0.0f, 0.0f,  0.0f);            // 腰环：以原点为中心
            break;
    }

    // ---------- 2. 顶点 ----------
    // 极角从 +z 量起：x = sinθcosφ, y = sinθsinφ, z = cosθ
    out.vertices.reserve(static_cast<std::size_t>(kHarmSector + 1) *
                         static_cast<std::size_t>(kHarmStack + 1) * 6);

    for (int i = 0; i <= kHarmStack; ++i)
    {
        const float theta = kPi * static_cast<float>(i) / static_cast<float>(kHarmStack);
        const float st    = std::sin(theta);
        const float ct    = std::cos(theta);

        for (int j = 0; j <= kHarmSector; ++j)
        {
            const float phi = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(kHarmSector);
            const float dir[3] = { st * std::cos(phi), st * std::sin(phi), ct };

            const float r = angularAbs(kind, dir[0], dir[1], dir[2]) * extent;

            const float p[3] = { dir[0] * r, dir[1] * r, dir[2] * r };

            float n[3] = { dir[0], dir[1], dir[2] };
            gradientNormal(kind, p, extent, n);   // 失败就退回径向（极少发生）

            out.vertices.push_back(p[0]);
            out.vertices.push_back(p[1]);
            out.vertices.push_back(p[2]);
            out.vertices.push_back(n[0]);
            out.vertices.push_back(n[1]);
            out.vertices.push_back(n[2]);
        }
    }

    // ---------- 3. 三角形：先定向，再按瓣归类 ----------
    std::vector<std::vector<unsigned int>> buckets(out.lobeCenters.size() / 3);

    const int stride = kHarmSector + 1;

    for (int i = 0; i < kHarmStack; ++i)
    {
        for (int j = 0; j < kHarmSector; ++j)
        {
            const unsigned int k1 = static_cast<unsigned int>(i * stride + j);
            const unsigned int k2 = static_cast<unsigned int>((i + 1) * stride + j);

            const unsigned int tri[2][3] = {
                { k1, k1 + 1, k2 },
                { k1 + 1, k2 + 1, k2 }
            };

            for (int t = 0; t < 2; ++t)
            {
                unsigned int a = tri[t][0], b = tri[t][1], e = tri[t][2];

                const float* va = &out.vertices[a * 6];
                const float* vb = &out.vertices[b * 6];
                const float* ve = &out.vertices[e * 6];

                // 几何法线（叉积）
                const float u[3] = { vb[0] - va[0], vb[1] - va[1], vb[2] - va[2] };
                const float v[3] = { ve[0] - va[0], ve[1] - va[1], ve[2] - va[2] };
                const float g[3] = { u[1] * v[2] - u[2] * v[1],
                                     u[2] * v[0] - u[0] * v[2],
                                     u[0] * v[1] - u[1] * v[0] };

                // 顶点法线之和（三个都朝外，和也朝外）
                const float sn[3] = { va[3] + vb[3] + ve[3],
                                      va[4] + vb[4] + ve[4],
                                      va[5] + vb[5] + ve[5] };

                // 反向就把后两个索引换位 —— 这是「从外看逆时针」的保证
                if (g[0] * sn[0] + g[1] * sn[1] + g[2] * sn[2] < 0.0f)
                {
                    const unsigned int tmp = b;
                    b = e;
                    e = tmp;
                }

                // 归属：三角形重心离哪个瓣心最近就归哪一瓣
                const float* p0 = &out.vertices[a * 6];
                const float* p1 = &out.vertices[b * 6];
                const float* p2 = &out.vertices[e * 6];
                const float cx = (p0[0] + p1[0] + p2[0]) / 3.0f;
                const float cy = (p0[1] + p1[1] + p2[1]) / 3.0f;
                const float cz = (p0[2] + p1[2] + p2[2]) / 3.0f;

                int   best  = 0;
                float bestD = FLT_MAX;
                for (std::size_t k = 0; k + 2 < out.lobeCenters.size(); k += 3)
                {
                    const float dx = cx - out.lobeCenters[k + 0];
                    const float dy = cy - out.lobeCenters[k + 1];
                    const float dz = cz - out.lobeCenters[k + 2];
                    const float dd = dx * dx + dy * dy + dz * dz;
                    if (dd < bestD) { bestD = dd; best = static_cast<int>(k / 3); }
                }

                buckets[static_cast<std::size_t>(best)].push_back(a);
                buckets[static_cast<std::size_t>(best)].push_back(b);
                buckets[static_cast<std::size_t>(best)].push_back(e);
            }
        }
    }

    // ---------- 4. 按瓣拼接索引，记下每瓣的区间 ----------
    for (std::size_t k = 0; k < buckets.size(); ++k)
    {
        out.lobeFirst.push_back(static_cast<int>(out.indices.size()));
        out.lobeSize.push_back(static_cast<int>(buckets[k].size()));
        out.indices.insert(out.indices.end(), buckets[k].begin(), buckets[k].end());
    }
}

// 点是否落在形状实体内部（ASCII 剖面图用）。
// 判据就是那句话：|p| ≤ f(p̂)·extent。s 退化成球，p 退化成两个相切球，
// 三种形状共用一条判据，剖面图不会和 3D 里看到的形状对不上。
bool insideShape(ShapeKind kind, const ShapeMesh& m, float x, float y, float z)
{
    const float eps = m.extent * 1e-4f;
    const float r   = radiusAtPoint(kind, x, y, z) * m.extent;
    const float len = std::sqrt(x * x + y * y + z * z);
    return len <= r + eps;
}

// 打印一个剖面。plane 0 = x-y (z=0)，1 = y-z (x=0)，2 = x-z (y=0)。
// 选哪个面看形状躺在哪：d_xy 躺在 x-y 面上，d_xz 躺在 x-z 面上，
// 用错平面会看到「四个点」而不是「四瓣」。
void appendSlice(std::ostringstream& oss, ShapeKind kind,
                 const ShapeMesh& m, int plane, const char* planeName)
{
    constexpr int kCols = 31;
    constexpr int kRows = 13;

    const float span = m.extent * 1.08f;

    oss << "  slice " << planeName << "   ('#'=inside  '.'=outside  '-|'=axis)\n";
    for (int r = 0; r < kRows; ++r)
    {
        const float b = span * (1.0f - 2.0f * static_cast<float>(r) / static_cast<float>(kRows - 1));
        oss << "    ";
        for (int c = 0; c < kCols; ++c)
        {
            const float a = span * (-1.0f + 2.0f * static_cast<float>(c) / static_cast<float>(kCols - 1));

            float x = 0.0f, y = 0.0f, z = 0.0f;
            if (plane == 0)      { x = a; y = b; }        // x-y
            else if (plane == 1) { y = a; z = b; }        // y-z
            else                 { x = a; z = b; }        // x-z

            char ch;
            if (insideShape(kind, m, x, y, z)) ch = '#';
            else if (r == kRows / 2)           ch = '-';
            else if (c == kCols / 2)           ch = '|';
            else                               ch = '.';
            oss << ch;
        }
        oss << "\n";
    }
}

// 有向体积（散度定理）：V = (1/6) Σ p0 · (p1 × p2)
// 绕序一致朝外时为正，且等于真实体积；绕序整体反了会变成负数。
// 这是一个不依赖「顶点法线」的独立校验 —— 法线和绕序是一起算出来的，
// 只查它们两个等于是自证，查体积才有意义。
double meshVolume(const ShapeMesh& m)
{
    double v = 0.0;
    for (std::size_t t = 0; t + 2 < m.indices.size(); t += 3)
    {
        const float* p0 = &m.vertices[m.indices[t + 0] * 6];
        const float* p1 = &m.vertices[m.indices[t + 1] * 6];
        const float* p2 = &m.vertices[m.indices[t + 2] * 6];

        v += static_cast<double>(p0[0]) * (static_cast<double>(p1[1]) * p2[2] -
                                           static_cast<double>(p1[2]) * p2[1]);
        v += static_cast<double>(p0[1]) * (static_cast<double>(p1[2]) * p2[0] -
                                           static_cast<double>(p1[0]) * p2[2]);
        v += static_cast<double>(p0[2]) * (static_cast<double>(p1[0]) * p2[1] -
                                           static_cast<double>(p1[1]) * p2[0]);
    }
    return v / 6.0;
}

// 解析体积。
//   s：4/3 π R³
//   p：两个半径 R/2 的球 → 2 × 4/3 π (R/2)³ = π R³ / 3
//   d：没有初等闭式，用 (1/3)∮ r³ dΩ 数值积分（球坐标，样点比网格密一倍）
double analyticVolume(ShapeKind kind, float extent)
{
    const double R = static_cast<double>(extent);

    if (kind == ShapeKind::S)
        return 4.0 / 3.0 * static_cast<double>(kPi) * R * R * R;

    if (kind == ShapeKind::Px || kind == ShapeKind::Py || kind == ShapeKind::Pz)
        return static_cast<double>(kPi) * R * R * R / 3.0;

    const int    ns = 240;
    const int    nt = 120;
    const double dphi   = 2.0 * static_cast<double>(kPi) / ns;
    const double dtheta = static_cast<double>(kPi) / nt;

    double sum = 0.0;
    for (int i = 0; i < nt; ++i)
    {
        const double theta = (i + 0.5) * dtheta;          // 单元中心，避免极点的 sinθ = 0
        const double st = std::sin(theta), ct = std::cos(theta);
        for (int j = 0; j < ns; ++j)
        {
            const double phi = (j + 0.5) * dphi;
            const double x = st * std::cos(phi);
            const double y = st * std::sin(phi);
            const double z = ct;

            const double r = static_cast<double>(
                angularAbs(kind, static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)));

            // dΩ = sinθ dθ dφ
            sum += r * r * r * st * dtheta * dphi;
        }
    }
    return sum / 3.0 * R * R * R;
}

const char* kShapeNames[SHAPE_COUNT] = {
    "s", "p_x", "p_y", "p_z",
    "d_xy", "d_yz", "d_xz", "d_x2-y2", "d_z2"
};

const char* kShapeAxes[SHAPE_COUNT] = {
    "-", "x", "y", "z",
    "xy", "yz", "xz", "xy", "z"
};

const char* kShapeFormulas[SHAPE_COUNT] = {
    "r = 1", "r = |x|", "r = |y|", "r = |z|",
    "r = |2xy|", "r = |2yz|", "r = |2xz|", "r = |x^2-y^2|", "r = |3z^2-1|/2"
};

} // namespace

const char* shapeName(ShapeKind kind)
{
    const int i = static_cast<int>(kind);
    return (i >= 0 && i < SHAPE_COUNT) ? kShapeNames[i] : "?";
}

const char* shapeAxisName(ShapeKind kind)
{
    const int i = static_cast<int>(kind);
    return (i >= 0 && i < SHAPE_COUNT) ? kShapeAxes[i] : "?";
}

const char* shapeFormula(ShapeKind kind)
{
    const int i = static_cast<int>(kind);
    return (i >= 0 && i < SHAPE_COUNT) ? kShapeFormulas[i] : "?";
}

Rgb shapeColor(ShapeKind kind)
{
    switch (kind)
    {
        // p：轴配色，红 x / 绿 y / 蓝 z
        case ShapeKind::Px: return { 0.95f, 0.35f, 0.35f };
        case ShapeKind::Py: return { 0.40f, 0.85f, 0.45f };
        case ShapeKind::Pz: return { 0.40f, 0.60f, 0.98f };

        // d：五个和轴配色明显不同的颜色，避免和 p 混淆
        case ShapeKind::Dxy:   return { 0.72f, 0.55f, 0.95f };   // 紫
        case ShapeKind::Dyz:   return { 0.35f, 0.85f, 0.85f };   // 青
        case ShapeKind::Dxz:   return { 0.98f, 0.65f, 0.30f };   // 橙
        case ShapeKind::Dx2Y2: return { 0.95f, 0.45f, 0.72f };   // 洋红
        case ShapeKind::Dz2:   return { 0.95f, 0.88f, 0.40f };   // 黄

        default:               return { 0.55f, 0.80f, 0.90f };   // s：中性青
    }
}

bool parseShapeKind(const std::string& text, ShapeKind* out)
{
    // 容错：去掉下划线、空格、^ 和连字符，转小写，
    // 于是 p_x / px / d_x2-y2 / dx^2-y^2 / dz2 都认
    std::string s;
    for (char c : text)
    {
        if (c == '_' || c == ' ' || c == '\t' || c == '^' || c == '-') continue;
        s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    ShapeKind k;
    if (s == "s")                    k = ShapeKind::S;
    else if (s == "px")              k = ShapeKind::Px;
    else if (s == "py")              k = ShapeKind::Py;
    else if (s == "pz" || s == "p")  k = ShapeKind::Pz;   // 只写 p 时默认 p_z
    else if (s == "dxy")             k = ShapeKind::Dxy;
    else if (s == "dyz")             k = ShapeKind::Dyz;
    else if (s == "dxz")             k = ShapeKind::Dxz;
    else if (s == "dx2y2")           k = ShapeKind::Dx2Y2;
    else if (s == "dz2" || s == "d") k = ShapeKind::Dz2;   // 只写 d 时默认 d_z2
    else return false;

    if (out) *out = k;
    return true;
}

void buildShape(ShapeKind kind, float extent, ShapeMesh& out)
{
    out.vertices.clear();
    out.indices.clear();
    out.lobeCenters.clear();
    out.lobeFirst.clear();
    out.lobeSize.clear();
    out.extent = extent;

    if (kind == ShapeKind::S)
    {
        // 一个球：半径就是 extent
        out.lobeRadius = extent;
        appendSphereLobe(extent, 0.0f, 0.0f, 0.0f, out);
        return;
    }

    if (kind == ShapeKind::Px || kind == ShapeKind::Py || kind == ShapeKind::Pz)
    {
        // p：两瓣，半径 extent/2，瓣心在对称轴上 ±extent/2。
        // 这样 r = |cos θ| 的曲面正好从 -extent 延到 +extent，和 s 的直径一样大，
        // 切换形状时视觉尺寸不跳。
        out.lobeRadius = extent * 0.5f;
        const float d  = extent * 0.5f;

        float ax = 0.0f, ay = 0.0f, az = 0.0f;
        switch (kind)
        {
            case ShapeKind::Px: ax = 1.0f; break;
            case ShapeKind::Py: ay = 1.0f; break;
            default:            az = 1.0f; break;
        }

        // +0.0f 把 -0.0 规整成 +0.0：否则 --dump-orbital 会打印出 "(-0.500, -0.000, -0.000)"
        // 这种看着像 bug 的负零。
        appendSphereLobe(out.lobeRadius, -ax * d + 0.0f, -ay * d + 0.0f, -az * d + 0.0f, out);
        appendSphereLobe(out.lobeRadius,  ax * d + 0.0f,  ay * d + 0.0f,  az * d + 0.0f, out);
        return;
    }

    // d：真的按 f 变形球面。瓣尖在 f 的最大值处，所以瓣半径也是 extent。
    out.lobeRadius = extent;
    buildHarmonicShape(kind, extent, out);
}

std::string describeShape(ShapeKind kind)
{
    ShapeMesh m;
    buildShape(kind, 1.0f, m);

    const std::size_t vcount = m.vertices.size() / 6;
    const std::size_t tcount = m.indices.size() / 3;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);

    oss << shapeName(kind) << "   axis " << shapeAxisName(kind)
        << "   lobes " << m.lobeCount() << "   " << shapeFormula(kind) << "\n";
    oss << "  vertices   : " << vcount << "\n";
    oss << "  triangles  : " << tcount << "\n";
    oss << "  extent     : " << m.extent << "   lobe radius " << m.lobeRadius << "\n";

    // 瓣心写错了排序就会反，画面上表现为"近的那瓣被远的那瓣盖住"，
    // 所以把瓣心和每瓣的三角形数都打出来。
    oss << "  lobes      :";
    for (std::size_t i = 0; i + 2 < m.lobeCenters.size(); i += 3)
    {
        oss << "  (" << m.lobeCenters[i + 0] << ", "
                    << m.lobeCenters[i + 1] << ", "
                    << m.lobeCenters[i + 2] << ")";
    }
    oss << "\n";

    oss << "  tris/lobe  :";
    for (std::size_t i = 0; i < m.lobeSize.size(); ++i)
        oss << "  " << (m.lobeSize[i] / 3);
    oss << "\n";

    if (vcount == 0 || m.indices.empty())
    {
        oss << "  [!] 空网格\n";
        return oss.str();
    }

    // ---------- 包围盒 / 法线 / 曲面误差 ----------
    float lo[3] = {  1e9f,  1e9f,  1e9f };
    float hi[3] = { -1e9f, -1e9f, -1e9f };
    float maxNormalErr  = 0.0f;
    float maxSurfaceErr = 0.0f;

    for (std::size_t i = 0; i + 5 < m.vertices.size(); i += 6)
    {
        const float p[3] = { m.vertices[i + 0], m.vertices[i + 1], m.vertices[i + 2] };
        for (int k = 0; k < 3; ++k)
        {
            if (p[k] < lo[k]) lo[k] = p[k];
            if (p[k] > hi[k]) hi[k] = p[k];
        }

        // 法线必须是单位长度，否则半透明的 Fresnel 项会算歪
        const float nx = m.vertices[i + 3], ny = m.vertices[i + 4], nz = m.vertices[i + 5];
        const float nlen  = std::sqrt(nx * nx + ny * ny + nz * nz);
        const float nerr  = std::fabs(nlen - 1.0f);
        if (nerr > maxNormalErr) maxNormalErr = nerr;

        // 每个顶点都必须落在解析曲面上：|p| = f(p̂)·extent
        // 这句话对 s / p / d 全都成立，所以不用按形状分支
        const float d   = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        const float r   = radiusAtPoint(kind, p[0], p[1], p[2]) * m.extent;
        const float err = std::fabs(d - r);
        if (err > maxSurfaceErr) maxSurfaceErr = err;
    }

    oss << "  bbox x     : " << lo[0] << " .. " << hi[0] << "\n";
    oss << "  bbox y     : " << lo[1] << " .. " << hi[1] << "\n";
    oss << "  bbox z     : " << lo[2] << " .. " << hi[2] << "\n";

    // ---------- 绕序 ----------
    // 三角形的几何法线（叉积）必须和顶点法线同向，否则是"从里往外看逆时针"，
    // 开背面剔除之后会看到内壁 —— 半透明时表现为整块形状突然变亮/消失。
    //
    // 极点处要单独处理：UV 球的南北极一圈三角形有两个顶点重合，
    // 叉积恒为 0，那不是"绕序反了"，是退化三角形（画不出像素）。
    // 直接按 dot <= 0 判会把 2 × sectorCount 个退化三角形误报成错误。
    // d 的节面上还有一圈细长的退化三角形，同样算"正常"。
    int reversed   = 0;
    int degenerate = 0;

    for (std::size_t t = 0; t + 2 < m.indices.size(); t += 3)
    {
        const unsigned int i0 = m.indices[t + 0];
        const unsigned int i1 = m.indices[t + 1];
        const unsigned int i2 = m.indices[t + 2];

        const float* v0 = &m.vertices[i0 * 6];
        const float* v1 = &m.vertices[i1 * 6];
        const float* v2 = &m.vertices[i2 * 6];

        const float e1[3] = { v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2] };
        const float e2[3] = { v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2] };
        const float g[3]  = { e1[1] * e2[2] - e1[2] * e2[1],
                              e1[2] * e2[0] - e1[0] * e2[2],
                              e1[0] * e2[1] - e1[1] * e2[0] };

        const float glen  = std::sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
        const float e1len = std::sqrt(e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2]);
        const float e2len = std::sqrt(e2[0] * e2[0] + e2[1] * e2[1] + e2[2] * e2[2]);

        if (glen <= 1e-6f * e1len * e2len)
        {
            ++degenerate;
            continue;
        }

        const float n[3] = { v0[3] + v1[3] + v2[3],
                             v0[4] + v1[4] + v2[4],
                             v0[5] + v1[5] + v2[5] };

        if (g[0] * n[0] + g[1] * n[1] + g[2] * n[2] <= 0.0f)
            ++reversed;
    }

    oss << "  normal len : max |n-1| = " << maxNormalErr << "\n";
    oss << "  on surface : max | |p| - r | = " << maxSurfaceErr << "\n";
    oss << "  winding    : " << reversed << " / " << tcount
        << " triangles reversed"
        << (reversed == 0 ? "  (ok, CCW outward)\n" : "  [!!] 绕序反了\n");
    oss << "  degenerate : " << degenerate << " (极点/节面上的退化三角形，正常)\n";

    // ---------- 体积（散度定理） ----------
    // 这一项和法线、绕序都不相关：法线是算绕序用的，绕序是算几何法线用的，
    // 两个互相印证等于自证。体积是从三角形本身积分出来的独立量。
    const double vmesh = meshVolume(m);
    const double vtrue = analyticVolume(kind, m.extent);
    const double ratio = (vtrue > 1e-9) ? vmesh / vtrue : 0.0;

    oss << std::setprecision(4);
    oss << "  volume     : mesh " << vmesh << "   analytic " << vtrue
        << "   (" << std::setprecision(1) << (ratio * 100.0) << "%)"
        << (std::fabs(ratio - 1.0) < 0.05 ? "  ok\n" : "  [!!] 偏差过大\n");
    oss << std::setprecision(3);

    // ---------- 剖面 ----------
    // 选形状所在的那个平面：d_xy 躺在 x-y 面上，d_xz 在 x-z 面上，用错平面只看到几个点。
    int plane = 0;
    const char* planeName = "x-y (z=0):";
    switch (kind)
    {
        case ShapeKind::Pz: case ShapeKind::Dz2: case ShapeKind::Dyz:
            plane = 1; planeName = "y-z (x=0):"; break;
        case ShapeKind::Dxz:
            plane = 2; planeName = "x-z (y=0):"; break;
        default:
            plane = 0; planeName = "x-y (z=0):"; break;
    }
    appendSlice(oss, kind, m, plane, planeName);

    return oss.str();
}

} // namespace orbital
