// src/orbital/element.h
// Atomotheca 数据层：亚层 / 离子 / 元素 / 元素数据库 的声明。
//
// 分层铁律（Prompt 第 7 节）：
//   本文件与 element.cpp 都【不允许】include <glad/glad.h>、<GLFW/glfw3.h>。
//   数据层不碰任何 OpenGL 状态，换渲染后端时这个文件一行都不用改。
//
// 头文件只放声明，实现全部在 element.cpp（铁律 1）。

#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace orbital
{

// ============================================================
//  亚层（subshell）
//  一个亚层用三个量子数就能唯一确定：n、l、电子数。
// ============================================================
struct Subshell
{
    int n         = 1;   // 主量子数：第几电子层，1..7
    int l         = 0;   // 角量子数：0=s  1=p  2=d  3=f
    int electrons = 0;   // 该亚层上住了几个电子
};

// 一个亚层最多装几个电子：2 × (2l + 1)
//   s: 2×(1)=2   p: 2×(3)=6   d: 2×(5)=10   f: 2×(7)=14
int subshellCapacity(int l);

// 0→'s' 1→'p' 2→'d' 3→'f'；超出范围返回 '?'
char subshellLetter(int l);

// 亚层的显示名，例如 n=3、l=2 → "3d"
std::string subshellName(const Subshell& s);

// 该亚层是否"住满"
bool isFull(const Subshell& s);

// ------------------------------------------------------------
//  两种排序，别搞混
// ------------------------------------------------------------
// 书写顺序（Aufbau 填充完之后教科书上的写法）：先按 n，再按 l
//   1s 2s 2p 3s 3p 3d 4s   ← 注意 3d 写在 4s 前面
// 能级顺序（真实的能量高低，Madelung 规则 n+l 小者先填）：
//   1s 2s 2p 3s 3p 4s 3d   ← 注意 4s 能量比 3d 低，所以先填满
// 第 17 天画能级图要用能级顺序，第 16 天显示排布式要用书写顺序。
std::vector<Subshell> sortByShell(std::vector<Subshell> subshells);
std::vector<Subshell> sortByEnergy(std::vector<Subshell> subshells);

// 把亚层列表渲染成排布式，跳过电子数为 0 的亚层：
//   {1s2, 2s2, 2p4} → "1s2 2s2 2p4"
// 纯 ASCII，不含上下标：ImGui 默认字体没有 ¹²³ 这些字形。
// 内部会先按书写顺序排一遍，所以传进来的顺序无所谓。
std::string formatConfig(const std::vector<Subshell>& subshells);

// 同上，但【保持传入顺序】不做排序。
// 传给 sortByEnergy() 的结果，就能得到按能量高低的填充次序（4s 在 3d 之前）。
std::string formatConfigOrdered(const std::vector<Subshell>& subshells);

// 电子总数
int countElectrons(const std::vector<Subshell>& subshells);

// ============================================================
//  离子
// ============================================================
struct Ion
{
    int charge = 0;                        // +2 = 失去 2 个电子；-2 = 得到 2 个
    std::vector<Subshell> subshells;       // 按书写顺序排好

    int         electronCount() const { return countElectrons(subshells); }
    std::string configString()  const { return formatConfig(subshells); }
    std::string label(const std::string& symbol) const;   // "Fe2+" / "O2-"
};

// ============================================================
//  元素
// ============================================================
struct Element
{
    std::string symbol;        // 元素符号，如 "Fe"
    std::string name;          // 英文名，如 "Iron"
    std::string nameZh;        // 中文名，如 "铁"（数据层先存着，UI 暂无中文字体）
    int    atomicNumber = 0;   // 原子序数 Z = 质子数 = 中性原子电子数
    double atomicMass   = 0.0; // 相对原子质量
    int    period       = 0;   // 周期（第几行）
    int    group        = 0;   // 族（第几列），1..18
    std::string category;      // "nonmetal" / "noble-gas" / ...
    std::array<float, 3> cpkColor {{ 1.0f, 1.0f, 1.0f }};

    std::vector<Subshell> subshells;         // 中性原子，书写顺序
    std::vector<int>      commonIonCharges;  // 常见离子电荷，如 Fe → {2, 3}

    int         electronCount() const { return countElectrons(subshells); }
    std::string configString()  const { return formatConfig(subshells); }

    // 能级顺序的副本，第 17 天画能级图用
    std::vector<Subshell> energyOrderedSubshells() const { return sortByEnergy(subshells); }

    // 按电荷推算离子排布。
    //   阳离子：从 n 最大的亚层开始剥电子，n 相同则先剥 l 大的
    //           （Fe: 3d6 4s2 失 3 个 → 先失 4s2，再失 3d 的 1 个 → 3d5）
    //   阴离子：按能级顺序找最后一个没住满的亚层，往里塞
    //           （O: 2p4 得 2 个 → 2p6）
    // 电荷为 0 时返回中性排布。
    Ion makeIon(int charge) const;
};

// ============================================================
//  元素数据库
// ============================================================
class ElementDatabase
{
public:
    // 从 JSON 文件加载。成功返回 true；失败返回 false 并把原因写进 *error。
    // 失败时数据库保持为空，不会留下半份数据。
    bool loadFromFile(const std::string& path, std::string* error);

    void clear();

    // 按符号查（区分大小写，数据文件里就是标准写法）
    const Element* find(const std::string& symbol) const;

    // 按原子序数查
    const Element* findByZ(int atomicNumber) const;

    const std::vector<Element>& elements() const { return elements_; }
    std::size_t                 size()     const { return elements_.size(); }
    bool                        empty()    const { return elements_.empty(); }

private:
    std::vector<Element> elements_;
};

// 生成一份适合打印到控制台的元素报告（--dump 用）。
std::string describe(const Element& e);

} // namespace orbital
