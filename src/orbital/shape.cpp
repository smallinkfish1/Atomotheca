// src/orbital/shape.cpp
// 第 18 天：s / p 轨道角向形状的网格生成与自检。
//
// 为什么 s 是球、p 是两个相切球（零基础版推导）：
//   球坐标下 r(θ) = |Y(θ)|。s 层的 Y 是常数（√(1/4π)），所以 r 不随方向变 → 球。
//   p 层的 Y ∝ cos θ（θ 是从 z 轴量起的极角），所以 r = |cos θ|。
//   把 r = cos θ（θ ∈ [0, 90°]）换成直角坐标：
//       z = r cos θ = cos² θ
//       ρ = r sin θ = sin θ cos θ      （ρ 是到 z 轴的距离）
//   消去 θ 得 ρ² + (z - 1/2)² = 1/4 —— 这就是"半径 1/2、球心在 z = 1/2 的球"。
//   θ ∈ [90°, 180°] 时 cos θ < 0，取绝对值后是同一个球关于原点的镜像。
//   所以「两个相切的球」不是偷懒的近似，而是角向函数本身的精确形状。

#include "shape.h"

#include "sphere.h"   // 复用 UV 球生成（纯数学，src/ 在 include 路径里）

#include <cctype>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <sstream>

namespace orbital
{
namespace
{

// 分段数：分子球体用 32×16，轨道大一些（半透明表面轮廓要干净）。
constexpr int kSector = 48;
constexpr int kStack  = 24;

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

    for (unsigned int i : idx)
        out.indices.push_back(base + i);

