#!/usr/bin/env python3
# 生成示例分子文件（.mol / .pdb）
# 用程序生成是为了保证固定列宽格式严格对齐，手写极易错列。

import os, math

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "assets")
os.makedirs(OUT, exist_ok=True)

# ============================================================
# 1. 甲烷 CH4（正四面体，C-H 1.09 Å）
# ============================================================
def methane():
    k = 1.09 / math.sqrt(3.0)
    atoms = [("C", (0.0, 0.0, 0.0))]
    dirs = [(k, k, k), (k, -k, -k), (-k, k, -k), (-k, -k, k)]
    for d in dirs:
        atoms.append(("H", d))
    bonds = [(1, 2, 1), (1, 3, 1), (1, 4, 1), (1, 5, 1)]
    return atoms, bonds

# ============================================================
# 2. 甲醇 CH3OH（C 1.43 Å, C-O 1.43 Å, O-H 0.96 Å，理想四面体角）
# ============================================================
def methanol():
    a2 = math.acos(-1.0/3.0)          # 109.47°
    ca, sa = math.cos(a2), math.sin(a2)

    C  = (0.0, 0.0, 0.0)
    O  = (1.43, 0.0, 0.0)             # C-O 沿 +x

    # C 上的 3 个 H：与 C→O 方向成 109.47°，绕 x 轴均布 120°
    atoms = [("C", C), ("O", O)]
    for i in range(3):
        phi = math.radians(120.0 * i)
        # 方向：x 分量 ca，yz 分量 sa*(cos, sin)
        d = (ca, sa*math.cos(phi), sa*math.sin(phi))
        atoms.append(("H", (C[0] + 1.09*d[0], C[1] + 1.09*d[1], C[2] + 1.09*d[2])))

    # O 上的 H：与 O→C 成 108.5°，取在 xy 平面、朝 -y 侧
    ang = math.radians(108.5)
    # O→C 方向 = (-1, 0, 0)；绕 z 轴旋转 ang 得到 O→H
    dx = -math.cos(ang)
    dy = -math.sin(ang)
    atoms.append(("H", (O[0] + 0.96*dx, O[1] + 0.96*dy, O[2])))

    bonds = [(1, 2, 1),                       # C-O
             (1, 3, 1), (1, 4, 1), (1, 5, 1), # C-H
             (2, 6, 1)]                       # O-H
    return atoms, bonds

# ============================================================
# 3. 乙醇 C2H5OH
# ============================================================
def ethanol():
    t = math.acos(-1.0/3.0)               # 109.47°
    ct, st = math.cos(t), math.sin(t)

    C1 = (0.0, 0.0, 0.0)
    C2 = (1.51, 0.0, 0.0)                 # C-C 1.51 Å

    # O：从 C2 出发，与 C2→C1 成 109.47°，在 xy 平面内
    ux, uy = 1.0/3.0, 2.0*math.sqrt(2.0)/3.0
    O = (C2[0] + 1.43*ux, C2[1] + 1.43*uy, 0.0)

    atoms = [("C", C1), ("C", C2), ("O", O)]

    # C1 上的 3 个 H
    for i in range(3):
        phi = math.radians(90.0 + 120.0 * i)
        d = (-1.0/3.0, (2.0*math.sqrt(2.0)/3.0)*math.cos(phi),
                       (2.0*math.sqrt(2.0)/3.0)*math.sin(phi))
        atoms.append(("H", (C1[0] + 1.09*d[0], C1[1] + 1.09*d[1], C1[2] + 1.09*d[2])))

    # C2 上的 2 个 H：绕 C1-C2 轴与 C2→O 对称
    # 用基向量：a = C2→C1 单位向量；p = 垂直于 a 且在 O 所在方向的分量
    ax = (-1.0, 0.0, 0.0)
    # b = C2→O 单位向量
    bx = (ux, uy, 0.0)
    # b 垂直于 a 的分量，归一化后为 (0,1,0)
    p = (0.0, 1.0, 0.0)
    q = (0.0, 0.0, -1.0)   # a × p
    for sign in (+1, -1):
        phi = math.radians(120.0 * sign)
        d = (1.0/3.0,
             (2.0*math.sqrt(2.0)/3.0)*(math.cos(phi)*p[1] + math.sin(phi)*q[1]),
             (2.0*math.sqrt(2.0)/3.0)*(math.cos(phi)*p[2] + math.sin(phi)*q[2]))
        atoms.append(("H", (C2[0] + 1.09*d[0], C2[1] + 1.09*d[1], C2[2] + 1.09*d[2])))

    # O 上的 H：与 O→C2 成 105°
    ang = math.radians(105.0)
    # O→C2 方向 = -(ux, uy, 0)
    ox, oy = -ux, -uy
    hx = ox*math.cos(ang) - oy*math.sin(ang)
    hy = ox*math.sin(ang) + oy*math.cos(ang)
    atoms.append(("H", (O[0] + 0.96*hx, O[1] + 0.96*hy, O[2])))

    bonds = [
        (1, 2, 1),                                  # C1-C2
        (1, 4, 1), (1, 5, 1), (1, 6, 1),            # C1-H
        (2, 3, 1),                                  # C2-O
        (2, 7, 1), (2, 8, 1),                       # C2-H
        (3, 9, 1),                                  # O-H
    ]
    return atoms, bonds

