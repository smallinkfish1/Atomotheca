# Molecule Viewer（分子可视化）

用 C++ + OpenGL 实现的有机物分子 3D 可视化程序，支持**球棍模型**与**空间填充模型**切换。

## 功能规划
- [x] 第 1 天：GLFW 空窗口
- [x] 第 2 天：GLAD 初始化 + 渲染循环
- [x] 第 3 天：VBO/VAO/EBO 与着色器
- [x] 第 4 天：GLM 与 3D 变换
- [ ] 第 5 天：轨道相机（旋转/缩放）
- [ ] 第 6 天：球体网格与光照
- [ ] 第 7 天：球棍模型（CH₄）
- [ ] 第 8 天：空间填充模型与模式切换
- [ ] 第 9 天：元素配色与 Blinn-Phong 光照
- [ ] 第 10 天：.mol/.pdb 解析器
- [ ] 第 11 天：Dear ImGui 控制面板
- [ ] 第 12 天：原子拾取与键长键角
- [ ] 第 13 天：跨平台构建与开源发布
- [ ] 第 14 天：Emscripten / WebAssembly

## 环境要求
- CMake ≥ 3.16
- C++17 编译器（g++ 11+）
- GLFW ≥ 3.3

## 在 Debian 13 (Trixie) 上编译
```bash
sudo apt install build-essential cmake git libglfw3-dev libgl1-mesa-dev
cmake -S . -B build
cmake --build build -j
./build/molecule_viewer
