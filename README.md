# Atomotheca

用 C++ + OpenGL 实现的有机物分子 3D 可视化程序，支持**球棍模型**与**空间填充模型**切换，可从 `.mol` / `.pdb` 文件加载分子，并支持点击原子查看键长与键角。

## 功能

- 球棍模型 / 空间填充模型，一键切换
- 轨道相机：鼠标拖拽旋转、滚轮缩放
- CPK 元素配色 + Blinn-Phong 光照（双光源）
- 读取 `.mol`（V2000）与 `.pdb` 文件，无键表时按共价半径自动推断
- 自动居中、按分子大小自动适配相机
- Dear ImGui 控制面板：模型、半径、光照、背景实时调节
- 点击原子高亮并显示标签，测量键长与键角

<!-- 把截图放到 docs/images/ 下，再取消注释 -->
<!-- ![screenshot](docs/images/day12-picking.png) -->

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
> 如果 `git clone` 太慢，可到 [ImGui Releases](https://github.com/ocornut/imgui/releases) 下载 zip，解压后把文件夹重命名为 `imgui` 放到 `external/` 下。

## 编译

### Linux（Debian / Ubuntu）

```bash
git clone https://github.com/smallinkfish1/Atomotheca.git
cd Cpp-OpenGL-molecule-viewer

sudo apt update
sudo apt install build-essential cmake git libglfw3-dev libgl1-mesa-dev libglm-dev
git clone --depth 1 --branch v1.90.9 https://github.com/ocornut/imgui.git external/imgui

cmake -S . -B build
cmake --build build -j
```

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
```

## 操作

- **鼠标左键拖拽**：旋转视角
- **鼠标左键单击**：选中原子（高亮 + 标签）
- **Shift + 左键单击**：追加选中第二个原子
- **点击空白处**：取消选择
- **滚轮**：缩放
- **空格键**：切换球棍模型 / 空间填充模型
- **Tab 键**：显示 / 隐藏控制面板
- **Esc 键**：取消选择（没有选中时退出）

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
- **ANIMATION**：自动旋转开关与转速
- **LIGHTING**：环境光、高光强度、光泽度、补光强度
- **BACKGROUND**：背景颜色
- **ELEMENT LEGEND**：元素配色与半径图例

面板标签为英文：ImGui 内置字体不含中文字形，需要中文界面要自行加载中文字体文件。

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

**分子数据**

- 自行实现 `.mol`（V2000）与 `.pdb` 解析器，无第三方依赖
- PDB 的 `CONECT` 双向冗余会被去重
- 无键表时按**共价半径**自动推断化学键
- 加载后按包围盒中心自动居中，并按分子半径自动适配相机距离

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

也可以从 [PubChem](https://pubchem.ncbi.nlm.nih.gov/) 下载任意分子的 3D MOL 文件，放进 `assets/` 后直接加载。

## 项目结构

```
molecule-viewer/
├── CMakeLists.txt
├── assets/               # 示例分子（.mol / .pdb）
├── docs/images/          # 截图（可选）
├── external/
│   ├── glad/             # 随仓库提供
│   └── imgui/            # 需自行 clone，已被 gitignore
└── src/
    ├── main.cpp          # 主循环、相机、绘制
    ├── picking.h         # 射线拾取
    ├── path_utils.h      # 跨平台资源路径解析
    ├── molfile.h         # .mol / .pdb 解析
    ├── molecule.h        # 分子数据结构与 CPK 配色
    ├── sphere.h          # 球体网格生成
    ├── cylinder.h        # 圆柱网格生成
    ├── shader.h          # 着色器封装
    └── shaders/          # GLSL 源码
```

## 许可证

MIT License，详见 [LICENSE](LICENSE)。
