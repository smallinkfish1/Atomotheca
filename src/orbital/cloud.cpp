// src/orbital/cloud.cpp
// 第 20 天：氢样轨道电子云（纯数学，不碰 OpenGL）。
//
// ============================================================
//  为什么不能继续用 Day 19 的 |Y| 形状
// ============================================================
//  Day 19 的图形是 r = |Y(方向)| · extent —— 只有角向，没有径向。
//  所以 1s / 2s / 3s 画出来是同一颗球，2p / 3p 是同一对哑铃。
//  真实电子云里 n 决定「多大、有几层」，l 决定「什么形状」，两件事必须分开：
//
//      ψ(r,θ,φ) = R_nl(r) · Y_lm(θ,φ)
//
//  这里 Y 用实球谐（和 Day 19 完全一致的九个式子），R 用氢样径向函数（Z = 1）：
//
//      R_nl(r) = sqrt( (2/n)³ · (n−l−1)! / (2n·(n+l)!) )
//                · e^(−r/n) · (2r/n)^l · L^(2l+1)_(n−l−1)(2r/n)
//
//  L 是连带拉盖尔多项式，用三项递推算，不用显式系数（n 到 6 时系数已经很难看）。
//
// ============================================================
//  怎么撒点：先半径、后方向，两块都能精确抽
// ============================================================
//  |ψ|² = R²(r)·|Y|² 是可分离的，所以联合分布能拆成两个独立的一维/球面分布：
//
//      P(方向) = |Y|²                      （对球面积分等于 1）
//      P(r | 方向) = r²R²(r)               （但和方向无关，见下）
//
//  等号右边之所以是 r²R² 而不是 R²：球坐标的体积元是 r²sinθ dr dθ dφ，
//  「半径落在 [r, r+dr] 的概率」必须乘上球壳面积 4πr²。
//
//  于是：
//    1. 半径：把 r²R² 在 [0, rMax] 上积成一张 4096 格的 CDF 表，再反演（二分 + 线性插值）。
//       这样抽样是**精确**的，而且一次只要 12 次比较，比拒绝采样快几个数量级
//       —— 直接在立方体里拒绝，接受率是万分之四，6 万个点要跑 1.5 亿次。
//    2. 方向：球面均匀撒，再按 |Y|²/|Y|²max 拒绝。s 是常数（永远接受），
//       p 接受率 1/3，d 最差的 d_z2 也有 20%，够用。
//
//  这两步的顺序可以换（先方向后半径也行），因为 P(r|方向) 与方向无关。
//
// ============================================================
//  为什么每个点的透明度是常数，不乘密度
// ============================================================
//  点的**数量密度**已经正比于 ρ 了。再按 ρ 调亮度等于把密度平方一次，
//  中心会糊成一个纯白团，壳层和节面全看不见。所以每个点一个 alpha，
//  由「一屏上有多少个点重叠」自然堆出层次。
//
// ============================================================

#include "cloud.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iomanip>
#include <random>
#include <sstream>

namespace orbital
{
namespace
{

constexpr double kPi = 3.14159265358979323846;

// 定点随机数：只用 mt19937 的原始 32 位输出，自己缩到 [0,1)。
// 不用 std::uniform_real_distribution —— 它的实现随标准库变，
// 同一个种子在 libstdc++ / libc++ / MSVC 上会给出不同序列。
// 文档里要写「seed = 1 时 ⟨r⟩ = 1.4999」这种可复现的数字，就必须自己定。
struct Rng
{
    std::mt19937 engine;

    explicit Rng(unsigned seed) : engine(seed) {}

