// src/molfile.h
// 第 10 天：分子文件解析器。
//   支持 .mol (MDL Molfile V2000) 和 .pdb (Protein Data Bank) 两种格式。
//   自己写，不依赖 Assimp 之类的重型库 —— 这两种格式都是纯文本，几百行就够。
//
// 两种格式的共同特点：都是"固定列宽"格式，必须按列切，不能按空格切。

#ifndef MOLFILE_H
#define MOLFILE_H

#include "molecule.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace molfile
{

// ============================================================
// 小工具
// ============================================================

// 去掉字符串首尾的空白字符
inline std::string trim(const std::string& s)
{
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// 把字符串右侧补齐到 n 个字符（不足部分补空格）
// 固定列宽格式必须这样做，否则 substr 会越界
inline void padRight(std::string& s, size_t n)
{
    if (s.size() < n)
        s.append(n - s.size(), ' ');
}

// 安全地取子串（越界返回空串）
inline std::string safeSub(const std::string& s, size_t pos, size_t len)
{
    if (pos >= s.size()) return "";
    return s.substr(pos, std::min(len, s.size() - pos));
}

// 元素符号规范化：
//   文件里可能写成 "C"、"CL"、"c"、"C " 各种形式，
//   统一成标准写法：首字母大写 + 第二个字母小写（如 "Cl"）
inline std::string normalizeElement(const std::string& raw)
{
    std::string s = trim(raw);
    if (s.empty()) return "C";

    std::string r;
    r += static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    if (s.size() >= 2 && std::isalpha(static_cast<unsigned char>(s[1])))
        r += static_cast<char>(std::tolower(static_cast<unsigned char>(s[1])));
    return r;
}

// ============================================================
// 共价半径表（用于按距离自动推断化学键）
// 单位：埃 Å。数据来自常见的共价半径表。
// ============================================================
inline float covalentRadius(const std::string& e)
{
    if (e == "H")  return 0.31f;
    if (e == "C")  return 0.76f;
    if (e == "N")  return 0.71f;
    if (e == "O")  return 0.66f;
    if (e == "F")  return 0.57f;
    if (e == "P")  return 1.07f;
    if (e == "S")  return 1.05f;
    if (e == "Cl") return 1.02f;
    if (e == "Br") return 1.20f;
    if (e == "I")  return 1.39f;
    return 0.77f;   // 未知元素给个中值
}

// ============================================================
// 自动推断化学键（当文件没有提供键表时使用）
//
// 原理：两个原子如果距离接近"两者共价半径之和"的 1.25 倍以内，
//       就认为它们成键。这是分子可视化的常用启发式。
//
// 复杂度 O(n²)，但只在加载时算一次，几千个原子也就几百万次比较。
// ============================================================
inline void inferBonds(Molecule& mol)
{
    const float TOLERANCE = 1.25f;   // 容忍系数：键长在不同分子里有波动

    mol.bonds.clear();
    const size_t n = mol.atoms.size();

    for (size_t i = 0; i < n; ++i)
    {
        for (size_t j = i + 1; j < n; ++j)
        {
            float d = glm::length(mol.atoms[i].position - mol.atoms[j].position);

            float rsum = covalentRadius(mol.atoms[i].element)
                       + covalentRadius(mol.atoms[j].element);

            if (d < rsum * TOLERANCE && d > 0.1f)   // 排除极端重合的情况
            {
                mol.bonds.push_back({ static_cast<int>(i), static_cast<int>(j) });
            }
        }
    }
}

// ============================================================
// 分子居中：把分子平移到以原点为中心
// 用"包围盒中心"而不是质心 —— 质心会被重原子拉偏
// ============================================================
inline void centerMolecule(Molecule& mol)
{
    if (mol.atoms.empty()) return;

    glm::vec3 minP( 1e30f);
    glm::vec3 maxP(-1e30f);
    for (const auto& a : mol.atoms)
    {
        minP = glm::min(minP, a.position);
        maxP = glm::max(maxP, a.position);
    }
    glm::vec3 c = (minP + maxP) * 0.5f;

    for (auto& a : mol.atoms)
        a.position -= c;
}

// ============================================================
// 分子"包围球"半径（含最外层原子的显示半径）
// 用来决定相机的初始距离
// ============================================================
inline float moleculeRadius(const Molecule& mol)
{
    float r = 0.0f;
    for (const auto& a : mol.atoms)
    {
        float atomR = getElementStyle(a.element).ballRadius;
        r = std::max(r, glm::length(a.position) + atomR);
    }
    return r;
}

// ============================================================
// 解析 .mol（MDL Molfile V2000）
// ============================================================
inline bool parseMol(const std::string& path, Molecule& out)
{
    std::ifstream f(path);
    if (!f.is_open())
    {
        std::cerr << "[错误] 无法打开文件: " << path << std::endl;
        return false;
    }

    std::string line;

    // ---------- 1. 跳过前 3 行（分子名 / 程序信息 / 注释）----------
    for (int i = 0; i < 3; ++i)
    {
        if (!std::getline(f, line))
        {
            std::cerr << "[错误] .mol 文件不完整（缺少头部）" << std::endl;
            return false;
        }
    }

    // ---------- 2. 计数行 ----------
    // 前 3 列 = 原子数，第 4~6 列 = 键数
    if (!std::getline(f, line))
    {
        std::cerr << "[错误] .mol 文件不完整（缺少计数行）" << std::endl;
        return false;
    }
    padRight(line, 6);

    int atomCount = 0;
    int bondCount = 0;
    try
    {
        atomCount = std::stoi(trim(safeSub(line, 0, 3)));
        bondCount = std::stoi(trim(safeSub(line, 3, 3)));
    }
    catch (const std::exception&)
    {
        std::cerr << "[错误] 无法解析计数行: " << line << std::endl;
        return false;
    }

    if (atomCount <= 0)
    {
        std::cerr << "[错误] 原子数为 0" << std::endl;
        return false;
    }

    // ---------- 3. 原子块 ----------
    out.atoms.reserve(atomCount);
    for (int i = 0; i < atomCount; ++i)
    {
        if (!std::getline(f, line))
        {
            std::cerr << "[错误] .mol 原子块提前结束（应为 "
                      << atomCount << " 个原子）" << std::endl;
            return false;
        }
        padRight(line, 34);   // 原子行至少需要 34 列

        try
        {
            float x = std::stof(trim(safeSub(line, 0, 10)));
            float y = std::stof(trim(safeSub(line, 10, 10)));
            float z = std::stof(trim(safeSub(line, 20, 10)));
            std::string el = normalizeElement(safeSub(line, 31, 3));

            out.atoms.push_back({ el, glm::vec3(x, y, z) });
        }
        catch (const std::exception&)
        {
            std::cerr << "[错误] 无法解析第 " << (i + 1)
                      << " 个原子行: " << line << std::endl;
            return false;
        }
    }

    // ---------- 4. 键块 ----------
    out.bonds.reserve(bondCount);
    for (int i = 0; i < bondCount; ++i)
    {
        if (!std::getline(f, line))
        {
            std::cerr << "[警告] .mol 键块提前结束" << std::endl;
            break;
        }
        padRight(line, 9);

        try
        {
            // ★ 注意：.mol 里原子编号是 1-based，转成我们的 0-based
            int a = std::stoi(trim(safeSub(line, 0, 3))) - 1;
            int b = std::stoi(trim(safeSub(line, 3, 3))) - 1;

            // 边界检查，防止损坏的文件导致越界访问
            if (a < 0 || b < 0 || a >= atomCount || b >= atomCount)
            {
                std::cerr << "[警告] 键的原子编号越界，已跳过: "
                          << (a + 1) << " - " << (b + 1) << std::endl;
                continue;
            }
            out.bonds.push_back({ a, b });
        }
        catch (const std::exception&)
        {
            std::cerr << "[警告] 无法解析键行，已跳过: " << line << std::endl;
        }
    }

    return true;
}

// ============================================================
// 从 PDB 的"原子名"推断元素符号
//
// PDB 的原子名有对齐惯例：
//   " CA " （前面有空格）→ 元素是 C（蛋白质里的 α 碳）
//   "CA  " （左对齐）    → 元素是 Ca（钙）
// 这个函数把这两种情况区分开。
// ============================================================
inline std::string elementFromAtomName(const std::string& raw4)
{
    if (raw4.size() < 2) return raw4.empty() ? "C" : normalizeElement(raw4);

    char c0 = raw4[0];
    char c1 = raw4[1];

    // 情况 1：第一个字符是数字（如 "1H" ）→ 元素是第二个字符
    if (std::isdigit(static_cast<unsigned char>(c0)))
        return normalizeElement(std::string(1, c1));

    // 情况 2：第一个字符是空格（如 " CA" ）→ 元素是第二个字符
    if (std::isspace(static_cast<unsigned char>(c0)))
        return normalizeElement(std::string(1, c1));

    // 情况 3：两个字符构成双字母元素（如 "CL"、"FE"，且左对齐）
    //         判断依据：第二个字符是大写字母，且整体是已知的两字母元素开头
    if (std::isalpha(static_cast<unsigned char>(c0)) &&
        std::isalpha(static_cast<unsigned char>(c1)))
    {
        std::string two = normalizeElement(raw4.substr(0, 2));
        // 只对已知的两字母元素才认，避免把 "CA"（α碳）误判成钙
        // 在蛋白质语境下，碳氢氮氧硫磷才是主力
        if (two == "Cl" || two == "Br" || two == "Na" || two == "Mg" ||
            two == "Zn" || two == "Fe" || two == "Ca" || two == "Mn" ||
            two == "Cu" || two == "Se")
            return two;
    }

    // 默认：取第一个字符
    return normalizeElement(std::string(1, c0));
}

// ============================================================
// 解析 .pdb（Protein Data Bank）
// ============================================================
inline bool parsePdb(const std::string& path, Molecule& out)
{
    std::ifstream f(path);
    if (!f.is_open())
    {
        std::cerr << "[错误] 无法打开文件: " << path << std::endl;
        return false;
    }

    std::string line;
    std::vector<std::string> conectLines;    // CONECT 行先存起来，稍后统一处理

    // PDB 里原子序号不一定连续，要记录"文件序号 → 我们的数组下标"
    std::map<int, int> serialToIndex;

    while (std::getline(f, line))
    {
        padRight(line, 80);

        std::string record = trim(safeSub(line, 0, 6));

        if (record == "ATOM" || record == "HETATM")
        {
            // 原子序号：第 7~11 列
            int serial = 0;
            try { serial = std::stoi(trim(safeSub(line, 6, 5))); }
            catch (...) { continue; }

            // 坐标：第 31~38 / 39~46 / 47~54 列
            float x = 0.0f, y = 0.0f, z = 0.0f;
            try
            {
                x = std::stof(trim(safeSub(line, 30, 8)));
                y = std::stof(trim(safeSub(line, 38, 8)));
                z = std::stof(trim(safeSub(line, 46, 8)));
            }
            catch (const std::exception&)
            {
                std::cerr << "[警告] 无法解析原子坐标，已跳过: "
                          << trim(line) << std::endl;
                continue;
            }

            // 元素符号：优先取第 77~78 列
            std::string el = normalizeElement(safeSub(line, 76, 2));

            // 如果那一列是空的（老文件常常不写），就从原子名推断
            if (trim(safeSub(line, 76, 2)).empty())
                el = elementFromAtomName(safeSub(line, 12, 4));

            int idx = static_cast<int>(out.atoms.size());
            out.atoms.push_back({ el, glm::vec3(x, y, z) });
            serialToIndex[serial] = idx;
        }
        else if (record == "CONECT")
        {
            conectLines.push_back(line);
        }
        else if (record == "END")
        {
            break;      // 遇到 END 就停
        }
        // 其他记录（HEADER / SEQRES / HELIX ...）一律忽略
    }

    if (out.atoms.empty())
    {
        std::cerr << "[错误] PDB 文件里没有找到 ATOM / HETATM 记录" << std::endl;
        return false;
    }

    // ---------- 处理 CONECT ----------
    // 格式：第 7~11 列是中心原子，之后每 5 列是一个相连原子
    for (const auto& cl : conectLines)
    {
        int center = 0;
        try { center = std::stoi(trim(safeSub(cl, 6, 5))); }
        catch (...) { continue; }

        auto itA = serialToIndex.find(center);
        if (itA == serialToIndex.end()) continue;

        for (size_t pos = 11; pos + 5 <= cl.size(); pos += 5)
        {
            std::string field = trim(safeSub(cl, pos, 5));
            if (field.empty()) continue;

            int other = 0;
            try { other = std::stoi(field); }
            catch (...) { continue; }

            auto itB = serialToIndex.find(other);
            if (itB == serialToIndex.end()) continue;

            int a = itA->second;
            int b = itB->second;
            if (a == b) continue;

            // CONECT 是双向冗余的（A 写 B、B 也写 A），要去重
            int lo = std::min(a, b);
            int hi = std::max(a, b);

            bool exists = false;
            for (const auto& bd : out.bonds)
            {
                if (bd.a == lo && bd.b == hi) { exists = true; break; }
            }
            if (!exists)
                out.bonds.push_back({ lo, hi });
        }
    }

    return true;
}

// ============================================================
// 统一入口：按扩展名分派，然后居中、补全键表
// ============================================================
inline bool loadMolecule(const std::string& path, Molecule& out)
{
    out.atoms.clear();
    out.bonds.clear();

    // 把路径转小写，用于判断扩展名
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    bool ok = false;
    bool isPdb = lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".pdb") == 0;

    if (isPdb)
        ok = parsePdb(path, out);
    else
        ok = parseMol(path, out);   // 默认按 .mol 处理

    if (!ok)
        return false;

    // 如果文件没有提供键表，按原子间距自动推断
    if (out.bonds.empty())
    {
        std::cout << "[提示] 文件中没有键表，正在按原子间距自动推断..."
                  << std::endl;
        inferBonds(out);
    }

    // 把分子平移到原点（用包围盒中心）
    centerMolecule(out);

    return true;
}

} // namespace molfile

#endif // MOLFILE_H
