# Atomotheca

用 C++ + OpenGL 实现的有机物分子 3D 可视化程序，支持**球棍模型**与**空间填充模型**切换，可从 `.mol` / `.pdb` 文件加载分子，并支持点击原子查看键长与键角。

同时内置 **Atomotheca 原子典**模块：从 JSON 读取元素数据，输出电子排布式与常见离子状态。

> 打算改代码或接手这个项目？先读 **[AGENTS.md](AGENTS.md)**：构建命令、架构铁律、
> 每日交付约定、已知技术债与踩过的坑都在里面。每日开发记录见 **[devlog/](devlog/)**。

## 功能

- 球棍模型 / 空间填充模型，一键切换
- 轨道相机：鼠标拖拽旋转、滚轮缩放
- CPK 元素配色 + Blinn-Phong 光照（双光源）
- 读取 `.mol`（V2000）与 `.pdb` 文件，无键表时按共价半径自动推断
- 自动居中、按分子大小自动适配相机
- Dear ImGui 控制面板：模型、半径、光照、背景实时调节
- 点击原子高亮并显示标签，测量键长与键角
- **元素数据库（Atomotheca）**：36 个元素（H–Kr，前四周期完整）的电子排布、常见离子由 `assets/data/elements.json` 驱动，改数据不用重编译
- **元素周期表面板**：18 族网格，按 CPK 配色，点击查看排布式并在中性原子 / 常见离子之间切换
- **2D 能级图**：按能量高低画出每个亚层的轨道方框与自旋箭头，4s 在 3d 下方，一眼看出填充顺序
- **3D 轨道形状**：半透明的 s 球、p 哑铃，以及五个 d 轨道（四瓣梅花与 `d_z2` 的「两瓣 + 腰环」）；
  形状由球谐函数的角向部分 `r = |f(方向)|` 直接算出，边缘带 Fresnel 高光，可旋转观察

<!-- 截图暂缺。要加的话放到 devlog/images/ 下，再取消下一行注释 -->
<!-- ![screenshot](devlog/images/day15.png) -->

## 环境要求

- CMake ≥ 3.16
- C++17 编译器（g++ 9+ / MSVC 2019+ / Clang 10+）
- GLFW ≥ 3.3
- GLM
- Dear ImGui ≥ 1.90（**需手动拉取，见下**）

### 依赖

**Dear ImGui 不在本仓库中**，首次编译前需要拉取：

```bash
git clone --depth 1 --branch v1.90.9 https://github.com/ocornut/imgui.git external/imgui
```