    // 取高 24 位 → 分辨率 2⁻²⁴ ≈ 6e-8，撒点足够
    double next() { return static_cast<double>(engine() >> 8) * (1.0 / 16777216.0); }
};

double factorial(int k)
{
    double f = 1.0;
    for (int i = 2; i <= k; ++i) f *= static_cast<double>(i);
    return f;
}

// 连带拉盖尔多项式 L^alpha_k(x)，三项递推：
//   L_0 = 1
//   L_1 = 1 + α − x
//   (k+1)L_{k+1} = (2k + 1 + α − x)L_k − (k + α)L_{k−1}
// k = n−l−1 最大也就是 5，浮点误差远小于可视化能看出的程度。
double laguerre(int k, int alpha, double x)
{
    if (k <= 0) return 1.0;

    double lm1 = 1.0;                  // L_0
    double l   = 1.0 + alpha - x;      // L_1
    for (int i = 1; i < k; ++i)
    {
        const double next = ((2.0 * i + 1.0 + alpha - x) * l - (i + alpha) * lm1)
                            / static_cast<double>(i + 1);
        lm1 = l;
        l   = next;
    }
    return l;
}

// 氢样径向函数 R_nl(r)，a₀ = 1、Z = 1，单位 a₀^(-3/2)。
double radialR(int n, int l, double r)
{
    if (n < 1 || l < 0 || l > n - 1) return 0.0;

    const double x      = 2.0 * r / static_cast<double>(n);
    const double prefac = std::sqrt(std::pow(2.0 / n, 3) *
                                    factorial(n - l - 1) /
                                    (2.0 * n * factorial(n + l)));

    // 注意 pow(x, l)：l = 0 时 x 可能为 0，0⁰ 在 C++ 里定义为 1，正好是我们要的
    return prefac * std::exp(-r / static_cast<double>(n))
                  * std::pow(x, l)
                  * laguerre(n - l - 1, 2 * l + 1, x);
}

// 实球谐函数 Y(单位方向)。九个式子和 Day 19 的 shape.cpp 是同一套，
// 只是那边只用 |Y| 当半径，这边要带符号（相位染色靠它）。
double angularY(ShapeKind kind, double x, double y, double z)
{
    static const double kS  = 0.5 / std::sqrt(kPi);                        // l = 0
    static const double kP1 = std::sqrt(3.0 / (4.0 * kPi));                // l = 1
    static const double kD2 = std::sqrt(15.0 / (4.0 * kPi));               // d_xy 等
    static const double kD3 = std::sqrt(15.0 / (16.0 * kPi));              // d_x2-y2
    static const double kD4 = std::sqrt(5.0 / (16.0 * kPi));               // d_z2

    switch (kind)
    {
    case ShapeKind::S:     return kS;
    case ShapeKind::Px:    return kP1 * x;
    case ShapeKind::Py:    return kP1 * y;
    case ShapeKind::Pz:    return kP1 * z;
    case ShapeKind::Dxy:   return kD2 * x * y;
    case ShapeKind::Dyz:   return kD2 * y * z;
    case ShapeKind::Dxz:   return kD2 * x * z;
    case ShapeKind::Dx2Y2: return kD3 * (x * x - y * y);
    case ShapeKind::Dz2:   return kD4 * (3.0 * z * z - 1.0);
    default:               return 0.0;
    }
}

// 在球面上扫一遍，拿到 |Y|² 的最大值（拒绝采样要上界）和 ⟨cos²θ⟩ 的积分值。
// 后者与径向完全无关，是检验「方向抽得对不对」的独立量。
struct AngularProbe
{
    double maxSquared = 0.0;   // 必须从 0 起扫：|Y|² 的峰值只有 0.08 ~ 0.40，
                               // 从 1.0 起会让上界虚高十几倍，拒绝效率变成 1/13
    double meanCos2   = 0.0;
};

AngularProbe probeAngular(ShapeKind kind)
{
    const int nu   = 600;   // cosθ 方向
    const int nphi = 600;   // 方位角

    std::vector<double> cosPhi(static_cast<std::size_t>(nphi));
    std::vector<double> sinPhi(static_cast<std::size_t>(nphi));
    for (int j = 0; j < nphi; ++j)
    {
        const double phi = (j + 0.5) * (2.0 * kPi / nphi);
        cosPhi[static_cast<std::size_t>(j)] = std::cos(phi);
        sinPhi[static_cast<std::size_t>(j)] = std::sin(phi);
    }

    AngularProbe out;
    double sumCos2 = 0.0, sum = 0.0;

    for (int i = 0; i < nu; ++i)
    {
        const double u = -1.0 + (i + 0.5) * (2.0 / nu);
        const double s = std::sqrt(std::max(0.0, 1.0 - u * u));

        for (int j = 0; j < nphi; ++j)
        {
            const double yv = angularY(kind, s * cosPhi[static_cast<std::size_t>(j)],
                                             s * sinPhi[static_cast<std::size_t>(j)], u);
            const double y2 = yv * yv;

            if (y2 > out.maxSquared) out.maxSquared = y2;
            sumCos2 += u * u * y2;
            sum     += y2;
        }
    }

    if (out.maxSquared <= 0.0) out.maxSquared = 1.0;   // 理论上不会发生，兜底防除零
    out.meanCos2 = (sum > 0.0) ? sumCos2 / sum : 0.0;
    return out;
}

// 径向采样器：把 r²R² 积成 CDF 表，靠反演抽 r。
struct RadialSampler
{
    int    n = 1;
    int    l = 0;
    int    bins = 4096;
    double rMax = 1.0;
    double dr   = 1.0;
    double total = 1.0;            // [0, rMax] 上的未归一化积分
    double tailFraction = 0.0;     // rMax 之外被丢掉的概率（估计值）
    std::vector<double> cdf;

