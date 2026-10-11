// src/molecule.h
// 分子的数据结构 + 甲烷数据。
// 第 9 天：改用 CPK 配色，并针对深色背景做亮度适配。

#ifndef MOLECULE_H
#define MOLECULE_H

#include <glm/glm.hpp>
#include <string>
#include <vector>

// 一个原子
struct Atom
{
    std::string element;   // 元素符号
    glm::vec3   position;  // 位置
};

// 一条化学键
struct Bond
{
    int a;
    int b;
};

// 一个分子
struct Molecule
{
    std::vector<Atom> atoms;
    std::vector<Bond> bonds;
};

// 元素的显示参数
struct ElementStyle
{
    float     ballRadius;   // 球棍模型的显示半径（艺术化，较小）
    float     vdwRadius;    // 范德华半径（真实，用于空间填充模型）
    glm::vec3 color;        // CPK 配色（已针对深色背景做亮度适配）
};

// 按元素符号取显示参数
//
// 配色说明：
//   采用 CPK 配色方案的"色相"，但整体提高了亮度、降低了饱和度。
//   原因：标准 CPK 里碳是深灰(0.2~0.3)，在深色背景下会糊成一团黑，
//        所以这里统一提亮，保证在深蓝黑背景上仍然清晰可辨。
//
// 半径说明：
//   ballRadius —— 球棍模型半径，艺术化调小，便于看清骨架
//   vdwRadius  —— 真实范德华半径（埃 Å）
inline ElementStyle getElementStyle(const std::string& element)
{
    // 氢：白
    if (element == "H") return { 0.25f, 1.20f, glm::vec3(0.94f, 0.94f, 0.96f) };
    // 碳：深灰（CPK 原色就是深灰；这里取一个"够暗但仍能与背景分离"的值）
    //   注意：绕序修复后球面能正常接受光照，所以不再需要第 9 天初期那种提亮补偿。
    //   但不能调到 0.30 以下，否则最暗处会低于背景色、轮廓糊掉。
    if (element == "C") return { 0.40f, 1.70f, glm::vec3(0.36f, 0.36f, 0.40f) };
    // 氮：蓝
    if (element == "N") return { 0.38f, 1.55f, glm::vec3(0.30f, 0.44f, 0.95f) };
    // 氧：红
    if (element == "O") return { 0.38f, 1.52f, glm::vec3(0.94f, 0.28f, 0.22f) };
    // 氟：绿
    if (element == "F") return { 0.36f, 1.47f, glm::vec3(0.35f, 0.85f, 0.35f) };
    // 硫：黄
    if (element == "S") return { 0.42f, 1.80f, glm::vec3(0.95f, 0.85f, 0.25f) };
    // 磷：橙
    if (element == "P") return { 0.42f, 1.80f, glm::vec3(0.95f, 0.60f, 0.20f) };
    // 氯：绿（比氟稍暗）
    if (element == "Cl") return { 0.40f, 1.75f, glm::vec3(0.25f, 0.70f, 0.30f) };
    // 兜底：未知元素用中性灰
    return { 0.35f, 1.50f, glm::vec3(0.62f, 0.62f, 0.65f) };
}

// 甲烷 CH4：1 个碳在原点，4 个氢指向正四面体顶点（键角 109.47°）
inline Molecule makeMethane()
{
    Molecule mol;
    const float k = 1.0f / 1.7320508f;    // 1/√3
    const float bondLength = 1.09f;       // C-H 键长（埃）

    mol.atoms.push_back({ "C", glm::vec3(0.0f, 0.0f, 0.0f) });

    const glm::vec3 dirs[4] = {
        glm::vec3(+k, +k, +k),
        glm::vec3(+k, -k, -k),
        glm::vec3(-k, +k, -k),
        glm::vec3(-k, -k, +k)
    };

    for (int i = 0; i < 4; ++i)
    {
        mol.atoms.push_back({ "H", dirs[i] * bondLength });
        mol.bonds.push_back({ 0, i + 1 });
    }

    return mol;
}

#endif // MOLECULE_H