> `external/glad/` 已随仓库提供，无需额外下载。
>
> `external/json/`（nlohmann/json 3.11.3 单头文件）也已随仓库提供，无需额外下载。
>
> `external/glm/` 同样随仓库提供（Web / WASM 构建时 emcc 的 sysroot 里没有 GLM，靠这份兜底）。
>
> `external/glfw/` **只在 Linux 上交叉编译 Windows 时才需要 clone**（见下面「交叉编译 Windows」）。
> Linux 原生构建优先使用系统 `libglfw3-dev`；系统库和源码都没有时，CMake 配置阶段会直接报错。
>
> 如果 `git clone` 太慢，可到 [ImGui Releases](https://github.com/ocornut/imgui/releases) 下载 zip，解压后把文件夹重命名为 `imgui` 放到 `external/` 下。

## 编译

### Linux（Debian / Ubuntu）

```bash
git clone https://github.com/smallinkfish1/Atomotheca.git
cd Atomotheca

sudo apt update
sudo apt install build-essential cmake git libglfw3-dev libgl1-mesa-dev libglm-dev
git clone --depth 1 --branch v1.90.9 https://github.com/ocornut/imgui.git external/imgui

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

> **GLFW 从哪来**：原生构建优先用系统的 `libglfw3-dev`（`find_package(glfw3 3.3)`），
> 因为 GLFW 3.4 的 X11 后端从源码编时还需要 `libxinerama-dev`、`libxcursor-dev`、
> `libxi-dev`、`libxrandr-dev`，而 apt 的 `libglfw3-dev` 已经把这些依赖编进去了。
> 只有交叉编译、或系统没装 glfw3 时，CMake 才退回编译 `external/glfw` 源码。
>
> 如果你以前用旧版本的 CMakeLists 配置过，`CMakeCache.txt` 里可能残留
> `GLFW_BUILD_X11=OFF`，此时编出来的 GLFW 只有 null 后端，运行时报
> `[错误] GLFW 初始化失败！`。删掉构建目录重新配置即可。

### 在 Linux 上交叉编译 Windows

不想开 Windows 虚拟机时，可以用 MinGW-w64 直接产出 `.exe`：

```bash
sudo apt install mingw-w64 libglm-dev
# 交叉编译无法链接宿主机的 .so，必须提供 GLFW 源码
git clone --depth 1 --branch 3.4 https://github.com/glfw/glfw.git external/glfw

cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw64.cmake
cmake --build build-win -j$(nproc)
```

产物在 `build-win/molecule_viewer.exe`，已静态链接 GCC 运行时，拷到没装 MinGW 的机器上也能跑。
`assets/` 和 `src/shaders/` 会由构建脚本自动复制到产物旁边。

### Windows

推荐用 **MSYS2**（命令和 Linux 一致）：

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
          mingw-w64-ucrt-x86_64-glfw mingw-w64-ucrt-x86_64-glm \
          mingw-w64-ucrt-x86_64-ninja
# 然后在 MSYS2 UCRT64 终端里
cmake -S . -B build -G Ninja
cmake --build build
```

也可以用 **vcpkg + Visual Studio**：

```cmd
vcpkg install glfw3:x64-windows glm:x64-windows
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg路径>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

### macOS

```bash
brew install glfw glm cmake
cmake -S . -B build
cmake --build build -j
```

## 运行

```bash
./build/molecule_viewer
```

**资源路径已自适配**，以下两种方式都能正常运行：

```bash
./build/molecule_viewer          # 在项目根目录
cd build && ./molecule_viewer    # 在 build 目录
```

程序会依次在「当前工作目录 → 可执行文件目录 → 可执行文件上级目录」查找 `assets/` 和 `src/shaders/`。

> 如果要把可执行文件单独拷走，记得同时拷贝 `assets/` 和 `src/shaders/`，或者用 `cmake --install build --prefix <目录>` 一起安装。

## 命令行

```bash
./build/molecule_viewer                      # 默认加载甲烷
./build/molecule_viewer assets/benzene.mol   # 指定分子
./build/molecule_viewer --help               # 查看帮助
./build/molecule_viewer --version            # 查看版本
./build/molecule_viewer --dump O             # 打印氧的电子排布与常见离子
./build/molecule_viewer --dump Fe            # 打印铁（含 Fe2+ / Fe3+）
./build/molecule_viewer --dump-table         # 用 ASCII 打印周期表，核对 18 族的排布
./build/molecule_viewer --dump-levels Fe     # 用 ASCII 打印能级图，核对 4s 在 3d 下方
./build/molecule_viewer --dump-orbital pz    # 打印轨道网格自检与 ASCII 剖面
./build/molecule_viewer --dump-orbital dz2   # d 轨道；省略形状名则九个全打
```

`--dump` 是纯数据层验证，不打开窗口，可以在没有显卡的环境（CI）里跑：

```
$ ./build/molecule_viewer --dump Fe
元素数据库: 36 个元素  |  示例 O: 1s2 2s2 2p4
Iron (Fe)  铁
  atomic number : 26
  atomic mass   : 55.845
  period / group: 4 / 8
  category      : transition-metal
  electrons     : 26
  config        : 1s2 2s2 2p6 3s2 3p6 3d6 4s2
  energy order  : 1s2 2s2 2p6 3s2 3p6 4s2 3d6   (填充次序，4s 在 3d 之前)
  subshells     :
    1s  2/2  [==]
    2s  2/2  [==]
    2p  6/6  [======]
    3s  2/2  [==]
    3p  6/6  [======]
    3d  6/10  [======....]
    4s  2/2  [==]
  common ions   :
    Fe2+  charge=+2  electrons=24  1s2 2s2 2p6 3s2 3p6 3d6
    Fe3+  charge=+3  electrons=23  1s2 2s2 2p6 3s2 3p6 3d5
```

> 元素符号大小写写错（`--dump fe`）会自动修正；数据库里没有的元素会列出所有可用符号。

`--dump-table` 用 ASCII 打印整张周期表，用来核对「按周期/族定位」的排布：

```
$ ./build/molecule_viewer --dump-table
     1   2   3   4   5   6   7   8   9   10  11  12  13  14  15  16  17  18
1    H                                                                   He
2    Li  Be                                          B   C   N   O   F   Ne
3    Na  Mg                                          Al  Si  P   S   Cl  Ar
4    K   Ca  Sc  Ti  V   Cr  Mn  Fe  Co  Ni  Cu  Zn  Ga  Ge  As  Se  Br  Kr

图例: 符号=已收录   . =真实存在但未收录   空白=该位置没有元素
```

前四周期 36 个元素全部收录，所以第 4 周期 18 格是满的。第 2、3 周期中间的空白不是缺数据——那一层没有 d 轨道，3–12 族本就不存在。

`--dump-levels` 用 ASCII 打印能级图，用来核对「谁在谁上面」：

```
$ ./build/molecule_viewer --dump-levels Fe
Fe  Iron   Z=26

  energy ^
         |
  3d     [^v][^ ][^ ][^ ][^ ]  6/10
  4s     [^v]                  2/2
  3p     [^v][^v][^v]          6/6
  3s     [^v]                  2/2
  2p     [^v][^v][^v]          6/6
  2s     [^v]                  2/2
  1s     [^v]                  2/2
         |
```

文本从上到下是能量由高到低，所以 **4s 出现在 3d 之后**。`^` 是自旋向上、`v` 是自旋向下。这个命令会把中性原子和该元素所有常见离子都打一遍。

`--dump-orbital` 检查轨道网格本身：顶点规模、包围盒、法线长度、三角形绕序、顶点是否落在解析曲面上、有向体积与解析体积的比值，再给一张 ASCII 剖面。形状名可取 `s` / `px` / `py` / `pz` / `dxy` / `dyz` / `dxz` / `dx2y2` / `dz2`（下划线、`^`、连字符都忽略），省略则九个全打。

```
$ ./build/molecule_viewer --dump-orbital pz
p_z   axis z   lobes 2
  vertices   : 2450
  triangles  : 4608
  extent     : 1.000   lobe radius 0.500
  lobes      :  (0.000, 0.000, -0.500)  (0.000, 0.000, 0.500)
  bbox x     : -0.500 .. 0.500
  bbox y     : -0.500 .. 0.500
  bbox z     : -1.000 .. 1.000
  normal len : max |n-1| = 0.000
  on surface : max |d-r| = 0.000
  winding    : 0 / 4608 triangles reversed  (ok, CCW outward)
  degenerate : 192 (极点退化三角形，正常)
  slice y-z (x=0):   ('#'=inside  '.'=outside  '-|'=axis)
    ...............|...............
    ...........#########...........
    .........#############.........
    .........#############.........
    .........#############.........
    ..........###########..........
    ---------------#---------------
    ..........###########..........
    .........#############.........
    .........#############.........
    .........#############.........
    ...........#########...........
    ...............|...............
```

包围盒说明了对称轴：`p_z` 在 z 方向伸到 ±1.000，另两轴只有 ±0.500。剖面里是两个在原点相切的球 —— 这正是角向函数 `r = |cos θ|` 的精确形状，不是手工造型。

## 操作

- **鼠标左键拖拽**：旋转视角
- **鼠标左键单击**：选中原子（高亮 + 标签）
- **Shift + 左键单击**：追加选中第二个原子
- **点击空白处**：取消选择
- **滚轮**：缩放
- **空格键**：切换球棍模型 / 空间填充模型
- **Tab 键**：显示 / 隐藏控制面板
- **P 键**：显示 / 隐藏元素周期表
- **L 键**：显示 / 隐藏能级图
- **O 键**：显示 / 隐藏 3D 轨道形状
- **Esc 键**：取消选择（没有选中时退出）

### 打开分子文件

三种方式任选：

- 把 `.mol` / `.pdb` 文件**拖拽**到窗口中
- 点击控制面板的 `Open .mol / .pdb ...`
- 按 `Ctrl+O`

## 测量

选中原子后，控制面板的 **SELECTION** 区会显示：

- 选中 **1 个**原子：元素、序号、坐标、邻居数、所有**键长**、两两**键角**
- 选中 **2 个**原子：两者**距离**；若有共同邻居，还会给出**键角**

验证示例：

- 甲烷：C–H 键长 1.090 Å，键角 **109.5°**（正四面体）
- 苯：C–C 键长 1.390 Å，C–H 1.080 Å，键角 **120.0°**（平面正六边形）

## 控制面板

左上角面板可实时调节，改完立即生效，无需重新编译：

- **MOLECULE**：文件名、原子数、键数、分子半径
- **SELECTION**：选中原子的元素、坐标、键长、键角
- **DISPLAY**：球棍 / 空间填充切换、半径缩放、原子标签开关
- **ORBITAL SHAPE**：s / p_x / p_y / p_z 选择、不透明度、是否隐藏分子
- **ANIMATION**：自动旋转开关与转速
- **LIGHTING**：环境光、高光强度、光泽度、补光强度
- **BACKGROUND**：背景颜色
- **ELEMENT LEGEND**：元素配色与半径图例

面板标签为英文：ImGui 内置字体不含中文字形，需要中文界面要自行加载中文字体文件。

## 元素周期表

按 `P` 键，或勾选控制面板里的 `Periodic table`。它是右侧的一个独立窗口，可以拖动、可以缩放。

- 18 族 × 4 周期网格，顶部有 1–18 的族号表头，每格底色是该元素的 CPK 配色，左上角是原子序数
- 第 2、3 周期的 3–12 族本来就是空的：那一层没有 d 轨道，不是数据缺失
- 前四周期已收满；淡灰空框表示「真实存在但数据库没收录」，往后扩到 5–7 周期时才出现
- 点任一格，下方显示英文名、原子量、周期 / 族、类别、电子排布式
- 排布式下面若多出一行 `fill order`，说明填充次序与书写顺序不同（如 Fe 是 `4s2 3d6` 而不是 `3d6 4s2`）
- `Atom` / `Fe2+` / `Fe3+` 按钮切换离子状态，排布式按「先失最外层」重新推算

## 能级图

按 `L` 键，或勾选控制面板里的 `Energy levels`。窗口跟随周期表的当前选择 —— 点周期表里的 Fe，这里立刻变成铁的；点 `Fe3+`，能级图也跟着变。

> 默认是关闭的：默认窗口 1100×700 放不下「控制面板 + 周期表 + 能级图」三块，同时开必然重叠。需要时按 `L`，窗口可以拖到任意位置。

- 纵轴没有数值刻度，只表示 **能量的先后次序**：越往上能量越高、越晚填
- **4s 画在 3d 下方**，因为 Madelung 规则下 4s（n+l=4）先于 3d（n+l=5）填充
- 每个方框是一个轨道，↑ 蓝、↓ 橙；成对的电子挤在一个框里
- 电子按**洪特规则**分布：同一亚层里先各占一个轨道、自旋平行，占满才配对
- 右侧 `6/10` 是该亚层的电子数 / 容量
- 只剩空位时（如 Ca 的 3d），上方会出现一个淡色空层并标注 `next to fill` —— 表示「下一个电子填这里」
- 鼠标悬停某一行，整行高亮并显示该亚层的轨道数与成对 / 单占数量

**为什么 Fe²⁺ 是 3d⁶ 而不是 3d⁴4s²**：能级图上看得最清楚 —— 4s 是最外层，电离时先丢 4s 的电子。所以切到 `Fe2+` 时，4s 那一行变成 `0/2`，3d 仍是 `6/10`。

## 轨道形状

按 `O` 键，或勾选控制面板里的 `ORBITAL SHAPE → Show in 3D view`。轨道画在分子中心，尺寸跟分子半径成比例。

面板里是一个下拉框，九种形状（全部 ASCII 命名，因为 ImGui 默认字体不含上标）：

| 形状 | 角向部分 | 长什么样 | 颜色 |
|---|---|---|---|
| `s` | `1` | 一个球 | 中性青 |
| `p_x` / `p_y` / `p_z` | `\|x\|` / `\|y\|` / `\|z\|` | 两个相切的球（哑铃） | 红 / 绿 / 蓝 |
| `d_xy` | `\|2xy\|` | 四瓣梅花，瓣在对角线上 | 紫 |
| `d_yz` / `d_xz` | `\|2yz\|` / `\|2xz\|` | 同上，转到 y-z / x-z 平面 | 青 / 橙 |
| `d_x2-y2` | `\|x²−y²\|` | 四瓣梅花，瓣在 x、y 轴上 | 洋红 |
| `d_z2` | `\|(3z²−1)/2\|` | 两极两瓣 + 赤道一圈腰环 | 黄 |

- 半透明，边缘比中心更不透明（Fresnel），所以看起来像「一层壳」而不是塑料片
- 默认会隐藏分子 —— 半透明轨道套在原子上基本看不清形状；想看「轨道包住原子」就取消 `Hide molecule`
- 自动旋转打开时，轨道跟着一起转

**画的是角向部分，不是电子云。** 所有形状都是同一句话：`r = |f(方向)| × extent`。

- `s` 的 f 是常数 → 球。
- `p` 的 f 正比于 cos θ，而 `r = |cos θ|` 在极坐标里**恰好**是「半径 R/2、球心在 ±R/2 的两个相切球」
  —— 所以 s / p 用两个 `generateSphere` 平移就是精确结果，没有手工造型。
- `d` 没有这种巧合，代码真的按球谐函数把一个 96×48 的球面做径向变形；
  法线用隐函数 `F(p) = |p| − f(p̂)·extent` 的梯度（中心差分）算，再按三角形自动定向绕序。

`d_z2` 的节面是两个圆锥（半顶角 54.7°）而不是平面，所以它长得和另外四个不一样：
`|z| > 1/√3` 的地方是两瓣，赤道带是半径只有一半的腰环。

完整的概率密度还要乘径向函数，那是后续的点云工作。同一个轨道的相邻瓣在物理上相位相反（+ / −），
这里整形状同色，只表达「长什么样、朝哪摆」。

## 技术要点

**几何与渲染**

- 球体采用 **UV 球（经纬球）** 参数化生成，通过分段数控制精度
- 圆柱键默认沿 Y 轴生成，用缩放 → 旋转 → 平移的顺序摆放到两原子之间
- 圆柱对准两原子用**点积求角度、叉积求旋转轴**
- 同一份网格通过 Model 矩阵复用，渲染不同大小的原子与不同长度的键

**光照**

- **Blinn-Phong 光照**：环境光 + 漫反射 + 镜面高光
- **双光源**（主光 + 补光），避免暗部死黑
- **CPK 配色**，并针对深色背景做亮度适配

**深度与遮挡**

- 开启背面剔除，三角形绕序统一为"从外看逆时针"
- 原子按相机距离排序绘制，缓解穿插造成的 Z-fighting
- 深度缓冲请求 24 位

**半透明（轨道形状）**

- 渲染拆成两个阶段：先不透明（原子、键），再半透明（轨道）。顺序不能反
- 透明物体按**瓣心**到相机的距离从远到近排序（整个形状只用一个中心排不出两瓣的先后）
- 画透明时关掉**深度写入**但保留深度测试：不被原子遮挡，同时自身各层都能画上
- 每一瓣分两次画（先背面、后正面），只画正面会变成一块扁平色块
- 结束必须还原 `glDepthMask(GL_TRUE)`、`glDisable(GL_BLEND)`、`glCullFace(GL_BACK)`，否则下一帧的遮挡关系会时好时坏
- 透明度用 Fresnel 而不是常数：`alpha = base + pow(1 - |N·V|, 2.5) × 0.55`，边缘更实、中心更透
- 背面要翻法线（`if (!gl_FrontFacing) N = -N;`），否则内壁发黑
- p 的网格是两个 UV 球平移拼成，索引偏移必须在追加顶点**之前**算好（`base = vertices.size() / 6`）

**分子数据**

- 自行实现 `.mol`（V2000）与 `.pdb` 解析器，无第三方依赖
- PDB 的 `CONECT` 双向冗余会被去重
- 无键表时按**共价半径**自动推断化学键
- 加载后按包围盒中心自动居中，并按分子半径自动适配相机距离

**元素数据（Atomotheca）**

- 元素基础数据、电子排布、常见离子全部外置到 `assets/data/elements.json`，C++ 里不硬编码
- 亚层用 `{n, l, e}` 三个整数表示，容量按 `2 × (2l + 1)` 校验
- 两套排序：**书写顺序**（按 `n`、`l`，与课本一致）和**填充顺序**（Madelung 规则 `n+l`，体现 4s 在 3d 之前）
- 阳离子按「**先失最外层**」逐个剥电子，所以 Fe²⁺ 是 `3d⁶`，不是 `3d⁴4s²`
- **例外显式写进 JSON**：Cr 是 `3d⁵4s¹`、Cu 是 `3d¹⁰4s¹`，不符合 Madelung 规则。数据层不做「规则自动修正」——规则只用来算离子和排序，中性原子的排布以 JSON 为准
- 加载时做三道校验：字段范围、电子总数 = 原子序数、离子推算自检
- 周期表按 `(period, group)` 坐标定位，不依赖数组顺序，增删元素不会让其他格子移位
- 族号表头与格子共用同一套 `SameLine` 坐标，用 `AddText` 居中而不是 `Text`，否则表头数字会和列错开
- `cellExists()` 区分「位置本来不存在」（第 2、3 周期的 d 区）与「存在但没收录」，前者留白、后者画淡框
- 格子用 `InvisibleButton` 占位 + `ImDrawList` 自绘：既能画两行文字，又靠 `PushID` 保证每格 ID 唯一
- 能级图的**纵向位置由 Madelung 序列决定**（不是按 n 排序），所以 4s 自然落在 3d 下方
- 洪特规则用两个整数表达：`paired = max(0, n-2l-1)`、`single = n - 2×paired`，不需要为每个轨道存占用表
- 自旋箭头用 `AddLine` + `AddTriangleFilled` 画，不能用 `↑` 字符（ImGui 默认字体没有这个字形）
- 能级行按「元素 + 电荷」缓存，只有切换元素或离子时才重建，避免每帧复制 vector
- 能级图的整块画布只用一个 `InvisibleButton`，鼠标落在哪一行靠「y ÷ 行高」反算，不生成几十个控件 ID
- 轨道网格的自检（`--dump-orbital`）验四件事：法线是否单位长度、顶点是否都落在解析曲面上、包围盒、三角形绕序
- 绕序检查必须**先判退化**：UV 球两极整圈顶点重合，叉积恒为 0，直接判 `dot <= 0` 会把 96 / 192 个退化三角形误报成「绕序反了」

**交互**

- 原子拾取用**射线拾取**：屏幕坐标经逆投影 + 逆视图还原成 3D 射线，再与球体求交
- 拾取、渲染、标签三处共用同一个世界变换（自动旋转时不脱节）
- 原子标签把 3D 坐标投影回屏幕，用 ImGui 前景层绘制

**化学**

- 甲烷为正四面体构型，键角 109.5°（碳 sp³ 杂化）
- 空间填充模型使用**范德华半径**，球棍模型使用艺术化的小半径

## 支持的格式

- **MOL（V2000）**：3D 坐标 + 键表
- **PDB**：`ATOM` / `HETATM` + `CONECT`（无 `CONECT` 时自动推断键）
- MOL（V3000）暂不支持；SDF 只读取第一个分子
- **元素数据（JSON）**：本项目自定义格式，见 `assets/data/elements.json`

也可以从 [PubChem](https://pubchem.ncbi.nlm.nih.gov/) 下载任意分子的 3D MOL 文件，放进 `assets/` 后直接加载。

## 项目结构

```
molecule-viewer/
├── AGENTS.md              # 给 AI / 接手者的开发说明书（构建、验证、铁律、约定）
├── README.md
├── CMakeLists.txt
├── cmake/
│   └── toolchain-mingw64.cmake  # Linux 上交叉编译 Windows
├── assets/                # 示例分子（.mol / .pdb）
│   └── data/
│       └── elements.json  # 元素数据库（36 个元素，H–Kr）
├── devlog/                # 每日开发日志与说明文档（day15 – day18）
├── external/
│   ├── glad/              # 随仓库提供
│   ├── json/              # nlohmann/json 单头文件，随仓库提供
│   ├── glm/               # 随仓库提供（Web 构建兜底）
│   ├── imgui/             # 需自行 clone，已被 gitignore
│   └── glfw/              # 交叉编译时才 clone，已被 gitignore
└── src/
    ├── main.cpp           # 应用状态、回调、ImGui 面板、主循环
    ├── picking.h          # 射线拾取
    ├── path_utils.h       # 跨平台资源路径解析
    ├── file_dialog.h      # 系统文件对话框（Win32 / zenity / osascript）
    ├── molfile.h          # .mol / .pdb 解析
    ├── molecule.h         # 分子数据结构与 CPK 配色
    ├── sphere.h           # 球体网格生成
    ├── cylinder.h         # 圆柱网格生成
    ├── shader.h           # 着色器封装
    ├── orbital/
    │   ├── element.h      # 元素 / 亚层 / 离子 / 数据库 声明
    │   ├── element.cpp    # JSON 解析、校验、电子排布、离子推算
    │   ├── shape.h        # s / p 轨道形状、颜色、几何自检声明
    │   └── shape.cpp      # 球 / 双瓣网格生成、绕序与曲面自检、ASCII 剖面
    ├── ui/
    │   ├── periodic_table.h/.cpp  # 元素周期表面板（网格、详情、离子切换）
    │   ├── energy_diagram.h/.cpp  # 能级图（能量顺序、洪特规则、自旋箭头）
    │   └── orbital_panel.h/.cpp   # 轨道形状控制项（形状、不透明度、隐藏分子）
    └── shaders/           # GLSL 源码（sphere / cylinder / orbital）
```

## 开发日志

`devlog/` 下每天一个 `dayNN-主题/` 子目录，里面是当天的「使用说明」+「实现原理」两份文档，
索引见 [devlog/README.md](devlog/README.md)。它回答「那天为什么这么改」，`git log` 回答「改了哪些行」。

| 天 | 主题 | 目录 |
|---|---|---|
| 15 | 元素数据库（Atomotheca 模块第 1 天） | [day15-元素数据库](devlog/day15-元素数据库/) |
| 16 | 元素周期表面板（模块第 2 天） | [day16-元素周期表](devlog/day16-元素周期表/) |
| 17 | 2D 能级图（模块第 3 天） | [day17-能级图](devlog/day17-能级图/) |
| 18 | 3D s / p 轨道形状（模块第 4 天） | [day18-轨道形状](devlog/day18-轨道形状/) |
| 19 | 3D d 轨道（模块第 5 天） | [day19-d轨道](devlog/day19-d轨道/) |

每天文档的最后一节固定是「今日功能的变化与修改」，每个 `dayNN-*` 目录下另有一份
`releasenote.md`——它就是那个版本的发布说明。

## 版本与发布

- 当前版本 `0.2.0`，唯一来源是 `CMakeLists.txt` 的 `project(VERSION x.y.z)`，
  `molecule_viewer --version` 打印的就是它；**tag 名必须与它一致**。
- 发布由 CI 完成：push `v*` tag 会三平台编译并自动创建 Release
  （Linux x86_64 tar.gz、Windows MSVC zip、Windows MinGW zip），
  见 [Releases](../../releases) 与 [.github/workflows/build.yml](.github/workflows/build.yml)。
- Release 正文自动取自 `devlog/dayNN-*/releasenote.md`：CI 取版本号相同的所有文件
  按天拼接，找不到才退回 GitHub 自动生成的 commit 清单。
- push 到 `main` 只编译验证、不发版。

## 许可证

MIT License，详见 [LICENSE](LICENSE)。

---