    double density(double r) const
    {
        const double R = radialR(n, l, r);
        return r * r * R * R;
    }

    // rMax 从 20n+10 起，不够长就 ×1.5 重来。
    // 判据：p(r) 过峰值之后按 e^(−2r/n) 衰减，尾部积分 ≈ p(rMax)·n/2；
    // 多项式因子会把它抬高一些，所以用 (n+2) 保守估计。
    void build(int n_, int l_)
    {
        n = n_;
        l = l_;
        rMax = 20.0 * n + 10.0;

        for (int attempt = 0; attempt < 12; ++attempt)
        {
            dr = rMax / bins;
            cdf.assign(static_cast<std::size_t>(bins + 1), 0.0);

            double prev = density(0.0);
            for (int i = 1; i <= bins; ++i)
            {
                const double cur = density(i * dr);
                cdf[static_cast<std::size_t>(i)] =
                    cdf[static_cast<std::size_t>(i - 1)] + 0.5 * (prev + cur) * dr;
                prev = cur;
            }

            total = cdf[static_cast<std::size_t>(bins)];
            tailFraction = (total > 0.0) ? density(rMax) * (n + 2.0) / total : 1.0;

            if (tailFraction < 1e-9) break;
            rMax *= 1.5;
        }

        for (double& v : cdf) v /= total;
    }

    // 反演：二分找到第一个 cdf > u 的格子，格内线性插值。
    // 抽到的是「这张表定义的分布」，而不是解析式——两者差多少由 integrateFine 单独核。
    int locate(double u) const
    {
        int lo = 0, hi = bins;
        while (lo < hi)
        {
            const int mid = (lo + hi) / 2;
            if (cdf[static_cast<std::size_t>(mid)] <= u) lo = mid + 1;
            else hi = mid;
        }
        return std::max(lo, 1);
    }

    double sample(double u) const
    {
        const int    i    = locate(u);
        const double span = cdf[static_cast<std::size_t>(i)] - cdf[static_cast<std::size_t>(i - 1)];
        const double f    = (span > 1e-300) ? (u - cdf[static_cast<std::size_t>(i - 1)]) / span : 0.0;
        return (static_cast<double>(i - 1) + std::min(std::max(f, 0.0), 1.0)) * dr;
    }

    // 分位数：cdf(r) = q 的 r（r98 用它）
    double radiusAt(double q) const { return sample(q); }

