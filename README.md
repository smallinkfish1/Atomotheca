# Molecule Viewer（分子可视化）（Work in Progress）

用 C++ + OpenGL 实现的有机物分子 3D 可视化程序，支持**球棍模型**与**空间填充模型**切换，可从 `.mol` / `.pdb` 文件加载分子。（正在开发中，最终功能尽情期待）

## 功能规划

- [x] 第 1 天：GLFW 空窗口
- [x] 第 2 天：GLAD 初始化 + 渲染循环
- [x] 第 3 天：VBO/VAO/EBO 与着色器
- [x] 第 4 天：GLM 与 3D 变换
- [x] 第 5 天：轨道相机（旋转/缩放）
- [x] 第 6 天：球体网格与光照
- [x] 第 7 天：球棍模型（CH₄）
- [x] 第 8 天：空间填充模型与模式切换
- [x] 第 9 天：元素配色与 Blinn-Phong 光照
- [x] 第 10 天：.mol/.pdb 解析器
- [x] 第 11 天：Dear ImGui 控制面板
- [x] 第 12 天：原子拾取与键长键角
- [ ] 第 13 天：跨平台构建与开源发布
- [ ] 第 14 天：Emscripten / WebAssembly

## 环境要求

- CMake ≥ 3.16
- C++17 编译器（g++ 11+）
- GLFW ≥ 3.3
- GLM (libglm-dev)
- Dear ImGui ≥ 1.90（需手动拉取，见下）

### 依赖 （重要）

- Dear ImGui（需手动拉取）：

```bash
git clone --depth 1 --branch v1.90.9 https://github.com/ocornut/imgui.git external/imgui
```

- GLAD 已随仓库提供（`external/glad/`），无需下载

## 在 Linux 上下载源码并编译

```bash
git clone https://github.com/smallinkfish1/Cpp-OpenGL-molecule-viewer.git
cd Cpp-OpenGL-molecule-viewer
sudo apt update
sudo apt install build-essential cmake git libglfw3-dev libgl1-mesa-dev libglm-dev
git clone --depth 1 --branch v1.90.9 https://github.com/ocornut/imgui.git external/imgui
cmake -S . -B build
cmake --build build -j
./build/molecule_viewer
```

## 运行注意

必须在**项目根目录**运行程序。着色器和分子数据都是用相对路径加载的（`src/shaders/`、`assets/`），在 `build/` 目录里执行会报"着色器文件读取失败"或"无法打开文件"。

## 操作

- **鼠标左键拖拽**：旋转视角
- **鼠标左键单击**：选中原子（高亮 + 显示标签）
- **Shift + 左键单击**：追加选中第二个原子（最多两个）
- **点击空白处**：取消选择
- **滚轮**：缩放
- **空格键**：切换球棍模型 / 空间填充模型
- **Tab 键**：显示 / 隐藏控制面板
- **Esc 键**：取消选择（没有选中时退出程序）

## 测量

选中原子后，控制面板的 **SELECTION** 区会显示：

- 选中 **1 个**原子：元素、序号、坐标、邻居数量、**所有键长**、**两两键角**
- 选中 **2 个**原子：两者**距离**；若有共同邻居，还会给出**键角**

验证示例：
- 甲烷 C–H 键长 1.090 Å，键角 **109.5°**（正四面体）
- 苯 C–C 键长 1.390 Å，C–H 1.080 Å，键角 **120.0°**（平面正六边形

## 控制面板

左上角面板可实时调节，改完立即生效，无需重新编译：

- **MOLECULE**：当前文件名、原子数、键数、分子半径
- **DISPLAY**：球棍 / 空间填充切换、原子半径或填充系数缩放、原子标签开关
- **ANIMATION**：自动旋转开关与转速
- **LIGHTING**：环境光、高光强度、光泽度、补光强度（可恢复默认）
- **BACKGROUND**：背景颜色
- **ELEMENT LEGEND**：元素配色与半径图例
- **SELECTION**：选中原子的元素、坐标、键长、键角；两个原子时显示距离

面板标签为英文：ImGui 内置字体不含中文字形，需要中文界面要自行加载中文字体文件。

## 技术要点

- 球体采用 **UV 球（经纬球）** 参数化生成，可通过分段数控制精度
- 圆柱键默认沿 Y 轴生成，用缩放 → 旋转 → 平移的顺序摆放到两原子之间
- 光照采用 **Blinn-Phong**：环境光 + 漫反射 + 镜面高光
- 双光源（主光 + 补光），避免暗部死黑
- 采用 CPK 配色（针对深色背景做亮度适配）
- 开启背面剔除，三角形绕序统一为"从外看逆时针"
- 原子按相机距离排序绘制（远 → 近），缓解穿插造成的 Z-fighting
- 甲烷分子模型：甲烷为正四面体构型，键角 109.5°（碳 sp³ 杂化）
- 自行实现 .mol / .pdb 解析器，无键表时按共价半径自动推断化学键
- 加载分子后按包围盒中心自动居中，并按分子半径自动适配相机距离
- 原子拾取用**射线拾取**：屏幕坐标经逆投影 + 逆视图还原成 3D 射线，再与球体求交
- 原子标签把 3D 坐标经投影矩阵投影回屏幕，用 ImGui 前景层绘制

## 支持的格式

- **MOL（V2000）**：3D 坐标 + 键表
- **PDB**：ATOM / HETATM + CONECT（无 CONECT 时自动推断键）
- MOL（V3000）暂不支持；SDF 只读取第一个分子

## 分子实例

```bash
./build/molecule_viewer                      # 默认加载甲烷
./build/molecule_viewer assets/ethanol.mol   # 乙醇
./build/molecule_viewer assets/benzene.mol   # 苯
./build/molecule_viewer assets/water.mol     # 水
./build/molecule_viewer assets/benzene.pdb   # 用 PDB 格式加载苯
```

也可以从 [PubChem](https://pubchem.ncbi.nlm.nih.gov/) 下载任意分子的 3D MOL 文件，放进 `assets/` 后直接加载。

## 截图

## 许可证

MIT License，详见 [LICENSE](LICENSE)。


