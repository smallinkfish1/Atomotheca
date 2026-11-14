// src/orbital/element.cpp
// Atomotheca 数据层实现：JSON 解析、校验、电子排布、离子推算。
//
// 本文件唯一允许的外部依赖是 nlohmann/json（header-only）。
// 不 include 任何图形库。

#include "orbital/element.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>

using json = nlohmann::json;

namespace orbital
{

// ============================================================
//  亚层基础
// ============================================================

int subshellCapacity(int l)
{
    // 一个轨道装 2 个电子（自旋相反），l 亚层有 (2l+1) 个轨道
    return 2 * (2 * l + 1);
}

char subshellLetter(int l)
{
    switch (l)
    {
    case 0: return 's';
    case 1: return 'p';
    case 2: return 'd';
    case 3: return 'f';
    default: return '?';
    }
}

std::string subshellName(const Subshell& s)
{
    return std::to_string(s.n) + subshellLetter(s.l);
}

bool isFull(const Subshell& s)
{
    return s.electrons >= subshellCapacity(s.l);
}

// ============================================================
//  排序
// ============================================================

std::vector<Subshell> sortByShell(std::vector<Subshell> subshells)
{
    std::sort(subshells.begin(), subshells.end(),
              [](const Subshell& a, const Subshell& b)
              {
                  if (a.n != b.n) return a.n < b.n;
                  return a.l < b.l;
              });
    return subshells;
}

std::vector<Subshell> sortByEnergy(std::vector<Subshell> subshells)
{
    // Madelung 规则：n+l 小的能量低、先填；n+l 相同时 n 小的先填。
    // 这条规则能自动产出经典的交错：4s(4+0=4) 排在 3d(3+2=5) 之前。
    std::sort(subshells.begin(), subshells.end(),
              [](const Subshell& a, const Subshell& b)
              {
                  const int sa = a.n + a.l;
                  const int sb = b.n + b.l;
                  if (sa != sb) return sa < sb;
                  return a.n < b.n;
              });
    return subshells;
}

std::string formatConfigOrdered(const std::vector<Subshell>& subshells)
{
    std::ostringstream oss;
    bool first = true;
    for (const Subshell& s : subshells)
    {
        if (s.electrons <= 0) continue;          // 空亚层不写进排布式
        if (!first) oss << ' ';
        oss << subshellName(s) << s.electrons;
        first = false;
    }
    if (first) oss << "(empty)";                  // 一个电子都没有（如 H+）
    return oss.str();
}

std::string formatConfig(const std::vector<Subshell>& subshells)
{
    // 入参可能没排过序，这里先按书写顺序排一份副本，保证输出稳定
    return formatConfigOrdered(sortByShell(subshells));
}

int countElectrons(const std::vector<Subshell>& subshells)
{
    int sum = 0;
    for (const Subshell& s : subshells) sum += s.electrons;
    return sum;
}

// ============================================================
//  离子
// ============================================================

std::string Ion::label(const std::string& symbol) const
{
    std::string text = symbol;
    if (charge != 0)
    {
        // 惯例：电荷绝对值是 1 时不写数字，H+ 而不是 H1+
        if (charge > 1 || charge < -1)
            text += std::to_string(charge > 0 ? charge : -charge);
        text += (charge > 0) ? '+' : '-';
    }
    return text;
}

// Madelung 填充顺序的前若干个 (n, l)，n 最大算到 7 够用
static std::vector<std::pair<int, int>> madelungSequence()
{
    std::vector<std::pair<int, int>> seq;
    for (int sum = 1; sum <= 10; ++sum)
    {
        for (int n = 1; n <= sum; ++n)
        {
            const int l = sum - n;
            if (l < 0 || l > 3) continue;
            if (l >= n) continue;       // 物理上不存在的组合，如 1p、2d
            seq.emplace_back(n, l);
        }
    }
    return seq;
}

Ion Element::makeIon(int charge) const
{
    Ion ion;
    ion.charge    = charge;
    ion.subshells = sortByShell(subshells);

    if (charge > 0)
    {
        // ---- 阳离子：按「先失最外层」逐个剥电子 ----
        // 不能按 Aufbau 逆序剥！Fe 的填充顺序末尾是 4s，但 4s 是最外层，
        // 电离时 4s 先走，所以 Fe2+ 是 3d6，不是 3d4 4s2。
        for (int k = 0; k < charge; ++k)
        {
            int best = -1;
            for (int i = 0; i < static_cast<int>(ion.subshells.size()); ++i)
            {
                if (ion.subshells[i].electrons <= 0) continue;
                if (best < 0) { best = i; continue; }

                const Subshell& cur  = ion.subshells[i];
                const Subshell& keep = ion.subshells[best];
                // n 大的先失；n 相同时 l 大的先失
                if (cur.n > keep.n || (cur.n == keep.n && cur.l > keep.l))
                    best = i;
            }
            if (best < 0) break;             // 电子已经剥光，剩下的电荷无意义
            ion.subshells[best].electrons -= 1;
        }
    }
    else if (charge < 0)
    {
        // ---- 阴离子：按能级顺序找第一个没住满的亚层，往里塞 ----
        const std::vector<std::pair<int, int>> seq = madelungSequence();

        for (int k = 0; k < -charge; ++k)
        {
            for (const auto& nl : seq)
            {
                auto it = std::find_if(ion.subshells.begin(), ion.subshells.end(),
                                       [&](const Subshell& s)
                                       { return s.n == nl.first && s.l == nl.second; });

                if (it == ion.subshells.end())
                {
                    // 这个亚层还不存在 → 新建一个，装第 1 个电子
                    Subshell s;
                    s.n = nl.first;
                    s.l = nl.second;
                    s.electrons = 1;
                    ion.subshells.push_back(s);
                    break;
                }
                if (!isFull(*it))
                {
                    it->electrons += 1;
                    break;
                }
                // 住满了就继续看下一个能级
            }
        }
        ion.subshells = sortByShell(ion.subshells);
    }

    return ion;
}

// ============================================================
//  JSON 解析
// ============================================================

namespace
{

// 把 json 里一个对象读成 Subshell，顺便做范围检查
bool readSubshell(const json& node, Subshell* out, std::string* error)
{
    if (!node.is_object())
    {
        *error = "subshells 里的元素必须是对象";
        return false;
    }
    if (!node.contains("n") || !node["n"].is_number_integer())
    {
        *error = "subshell 缺少整数 n";
        return false;
    }
    if (!node.contains("l") || !node["l"].is_number_integer())
    {
        *error = "subshell 缺少整数 l";
        return false;
    }
    if (!node.contains("e") || !node["e"].is_number_integer())
    {
        *error = "subshell 缺少整数 e（电子数）";
        return false;
    }

    out->n         = node["n"].get<int>();
    out->l         = node["l"].get<int>();
    out->electrons = node["e"].get<int>();

    if (out->n < 1 || out->n > 7)
    {
        *error = "n 必须在 1..7 之间，实际 " + std::to_string(out->n);
        return false;
    }
    if (out->l < 0 || out->l > 3)
    {
        *error = "l 必须在 0..3 之间，实际 " + std::to_string(out->l);
        return false;
    }
    if (out->l >= out->n)
    {
        *error = "不存在 " + subshellName(*out) + " 这种亚层（要求 l < n）";
        return false;
    }
    if (out->electrons < 0 || out->electrons > subshellCapacity(out->l))
    {
        *error = subshellName(*out) + " 的电子数 " + std::to_string(out->electrons) +
                 " 超出容量 " + std::to_string(subshellCapacity(out->l));
        return false;
    }
    return true;
}

// 校验一个元素是否自洽，返回空字符串表示通过
std::string validateElement(const Element& e)
{
    if (e.atomicNumber < 1 || e.atomicNumber > 118)
        return "atomicNumber 必须在 1..118 之间";

    if (e.subshells.empty())
        return "subshells 不能为空";

    // 同一元素里不能出现两个相同的 (n, l)
    for (std::size_t i = 0; i < e.subshells.size(); ++i)
    {
        for (std::size_t j = i + 1; j < e.subshells.size(); ++j)
        {
            if (e.subshells[i].n == e.subshells[j].n &&
                e.subshells[i].l == e.subshells[j].l)
                return "亚层重复: " + subshellName(e.subshells[i]);
        }
    }

    // 电子总数必须等于质子数，这一条能抓出绝大多数手写数据错误
    const int total = countElectrons(e.subshells);
    if (total != e.atomicNumber)
        return "亚层电子总数 " + std::to_string(total) +
               " 与原子序数 " + std::to_string(e.atomicNumber) + " 不符";

    // 离子电荷的合理性 + 推算结果自检
    for (int q : e.commonIonCharges)
    {
        // H+ 是裸质子（0 个电子），合法；所以边界是 q > Z 才算剥空
        if (q > e.atomicNumber)
            return "离子电荷 +" + std::to_string(q) + " 大于电子数，电子不够剥";
        if (q < 0 && -q > 14)
            return "离子电荷 " + std::to_string(q) + " 过小，超出单原子阴离子的合理范围";

        // 推算出的电子数必须正好等于 Z - q；不等说明 makeIon 的逻辑坏了
        const Ion ion = e.makeIon(q);
        const int expected = e.atomicNumber - q;
        if (ion.electronCount() != expected)
            return "离子 " + ion.label(e.symbol) + " 推算得 " +
                   std::to_string(ion.electronCount()) + " 个电子，应为 " +
                   std::to_string(expected);
    }

    return {};
}

} // namespace

bool ElementDatabase::loadFromFile(const std::string& path, std::string* error)
{
    std::string dummy;
    std::string* err = error ? error : &dummy;
    err->clear();

    std::ifstream file(path);
    if (!file.is_open())
    {
        *err = "打不开文件: " + path;
        return false;
    }

    json root;
    try
    {
        file >> root;
    }
    catch (const json::parse_error& e)
    {
        // e.byte 是出错位置（字节偏移），大文件靠它定位
        *err = "JSON 解析失败（字节偏移 " + std::to_string(e.byte) + "）: " + e.what();
        return false;
    }

    if (!root.is_object() || !root.contains("elements") || !root["elements"].is_array())
    {
        *err = "JSON 顶层必须是对象，且含一个数组字段 \"elements\"";
        return false;
    }

    const json& arr = root["elements"];
    if (arr.empty())
    {
        *err = "\"elements\" 数组是空的";
        return false;
    }

    // 先解析到一个临时 vector，全部成功再整体替换。
    // 这样失败时数据库不会被写坏一半（异常安全的基本要求）。
    std::vector<Element> parsed;
    parsed.reserve(arr.size());

    for (std::size_t i = 0; i < arr.size(); ++i)
    {
        const json& node = arr[i];
        const std::string where = "第 " + std::to_string(i + 1) + " 个元素";

        if (!node.is_object())
        {
            *err = where + " 不是对象";
            return false;
        }

        Element e;
        try
        {
            if (!node.contains("symbol") || !node["symbol"].is_string())
            {
                *err = where + " 缺少字符串字段 symbol";
                return false;
            }
            e.symbol       = node["symbol"].get<std::string>();
            e.name         = node.value("name", std::string{});
            e.nameZh       = node.value("nameZh", std::string{});
            e.atomicNumber = node.value("atomicNumber", 0);
            e.atomicMass   = node.value("atomicMass", 0.0);
            e.period       = node.value("period", 0);
            e.group        = node.value("group", 0);
            e.category     = node.value("category", std::string{});

            if (node.contains("cpkColor") && node["cpkColor"].is_array() &&
                node["cpkColor"].size() == 3)
            {
                for (int c = 0; c < 3; ++c)
                    e.cpkColor[c] = node["cpkColor"][c].get<float>();
            }

            if (!node.contains("subshells") || !node["subshells"].is_array())
            {
                *err = where + "（" + e.symbol + "）缺少数组字段 subshells";
                return false;
            }
            for (const json& s : node["subshells"])
            {
                Subshell sub;
                std::string reason;
                if (!readSubshell(s, &sub, &reason))
                {
                    *err = where + "（" + e.symbol + "）的亚层有误: " + reason;
                    return false;
                }
                e.subshells.push_back(sub);
            }
            e.subshells = sortByShell(e.subshells);

            if (node.contains("commonIons") && node["commonIons"].is_array())
            {
                for (const json& q : node["commonIons"])
                    e.commonIonCharges.push_back(q.get<int>());
            }
        }
        catch (const json::type_error& e)
        {
            // 字段类型写错了（比如 atomicNumber 写成了字符串）
            *err = where + " 字段类型错误: " + e.what();
            return false;
        }

        const std::string problem = validateElement(e);
        if (!problem.empty())
        {
            *err = where + "（" + e.symbol + "）校验失败: " + problem;
            return false;
        }

        // 符号、原子序数都必须在全表范围内唯一
        for (const Element& prev : parsed)
        {
            if (prev.symbol == e.symbol)
            {
                *err = "元素符号重复: " + e.symbol;
                return false;
            }
            if (prev.atomicNumber == e.atomicNumber)
            {
                *err = "原子序数重复: " + std::to_string(e.atomicNumber);
                return false;
            }
        }

        parsed.push_back(std::move(e));
    }

    // 按原子序数排好，后续按 Z 查、按周期表画格子都省事
    std::sort(parsed.begin(), parsed.end(),
              [](const Element& a, const Element& b)
              { return a.atomicNumber < b.atomicNumber; });

    elements_.swap(parsed);
    return true;
}

void ElementDatabase::clear()
{
    elements_.clear();
}

const Element* ElementDatabase::find(const std::string& symbol) const
{
    for (const Element& e : elements_)
        if (e.symbol == symbol) return &e;
    return nullptr;
}

const Element* ElementDatabase::findByZ(int atomicNumber) const
{
    for (const Element& e : elements_)
        if (e.atomicNumber == atomicNumber) return &e;
    return nullptr;
}

// ============================================================
//  控制台报告
// ============================================================

std::string describe(const Element& e)
{
    std::ostringstream out;

    out << e.name << " (" << e.symbol << ")";
    if (!e.nameZh.empty()) out << "  " << e.nameZh;
    out << "\n";
    out << "  atomic number : " << e.atomicNumber << "\n";
    out << "  atomic mass   : " << e.atomicMass << "\n";
    out << "  period / group: " << e.period << " / " << e.group << "\n";
    out << "  category      : " << e.category << "\n";
    out << "  electrons     : " << e.electronCount() << "\n";
    out << "  config        : " << e.configString() << "\n";

    // 能级顺序：和书写顺序不同时才打印，省得读者以为代码写错了
    const std::vector<Subshell> byEnergy = e.energyOrderedSubshells();
    const std::string energyText = formatConfigOrdered(byEnergy);
    if (energyText != e.configString())
        out << "  energy order  : " << energyText << "   (填充次序，4s 在 3d 之前)\n";

    out << "  subshells     :\n";
    for (const Subshell& s : e.subshells)
    {
        const int cap = subshellCapacity(s.l);
        // 用等号方块直观显示"住满没住满"
        std::string bar(static_cast<std::size_t>(s.electrons), '=');
        std::string pad(static_cast<std::size_t>(cap - s.electrons), '.');
        out << "    " << subshellName(s) << "  " << s.electrons << "/" << cap
            << "  [" << bar << pad << "]\n";
    }

    if (e.commonIonCharges.empty())
    {
        out << "  common ions   : (none)\n";
    }
    else
    {
        out << "  common ions   :\n";
        for (int q : e.commonIonCharges)
        {
            const Ion ion = e.makeIon(q);
            const std::string name = ion.label(e.symbol);
            out << "    " << name << std::string(name.size() < 5 ? 5 - name.size() : 1, ' ')
                << " charge=" << (q > 0 ? "+" : "") << q
                << "  electrons=" << ion.electronCount()
                << "  " << ion.configString() << "\n";
        }
    }

    return out.str();
}

} // namespace orbital