    out.lobeCenters.push_back(cx);
    out.lobeCenters.push_back(cy);
    out.lobeCenters.push_back(cz);
}

// 点是否落在形状实体内部（剖面图用）。
bool insideShape(ShapeKind kind, const ShapeMesh& m, float x, float y, float z)
{
    const float eps = m.extent * 1e-4f;

    if (kind == ShapeKind::S)
    {
        const float d2 = x * x + y * y + z * z;
        const float r  = m.extent + eps;
        return d2 <= r * r;
    }

    // p：落在任意一瓣球内即算内部（两瓣在原点相切，联集就是哑铃）
    const float r  = m.lobeRadius + eps;
    const float r2 = r * r;
    for (std::size_t i = 0; i + 2 < m.lobeCenters.size(); i += 3)
    {
        const float dx = x - m.lobeCenters[i + 0];
        const float dy = y - m.lobeCenters[i + 1];
        const float dz = z - m.lobeCenters[i + 2];
        if (dx * dx + dy * dy + dz * dz <= r2)
            return true;
    }
    return false;
}

// 打印一个剖面。plane 0 = x-y (z=0)，1 = y-z (x=0)。
// 这两个平面二选一就够：p 的两瓣沿对称轴排布，所选平面一定包含该轴。
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
            if (plane == 0) { x = a; y = b; }   // x-y
            else            { y = a; z = b; }   // y-z

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

const char* kShapeNames[SHAPE_COUNT] = { "s", "p_x", "p_y", "p_z" };
const char* kShapeAxes[SHAPE_COUNT]  = { "-", "x",   "y",   "z"   };

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

Rgb shapeColor(ShapeKind kind)
{
    switch (kind)
    {
        case ShapeKind::Px: return { 0.95f, 0.35f, 0.35f };   // 红 = x
        case ShapeKind::Py: return { 0.40f, 0.85f, 0.45f };   // 绿 = y
        case ShapeKind::Pz: return { 0.40f, 0.60f, 0.98f };   // 蓝 = z
        default:            return { 0.55f, 0.80f, 0.90f };   // s：中性青
    }
}

bool parseShapeKind(const std::string& text, ShapeKind* out)
{
    // 容错：去掉下划线、转小写，于是 p_x / px / PX 都认
    std::string s;
    for (char c : text)
    {
        if (c == '_' || c == ' ' || c == '\t') continue;
        s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    ShapeKind k;
    if (s == "s")        k = ShapeKind::S;
    else if (s == "px")  k = ShapeKind::Px;
    else if (s == "py")  k = ShapeKind::Py;
    else if (s == "pz" || s == "p") k = ShapeKind::Pz;   // 只写 p 时默认 p_z
    else return false;

    if (out) *out = k;
    return true;
}

void buildShape(ShapeKind kind, float extent, ShapeMesh& out)
{
    out.vertices.clear();
    out.indices.clear();
    out.lobeCenters.clear();
    out.extent = extent;

    if (kind == ShapeKind::S)
    {
        // 一个球：半径就是 extent
        out.lobeRadius = extent;
        appendSphereLobe(extent, 0.0f, 0.0f, 0.0f, out);
        return;
    }

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
}

std::string describeShape(ShapeKind kind)
{
    ShapeMesh m;
    buildShape(kind, 1.0f, m);

    const std::size_t vcount = m.vertices.size() / 6;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);

    oss << shapeName(kind) << "   axis " << shapeAxisName(kind)
        << "   lobes " << (m.lobeCenters.size() / 3) << "\n";
    oss << "  vertices   : " << vcount << "\n";
    oss << "  triangles  : " << (m.indices.size() / 3) << "\n";
    oss << "  extent     : " << m.extent << "   lobe radius " << m.lobeRadius << "\n";

    // 瓣心必须落在对称轴上：半透明排序按瓣心到相机的距离进行，
    // 瓣心写错了排序就会反，画面上表现为"近的那瓣被远的那瓣盖住"。
    oss << "  lobes      :";
    for (std::size_t i = 0; i + 2 < m.lobeCenters.size(); i += 3)
    {
        oss << "  (" << m.lobeCenters[i + 0] << ", "
                    << m.lobeCenters[i + 1] << ", "
                    << m.lobeCenters[i + 2] << ")";
    }
    oss << "\n";

    if (vcount == 0 || m.indices.empty())
    {
        oss << "  [!] 空网格\n";
        return oss.str();
    }

    // ---------- 包围盒 ----------
    float lo[3] = {  1e9f,  1e9f,  1e9f };
    float hi[3] = { -1e9f, -1e9f, -1e9f };
    float maxNormalErr = 0.0f;
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
        const float nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
        const float nerr = std::fabs(nlen - 1.0f);
        if (nerr > maxNormalErr) maxNormalErr = nerr;

        // 每个顶点都必须落在解析曲面上
        float err;
        if (kind == ShapeKind::S)
        {
            const float d = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
            err = std::fabs(d - m.extent);
        }
        else
        {
            float best = 1e9f;
            for (std::size_t c = 0; c + 2 < m.lobeCenters.size(); c += 3)
            {
                const float dx = p[0] - m.lobeCenters[c + 0];
                const float dy = p[1] - m.lobeCenters[c + 1];
                const float dz = p[2] - m.lobeCenters[c + 2];
                const float d  = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (d < best) best = d;
            }
            err = std::fabs(best - m.lobeRadius);
        }
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

        const float glen = std::sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
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
    oss << "  on surface : max |d-r| = " << maxSurfaceErr << "\n";
    oss << "  winding    : " << reversed << " / " << (m.indices.size() / 3)
        << " triangles reversed"
        << (reversed == 0 ? "  (ok, CCW outward)\n" : "  [!!] 绕序反了\n");
    oss << "  degenerate : " << degenerate << " (极点退化三角形，正常)\n";

    // ---------- 剖面 ----------
    // p_z 的哑铃在 x-y 平面上只剩原点一点，所以它必须看 y-z 平面。
    if (kind == ShapeKind::Pz)
        appendSlice(oss, kind, m, 1, "y-z (x=0):");
    else
        appendSlice(oss, kind, m, 0, "x-y (z=0):");

    return oss.str();
}

} // namespace orbital