    double cdfAt(double r) const
    {
        if (r <= 0.0) return 0.0;
        if (r >= rMax) return 1.0;

        const double t = r / dr;
        const int    i = static_cast<int>(t);
        if (i + 1 > bins) return 1.0;
        const double f = t - static_cast<double>(i);
        return cdf[static_cast<std::size_t>(i)] +
               (cdf[static_cast<std::size_t>(i + 1)] - cdf[static_cast<std::size_t>(i)]) * f;
    }

    // 独立算一遍 CDF（Simpson，2000 段），用来检查查表本身准不准。
    // 它和 cdf[] 不共享任何离散化假设，所以能验出「格子太粗」这类问题。
    double integrateFine(double r) const
    {
        if (r <= 0.0) return 0.0;

        const int    steps = 2000;
        const double h     = r / steps;
        double       s     = density(0.0) + density(r);
        for (int i = 1; i < steps; ++i)
            s += density(i * h) * ((i % 2 != 0) ? 4.0 : 2.0);
        return s * h / 3.0 / total;
    }
};

// 找出 R_nl(r) 在 (0, rMax] 上的全部零点（= 径向节面），返回它们的半径。
// 个数应该等于 n − l − 1 —— 拉盖尔递推里错一个符号、错一个系数，
// 节面个数或位置立刻就不对了，所以这是最省事的验算方式。
//
// 顺带把位置也算出来：2s 的唯一节点在 r = 2.000 a₀，
// 3s 的两个在 1.902 与 7.098 a₀，这些是可以直接对课本的硬数字。
std::vector<double> findRadialNodes(int n, int l, double rMax)
{
    std::vector<double> nodes;

    const int steps = std::max(4000, static_cast<int>(rMax * 200.0));

    double rPrev = rMax * 1e-4;
    double prev  = radialR(n, l, rPrev);

    for (int i = 1; i <= steps; ++i)
    {
        const double r   = rMax * (1e-4 + (1.0 - 1e-4) * i / steps);
        const double cur = radialR(n, l, r);

        if (prev * cur < 0.0)   // 严格小于：正好取到 0 不算符号变化
        {
            // 线性插值定位零点。采样间距 ~0.005 a₀，插值误差远小于显示的小数位。
            const double t = std::fabs(prev) / (std::fabs(prev) + std::fabs(cur));
            nodes.push_back(rPrev + (r - rPrev) * t);
        }

        prev  = cur;
        rPrev = r;
    }
    return nodes;
}

// 剖面落在哪个平面上。选错平面会把整个轨道切成「一片空白」——
// 例如 d_xy 的密度在 x-z 平面上处处为 0（那里 xy = 0），必须看 x-y。
void sliceAxes(ShapeKind kind, int* h, int* v)
{
    switch (kind)
    {
    case ShapeKind::Py:    *h = 1; *v = 2; return;   // y-z
    case ShapeKind::Dxy:   *h = 0; *v = 1; return;   // x-y
    case ShapeKind::Dyz:   *h = 1; *v = 2; return;   // y-z
    case ShapeKind::Dx2Y2: *h = 0; *v = 1; return;   // x-y
    case ShapeKind::Dz2:   *h = 1; *v = 2; return;   // y-z
    default:               *h = 0; *v = 2; return;   // x-z
    }
}

void appendSlice(std::ostringstream& out, int n, ShapeKind kind, float radius)
{
    const int cols = 51;
    const int rows = 21;

    int hAxis = 0, vAxis = 2;
    sliceAxes(kind, &hAxis, &vAxis);
    static const char* kAxisName[3] = { "x", "y", "z" };
    const int other = 3 - hAxis - vAxis;

    std::vector<double> dens(static_cast<std::size_t>(cols * rows), 0.0);
    double maxDensity = 0.0;

    for (int r = 0; r < rows; ++r)
    {
        const double v = radius * (1.0 - 2.0 * r / (rows - 1.0));
        for (int c = 0; c < cols; ++c)
        {
            const double h = radius * (-1.0 + 2.0 * c / (cols - 1.0));

            double p[3] = { 0.0, 0.0, 0.0 };
            p[hAxis] = h;
            p[vAxis] = v;
            p[other] = 0.0;

            const double d = probabilityDensity(n, kind, p[0], p[1], p[2]);
            dens[static_cast<std::size_t>(r * cols + c)] = d;
            if (d > maxDensity) maxDensity = d;
        }
    }

    out << "  slice " << kAxisName[hAxis] << "-" << kAxisName[vAxis]
        << " (" << kAxisName[other] << "=0), radius "
        << static_cast<int>(radius + 0.5f) << " a0, levels (rho/rhomax)^(1/4)\n";

    // 四级根压一下动态范围：中心的密度比边缘大好几个数量级，
    // 直接用线性分级的话，除了最里面一格以外全是空白。
    static const char kLevels[] = " .:-=+*#%@";
    const int kLevelCount = 10;

    std::ostringstream os;
    os << std::fixed << std::setprecision(1);

    for (int r = 0; r < rows; ++r)
    {
        const double v = radius * (1.0 - 2.0 * r / (rows - 1.0));
        os << "    ";
        os << std::setw(6) << v << " |";
        for (int c = 0; c < cols; ++c)
        {
            const double d = dens[static_cast<std::size_t>(r * cols + c)];
            int idx = 0;
            if (maxDensity > 0.0 && d > 0.0)
                idx = static_cast<int>(std::lround(std::pow(d / maxDensity, 0.25) *
                                                   (kLevelCount - 1)));
            idx = std::min(std::max(idx, 0), kLevelCount - 1);
            os << kLevels[idx];
        }
        os << "\n";
    }

    out << os.str();
    out << "            +" << std::string(static_cast<std::size_t>(cols), '-') << "\n";
    out << "               " << kAxisName[hAxis] << " (horizontal)\n";
}

// 一维径向剖面：把一个球壳压成一行字符。
// 二维剖面里径向节面只是一圈很薄的暗环，格子一粗就被抹掉了；
// 而「有 n−l−1 个零」正是今天最该被看见的东西，所以单独再画一行。
void appendRadialProfile(std::ostringstream& out, int n, int l, double rMax)
{
    const int cols = 61;

    std::vector<double> p(static_cast<std::size_t>(cols), 0.0);
    double peak = 0.0, rPeak = 0.0;

    for (int c = 0; c < cols; ++c)
    {
        const double r = rMax * (c + 0.5) / cols;
        const double R = radialR(n, l, r);
        p[static_cast<std::size_t>(c)] = r * r * R * R;

        if (p[static_cast<std::size_t>(c)] > peak)
        {
            peak  = p[static_cast<std::size_t>(c)];
            rPeak = r;
        }
    }

    static const char kLevels[] = " .:-=+*#%@";

    std::string line;
    line.reserve(static_cast<std::size_t>(cols));
    for (int c = 0; c < cols; ++c)
    {
        int idx = 0;
        if (peak > 0.0 && p[static_cast<std::size_t>(c)] > 0.0)
            idx = static_cast<int>(std::lround(
                      std::pow(p[static_cast<std::size_t>(c)] / peak, 0.25) * 9.0));
        idx = std::min(std::max(idx, 0), 9);
        line.push_back(kLevels[idx]);
    }

    out << "  radial P(r) = r^2 R^2(r)   0 .. " << std::fixed << std::setprecision(1)
        << rMax << " a0   peak at " << rPeak << " a0\n";
    out << "    " << line << "\n";
}

std::string formatPair(double sample, double analytic, const char* unit){
    std::ostringstream os;
    os << std::fixed << std::setprecision(4) << sample
       << " / " << analytic;
    if (unit != nullptr) os << ' ' << unit;

    if (std::fabs(analytic) > 1e-12)
    {
        const double rel = std::fabs(sample - analytic) / std::fabs(analytic) * 100.0;
        os << "  (" << std::setprecision(2) << rel << "%)";
    }
    return os.str();
}

} // namespace

// ==================== 公开接口 ====================

int angularL(ShapeKind kind)
{
    switch (kind)
    {
    case ShapeKind::S:
        return 0;
    case ShapeKind::Px:
    case ShapeKind::Py:
    case ShapeKind::Pz:
        return 1;
    case ShapeKind::Dxy:
    case ShapeKind::Dyz:
    case ShapeKind::Dxz:
    case ShapeKind::Dx2Y2:
    case ShapeKind::Dz2:
        return 2;
    default:
        return 0;
    }
}

int radialNodeCount(int n, int l)
{
    return std::max(0, n - l - 1);
}

bool validOrbital(int n, ShapeKind kind, std::string* why)
{
    const auto fail = [why](const std::string& msg) {
        if (why != nullptr) *why = msg;
        return false;
    };

    if (n < 1 || n > 6)
        return fail("主量子数 n 必须是 1..6（再高的壳层这个窗口也看不清）");

    const int k = static_cast<int>(kind);
    if (k < 0 || k >= SHAPE_COUNT)
        return fail("角向形状无效");

    const int l = angularL(kind);
    if (l > n - 1)
        return fail("不存在 " + std::to_string(n) + std::string(shapeName(kind)) +
                    "：n = " + std::to_string(n) + " 最多到 l = " + std::to_string(n - 1));

    return true;
}

double probabilityDensity(int n, ShapeKind kind, double x, double y, double z)
{
    if (n < 1) return 0.0;

    const double r = std::sqrt(x * x + y * y + z * z);

    // r → 0 时方向没有定义，但 l = 0 的密度是各向同性的、l > 0 的密度趋于 0，
    // 所以随便给个单位方向都不影响结果。
    double ux = 0.0, uy = 0.0, uz = 1.0;
    if (r > 1e-12)
    {
        ux = x / r;
        uy = y / r;
        uz = z / r;
    }

    const double R = radialR(n, angularL(kind), r);
    const double Y = angularY(kind, ux, uy, uz);
    return R * R * Y * Y;
}

void buildCloud(int n, ShapeKind kind, int count, unsigned seed,
                std::vector<CloudPoint>& out, CloudStats& stats)
{
    stats = CloudStats{};
    out.clear();

    if (count < 1 || !validOrbital(n, kind, nullptr)) return;

    const int l = angularL(kind);

    RadialSampler radial;
    radial.build(n, l);

    const AngularProbe probe = probeAngular(kind);

    // 拒绝采样需要一个**上界**：网格扫描出来的最大值永远略小于真最大值
    // （格子不会正巧落在峰顶），所以乘 1.01。上界偏大只是多拒几次，
    // 偏小会悄悄截断分布——那种错误在图上完全看不出来，只能靠 ⟨cos²θ⟩ 抓。
    const double yMax2 = probe.maxSquared * 1.01;

    Rng rng(seed);

    out.reserve(static_cast<std::size_t>(count));
    std::vector<double> radii(static_cast<std::size_t>(count), 0.0);

    double sumR = 0.0, sumR2 = 0.0, sumCos2 = 0.0;

    for (int i = 0; i < count; ++i)
    {
        // ---- 1. 半径：反演径向 CDF ----
        const double r = radial.sample(rng.next());
        radii[static_cast<std::size_t>(i)] = r;
        sumR  += r;
        sumR2 += r * r;

        // ---- 2. 方向：球面均匀撒 + 按 |Y|² 拒绝 ----
        double ux = 0.0, uy = 0.0, uz = 1.0, yv = 0.0;
        for (;;)
        {
            const double u   = 2.0 * rng.next() - 1.0;
            const double phi = 2.0 * kPi * rng.next();
            const double s   = std::sqrt(std::max(0.0, 1.0 - u * u));

            ux = s * std::cos(phi);
            uy = s * std::sin(phi);
            uz = u;
            yv = angularY(kind, ux, uy, uz);

            ++stats.dirTrials;
            if (rng.next() * yMax2 <= yv * yv) break;
        }
        sumCos2 += uz * uz;

        // ---- 3. 记录点与相位 ----
        const double R = radialR(n, l, r);

        CloudPoint p;
        p.x     = static_cast<float>(r * ux);
        p.y     = static_cast<float>(r * uy);
        p.z     = static_cast<float>(r * uz);
        p.phase = (R * yv >= 0.0) ? 1.0f : -1.0f;
        out.push_back(p);
    }

    // ---- 统计自检 ----
    const double dn = static_cast<double>(count);

    stats.points         = count;
    stats.meanR          = sumR / dn;
    stats.meanR2         = sumR2 / dn;
    stats.meanCos2       = sumCos2 / dn;
    stats.analyticMeanCos2 = probe.meanCos2;

    // 闭式解（Z = 1，单位 a₀）：
    //   ⟨r⟩  = (3n² − l(l+1)) / 2
    //   ⟨r²⟩ = n²(5n² + 1 − 3l(l+1)) / 2
    // 它们是从 |ψ|² 直接积出来的，和采样流程没有共同假设，所以是独立的对账依据。
    stats.analyticMeanR  = 0.5 * (3.0 * n * n - l * (l + 1));
    stats.analyticMeanR2 = 0.5 * n * n * (5.0 * n * n + 1.0 - 3.0 * l * (l + 1));

    stats.radialNodes         = static_cast<int>(findRadialNodes(n, l, radial.rMax).size());
    stats.expectedRadialNodes = radialNodeCount(n, l);
    stats.rMax          = static_cast<float>(radial.rMax);
    stats.r98           = static_cast<float>(radial.radiusAt(0.98));
    stats.tailFraction  = radial.tailFraction;

    // Kolmogorov–Smirnov：经验 CDF 与理论 CDF 的最大偏差。
    // 若半径真是从这张表抽的，偏差应在 1.36/√N 附近（95% 显著性）。
    std::sort(radii.begin(), radii.end());

    double ks = 0.0;
    for (int i = 0; i < count; ++i)
    {
        const double f = radial.cdfAt(radii[static_cast<std::size_t>(i)]);
        ks = std::max(ks, std::fabs(f - static_cast<double>(i + 1) / dn));
        ks = std::max(ks, std::fabs(f - static_cast<double>(i) / dn));
    }
    stats.ksDeviation = ks;
    stats.ksCritical  = 1.36 / std::sqrt(dn);
}

std::string cloudLabel(int n, ShapeKind kind)
{
    return std::to_string(n) + shapeName(kind);
}

bool parseOrbitalSpec(const std::string& text, int* n, ShapeKind* kind)
{
    // 前导数字 = 主量子数，剩下的部分交给 parseShapeKind（它容忍下划线、^、连字符）
    std::size_t i = 0;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;

    if (i == 0 || i >= text.size()) return false;

    const int nn = std::atoi(text.substr(0, i).c_str());

    ShapeKind k = ShapeKind::S;
    if (!parseShapeKind(text.substr(i), &k)) return false;
    if (!validOrbital(nn, k, nullptr)) return false;

    if (n != nullptr) *n = nn;
    if (kind != nullptr) *kind = k;
    return true;
}

Rgb phaseColor(float phase)
{
    // ψ > 0 暖红，ψ < 0 冷蓝。颜色只是「符号」，不是「正负电荷」。
    if (phase >= 0.0f) return Rgb{ 1.00f, 0.42f, 0.36f };
    return Rgb{ 0.32f, 0.62f, 1.00f };
}

std::string cloudSummaryLine(int n, ShapeKind kind, int count)
{
    std::vector<CloudPoint> points;
    CloudStats              stats;
    buildCloud(n, kind, count, 1u, points, stats);

    std::ostringstream os;
    os << "  " << std::left << std::setw(7) << cloudLabel(n, kind) << std::right
       << " pts " << std::setw(5) << stats.points
       << "  <r> " << std::fixed << std::setprecision(3) << stats.meanR
       << "/" << stats.analyticMeanR
       << "  <r2> " << std::setprecision(2) << stats.meanR2
       << "/" << stats.analyticMeanR2
       << "  cos2 " << std::setprecision(3) << stats.meanCos2
       << "/" << stats.analyticMeanCos2
       << "  r98 " << std::setprecision(1) << stats.r98
       << "  nodes " << stats.radialNodes << "/" << stats.expectedRadialNodes;
    return os.str();
}

std::string describeCloud(int n, ShapeKind kind, int count)
{
    std::ostringstream os;

    std::string why;
    if (!validOrbital(n, kind, &why))
    {
        os << "无效轨道: " << why << "\n";
        return os.str();
    }

    std::vector<CloudPoint> points;
    CloudStats              stats;
    buildCloud(n, kind, count, 1u, points, stats);

    const int l = angularL(kind);

    os << cloudLabel(n, kind) << "   n = " << n << ", l = " << l
       << ", angular " << shapeName(kind) << "\n";

    // 表格检查：查表 CDF 与 Simpson 独立积分的最大差
    RadialSampler radial;
    radial.build(n, l);
    double tableError = 0.0;
    for (int c = 1; c <= 25; ++c)
    {
        const double r = radial.rMax * c / 26.0;
        tableError = std::max(tableError,
                              std::fabs(radial.integrateFine(r) - radial.cdfAt(r)));
    }

    os << "  points     : " << stats.points
       << "   (dir trials " << stats.dirTrials
       << ", " << std::fixed << std::setprecision(2)
       << (stats.points > 0 ? static_cast<double>(stats.dirTrials) / stats.points : 0.0)
       << " per point)\n";

    os << "  <r>        : " << formatPair(stats.meanR, stats.analyticMeanR, "a0") << "\n";
    os << "  <r2>       : " << formatPair(stats.meanR2, stats.analyticMeanR2, "a0^2") << "\n";
    os << "  <cos2>     : " << formatPair(stats.meanCos2, stats.analyticMeanCos2, nullptr) << "\n";

    os << "  r nodes    : " << stats.radialNodes
       << "   (expect n-l-1 = " << stats.expectedRadialNodes << ")";

    const std::vector<double> nodeRadii = findRadialNodes(n, l, radial.rMax);
    if (!nodeRadii.empty())
    {
        os << "   at r =";
        bool first = true;
        for (double r : nodeRadii)
        {
            os << (first ? " " : ", ") << std::fixed << std::setprecision(2) << r;
            first = false;
        }
        os << " a0";
    }
    os << "\n";

    os << "  KS dev     : " << std::fixed << std::setprecision(4) << stats.ksDeviation
       << "   (95% bound " << stats.ksCritical << ")  "
       << (stats.ksDeviation <= stats.ksCritical ? "ok" : "SUSPICIOUS") << "\n";

    os << "  radial cdf : max |table - simpson| = "
       << std::scientific << std::setprecision(2) << tableError << "\n";

    os << "  range      : rMax " << std::fixed << std::setprecision(1) << stats.rMax
       << " a0   r98 " << stats.r98 << " a0   truncated "
       << std::scientific << std::setprecision(1) << stats.tailFraction << "\n";

    os << "\n  rho = |R_nl(r) * Y(dir)|^2     radial part : hydrogenic, Z = 1, a0 = 1\n";
    os << "                                angular part: " << shapeFormula(kind) << "\n\n";

    appendRadialProfile(os, n, l, stats.rMax);
    os << "\n";

    appendSlice(os, n, kind, stats.r98);

    return os.str();
}

} // namespace orbital