# ============================================================
# 4. 苯 C6H6（正六边形，C-C 1.39 Å，C-H 1.08 Å）
# ============================================================
def benzene():
    R, rH = 1.39, 2.47
    atoms, bonds = [], []
    for i in range(6):
        a = math.radians(60.0 * i)
        atoms.append(("C", (R*math.cos(a), R*math.sin(a), 0.0)))
    for i in range(6):
        a = math.radians(60.0 * i)
        atoms.append(("H", (rH*math.cos(a), rH*math.sin(a), 0.0)))
    for i in range(6):
        bonds.append((i+1, (i+1) % 6 + 1, 1))       # 环上的 C-C
        bonds.append((i+1, i+7, 1))                 # C-H
    return atoms, bonds

# ============================================================
# 5. 水 H2O（O-H 0.96 Å，键角 104.5°）
# ============================================================
def water():
    half = math.radians(104.5 / 2.0)
    atoms = [
        ("O", (0.0, 0.0, 0.0)),
        ("H", (0.96*math.sin(half), 0.96*math.cos(half), 0.0)),
        ("H", (-0.96*math.sin(half), 0.96*math.cos(half), 0.0)),
    ]
    bonds = [(1, 2, 1), (1, 3, 1)]
    return atoms, bonds


# ============================================================
# 写 MOL (V2000)
# ============================================================
def write_mol(path, title, atoms, bonds):
    lines = []
    lines.append(title)
    lines.append("  MoleculeViewer          3D")     # 第 2 行：程序信息
    lines.append("")                                 # 第 3 行：注释（可为空）
    # 计数行：原子数(3) 键数(3) 其余填 0
    lines.append("%3d%3d  0  0  0  0            999 V2000" % (len(atoms), len(bonds)))
    # 原子行：x(10.4f) y(10.4f) z(10.4f) ' ' 元素(3,左对齐) 后面一堆 0
    for el, (x, y, z) in atoms:
        lines.append("%10.4f%10.4f%10.4f %-3s 0  0  0  0  0  0  0  0  0  0  0  0"
                     % (x, y, z, el))
    # 键行：a(3) b(3) 键类型(3) 其余 0（原子编号 1-based）
    for a, b, order in bonds:
        lines.append("%3d%3d%3d  0  0  0  0" % (a, b, order))
    lines.append("M  END")
    lines.append("")
    with open(path, "w", encoding="ascii") as f:
        f.write("\n".join(lines))
    print("wrote", path)

# ============================================================
# 写 PDB（严格 80 列）
# ============================================================
def write_pdb(path, name, atoms, bonds):
    lines = []
    lines.append("COMPND    %s" % name)
    lines.append("AUTHOR    GENERATED-BY-MOLECULE-VIEWER")

    for idx, (el, (x, y, z)) in enumerate(atoms, start=1):
        # 原子名：元素 + 序号（如 C1、H3），左对齐占 4 列
        atom_name = "%s%d" % (el, idx)
        line = (
            "ATOM  "                      # 1-6   记录名
            + "%5d" % idx                 # 7-11  原子序号
            + " "                         # 12
            + "%-4s" % atom_name[:4]      # 13-16 原子名
            + " "                         # 17    altLoc
            + "%3s" % "UNL"               # 18-20 残基名
            + " "                         # 21
            + "A"                         # 22    链 ID
            + "%4d" % 1                   # 23-26 残基序号
            + " "                         # 27    iCode
            + "   "                       # 28-30
            + "%8.3f" % x                 # 31-38 x
            + "%8.3f" % y                 # 39-46 y
            + "%8.3f" % z                 # 47-54 z
            + "%6.2f" % 1.00              # 55-60 occupancy
            + "%6.2f" % 0.00              # 61-66 tempFactor
            + " " * 10                    # 67-76
            + "%2s" % el                  # 77-78 元素符号（右对齐）
        )
        lines.append(line)

    # CONECT：把每个原子的邻居列出来
    adj = {}
    for a, b, _ in bonds:
        adj.setdefault(a, []).append(b)
        adj.setdefault(b, []).append(a)
    for i in range(1, len(atoms) + 1):
        nbrs = sorted(adj.get(i, []))
        if not nbrs:
            continue
        line = "CONECT" + "%5d" % i
        for n in nbrs:
            line += "%5d" % n
        lines.append(line)

    lines.append("END")
    lines.append("")
    with open(path, "w", encoding="ascii") as f:
        f.write("\n".join(lines))
    print("wrote", path)


# ============================================================
# 生成全部示例文件
# ============================================================
if __name__ == "__main__":
    datasets = [
        ("methane",  "METHANE",  methane),
        ("methanol", "METHANOL", methanol),
        ("ethanol",  "ETHANOL",  ethanol),
        ("benzene",  "BENZENE",  benzene),
        ("water",    "WATER",    water),
    ]
    for fname, title, fn in datasets:
        atoms, bonds = fn()
        write_mol(os.path.join(OUT, fname + ".mol"), title, atoms, bonds)
        write_pdb(os.path.join(OUT, fname + ".pdb"), title, atoms, bonds)
    print("\n完成。")
