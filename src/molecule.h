// src/molecule.h
// 分子的数据结构，以及硬编码的甲烷 CH4 数据。
// 第 10 天会改成从 .mol / .pdb 文件读取。

#ifndef MOLECULE_H
#define MOLECULE_H

#include <glm/glm.hpp>
#include <string>
#include <vector>

// 一个原子
struct Atom
{
    std::string element;   // 元素符号，如 "C" / "H"
    glm::vec3   position;  // 位置（单位：埃 Å，但我们按"约 1"的尺度用）
};

// 一条化学键（存的是两个原子的下标）
struct Bond
{
    int a;   // 原子 A 的索引
    int b;   // 原子 B 的索引
};

// 一个分子
struct Molecule
{
    std::vector<Atom> atoms;
    std::vector<Bond> bonds;
};

// 元素显示参数（球棍模型的"艺术化"半径，不是真实原子半径）
struct ElementStyle
{
    float radius;              // 球棍模型里的显示半径
    glm::vec3 color;           // 颜色（第 9 天正式用）
};

// 按元素符号取显示样式
// 这不是真实原子半径，是按"好看"调整过的球棍模型半径
inline ElementStyle getElementStyle(const std::string& element)
{
    if (element == "C") return { 0.40f, glm::vec3(0.30f, 0.30f, 0.32f) };  // 深灰
    if (element == "H") return { 0.25f, glm::vec3(0.95f, 0.95f, 0.95f) };  // 近白
    if (element == "O") return { 0.38f, glm::vec3(0.90f, 0.15f, 0.15f) };  // 红
    if (element == "N") return { 0.38f, glm::vec3(0.15f, 0.30f, 0.90f) };  // 蓝
    // 兜底：未知元素用灰色中等大小
    return { 0.35f, glm::vec3(0.60f, 0.60f, 0.60f) };
}

// 构建甲烷 CH4 的球棍模型数据
// 结构：1 个碳在原点，4 个氢指向正四面体的四个顶点（键角 109.5°）
inline Molecule makeMethane()
{
    Molecule mol;

    // 正四面体的四个方向（归一化后）
    // 这三个数的平方和是 3，所以除以 sqrt(3) 归一化
    const float k = 1.0f / 1.7320508f;   // 1/√3

    // C-H 键长（埃 Å）
    const float bondLength = 1.09f;

    // 第 0 个原子：碳，在原点
    mol.atoms.push_back({ "C", glm::vec3(0.0f, 0.0f, 0.0f) });

    // 四个氢，位置 = 方向 × 键长
    const glm::vec3 dirs[4] = {
        glm::vec3(+k, +k, +k),
        glm::vec3(+k, -k, -k),
        glm::vec3(-k, +k, -k),
        glm::vec3(-k, -k, +k)
    };

    for (int i = 0; i < 4; ++i)
    {
        mol.atoms.push_back({ "H", dirs[i] * bondLength });
        // 每个氢都和碳（索引 0）成键
        mol.bonds.push_back({ 0, i + 1 });
    }

    return mol;
}

#endif // MOLECULE_H
