# AGENTS.md

Atomotheca（原子典）是一个用 C++17 + OpenGL 3.3 手写的分子与原子结构 3D 可视化程序。
面向使用者的文档在 [README.md](README.md)，每日开发记录在 [devlog/](devlog/)。
本文件只放「代理干活需要、但不该塞进 README」的东西：构建命令、验证方式、架构约束、已知坑。

## 项目概览

- 学习导向的桌面工具，不是工业级产品。不追求渲染百万原子，不做量子化学计算、不做分子动力学。
- 分子查看器已完成：`.mol`(V2000) / `.pdb` 加载、球棍与空间填充模型、轨道相机、Blinn-Phong 双光源、ImGui 面板、射线拾取、键长键角测量、拖拽与文件对话框、无键表时按共价半径推断化学键。
- Atomotheca 模块进行中：已完成 JSON 元素库（36 个元素，H–Kr，前四周期完整）、电子排布、离子推算、元素周期表面板（18 族网格 + 族号表头 + 点击详情 + 离子切换）；待做 2D 能级图、s/p/d 轨道 3D、电子云点云。
- 化学数据一律放 `assets/data/*.json`，C++ 代码里禁止硬编码大段化学数据。

### 技术栈（已锁定，不要更换）

| 用途 | 选型 | 说明 |
|---|---|---|
| 语言 | C++17 | 不用 C++20 特性 |
| 图形 API | OpenGL 3.3 Core | 不用 Vulkan / DirectX |
| 窗口与输入 | GLFW 3.4（≥ 3.3） | 不用 SDL、不用平台原生窗口 API |
| 函数加载 | GLAD | `external/glad/`，随仓库提供 |
| 数学库 | GLM | 系统包或 `external/glm/` |
| UI | Dear ImGui 1.90.9 | 不用 Qt、不用 Web 前端 |
| 构建 | CMake ≥ 3.16 | |
| JSON | nlohmann/json 3.11.3 单头文件 | `external/json/nlohmann/json.hpp`，随仓库提供 |
| 脚本 | Python 3 | 只用于生成测试资源，不参与运行时 |

- 禁止引入：Qt、SDL、Vulkan、bgfx、assimp、任何需要包管理器安装的运行时依赖、任何体积超过 5 MB 的第三方库。

## 环境准备

```bash
sudo apt install build-essential cmake git libglfw3-dev libgl1-mesa-dev libglm-dev
git clone --depth 1 --branch v1.90.9 https://github.com/ocornut/imgui.git external/imgui
```

- `external/glad/`、`external/json/`、`external/glm/` 已随仓库提供，不要重复下载。
- `external/imgui/` 必须自己 clone（已被 gitignore）。
- `external/glfw/` 只在交叉编译时 clone；原生构建优先用系统 `libglfw3-dev`。
  原因：源码编 GLFW 3.4 的 X11 后端还需要 `libxinerama-dev`、`libxcursor-dev`、`libxi-dev`、`libxrandr-dev`，而 `libglfw3-dev` 已经把这些依赖编进去了。
- 用户机器：Debian 13，NVIDIA GTX 1650，OpenGL 3.3.0。

## 构建与运行

```bash
# Linux 原生（默认走系统 GLFW）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/molecule_viewer

# 交叉编译 Windows
git clone --depth 1 --branch 3.4 https://github.com/glfw/glfw.git external/glfw
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw64.cmake
cmake --build build-win -j$(nproc)

# Web / WASM（需要激活过 emsdk；preload 参数已在 CMakeLists 里配好）
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web -j$(nproc)
```

- 构建必须零告警。当前基线：GNU 14.2.0，0 warning。新增告警视为未完成。
- 不要 `rm -rf build/`（文件数超过安全删除阈值会被拦）。要全新构建就用独立目录，如 `cmake-build-fresh`、`cmake-build-<用途>`（已被 `.gitignore` 的 `cmake-build-*/` 覆盖），或用 `cmake -E remove_directory`。
- 资源走相对路径，三级回退：当前工作目录 → exe 目录 → exe 上级。所以在项目根目录或 `build/` 里运行都能找到 `assets/` 与 `src/shaders/`。
- 改过 `CMakeLists.txt` 里 GLFW 的选择逻辑后，注意 `CMakeCache.txt` 里的旧值：历史上用 `set(... FORCE)` 写过 `GLFW_BUILD_X11=OFF`，不清缓存会一直编出 null 后端。

## 测试与验证

项目没有单元测试框架，验证靠下面几条，全部要真跑：

```bash
./build/molecule_viewer --help
./build/molecule_viewer --version
./build/molecule_viewer --dump Fe     # 期望 1s2 2s2 2p6 3s2 3p6 3d6 4s2，且 Fe2+/Fe3+ 为 3d6 / 3d5
./build/molecule_viewer --dump O      # 期望 1s2 2s2 2p4
./build/molecule_viewer --dump fe     # 小写应自动纠正
./build/molecule_viewer --dump Xx     # 期望退出码 255 并列出可用符号
./build/molecule_viewer --dump Cr     # 期望 3d5 4s1（例外元素），Cr3+ 为 3d3
./build/molecule_viewer --dump Cu     # 期望 3d10 4s1（例外元素），Cu+ 为 3d10
./build/molecule_viewer --dump-table  # 期望 4 行 18 族全满，H 在第 1 族、He 在第 18 族、Kr 在第 4 周期第 18 族
```

- `--dump` 是纯数据层验证，不打开窗口，可在无显卡环境跑。改数据层必须跑这套。
- 改渲染、UI、交互：编译通过不足以证明正确，必须请用户在真实桌面会话里启动一次。
- 代理所在沙箱通常**没有 GPU、没有可用 GLX**（无 `/dev/dri`，`glxinfo -B` 自己就报 `GLX: Failed to create context`）。所以涉及窗口的结论一律写「未实机验证」，不许写成「已验证」。
- 36 个元素逐个对照课本核对过排布式；新增元素时同样逐个核，不要只看总数。
- 例外必须显式写进 JSON：Cr 是 `3d5 4s1`、Cu 是 `3d10 4s1`，都不是 Madelung 规则的产物。数据层不做「规则修正」，算出来的和权威值不一致时以 JSON 为准。

## 目录结构

```
src/
├── main.cpp          1502 行  应用状态、GLFW 回调、ImGui 面板、主循环（待拆分）
├── shader.h           178     着色器编译与 uniform 封装
├── sphere.h           105     UV 球网格生成（纯数学）
├── cylinder.h          80     圆柱侧面网格
├── molecule.h         100     Atom / Bond / Molecule、元素样式表
├── molfile.h          473     .mol / .pdb 解析、键推断、包围盒居中
├── picking.h          133     屏幕坐标 → 射线 → 原子命中
├── path_utils.h       105     资源路径三级回退
├── file_dialog.h      267     系统文件对话框（Win32 / zenity / osascript）
├── orbital/element.h  143     亚层 / 离子 / 元素 / 数据库声明
├── orbital/element.cpp 537    JSON 解析、校验、电子排布、离子推算
├── ui/periodic_table.h  39    周期表面板状态与入口声明
├── ui/periodic_table.cpp 340  18 族网格 + 族号表头、格子自绘、详情区、ASCII 版
└── shaders/                   sphere.vert/.frag、cylinder.vert/.frag

assets/            示例分子 .mol / .pdb，以及 data/elements.json（36 个元素）
devlog/dayNN-主题/  每日交付文档（md 进 git，zip 不进）
external/          glad、json、glm 随仓库；imgui、glfw 由使用者 clone
```

## 代码风格

- 注释用中文，标识符用英文。注释要写「为什么」，不要写「这里初始化」。
- 头文件用 `#pragma once`；`.h` 只放声明，实现放 `.cpp`。
- 优先 `std::vector`、`std::string`、`std::unique_ptr`，不用裸 `new`。
- 不用模板元编程、不用复杂宏、不用 `std::enable_if` 这类炫技写法。
- 关键数学公式、OpenGL 状态机操作必须写注释。
- 分层：只有 `main.cpp` 和 `shader.h` 可以 `#include <glad/glad.h>`；`molecule.h`、`molfile.h`、`orbital/*` 一律不许碰 OpenGL。
- 平台相关代码只能出现在 `path_utils.h`、`file_dialog.h` 这类适配文件里，并且用 `#ifdef` 隔离。

## 架构铁律

1. **模块化**：不把所有逻辑塞进 `main.cpp`。它只能是入口和主循环调度器；渲染 / 业务 / UI / 数据四层分开。
2. **为跨平台与重构留后路**：OpenGL 之上保留可抽出的 `Renderer` / `Mesh` 边界；化学模块与渲染后端解耦。
3. **性能警觉**：渲染循环内禁止动态分配（`new`、`push_back` 扩容），网格与缓冲区在循环外构建；原子球体未来要走向 per-instance 数据，不要每帧重建 VBO。
4. **零基础友好**：给代码前先说明这段在 CPU / GPU / 显存上做了什么；不用用户没学过的语法。
5. **不推翻重来**：在原代码上增量演进，不改技术栈、不换架构风格。

## 开发工作流

- **一次只推进一天**。用户说「开始第 N 天」时，只输出第 N 天，不要顺手把后面几天写了。
- 每天的输出固定六段：今日目标 / 核心概念 / 架构建议 / 代码实现（改现有文件要给修改前后对比）/ 编译与验证 / 排错清单。
- 每天收尾产出三样：
  - `devlog/dayNN-主题/使用说明-第N天-xxx.md`（怎么用 + 逐文件「改了什么/为什么改/影响哪些行为」+ 可直接复制的 Git commit 建议）
  - `devlog/dayNN-主题/第N天实现功能原理.md`（面向零基础的原理推导）
  - 一份 zip（代码按原路径打包，不提交 git）
- README 默认只给 `README-建议修改版.md`，不直接覆盖；用户明确要求「直接改 README」时才覆盖。
- 收尾更新 `devlog/README.md` 的索引表和本文的进度表。

## 当前进度

| 阶段 | 内容 | 状态 |
|---|---|---|
| Day 1–6 | 窗口、GLAD、VBO/VAO/EBO、GLM、轨道相机、球体与光照 | 完成 |
| Day 7–9 | 球棍模型、空间填充与 Z-fighting、CPK 配色与双光源 | 完成 |
| Day 10–14 | .mol/.pdb 解析、ImGui 面板、射线拾取、跨平台路径、Emscripten | 完成（Web 仅编译级） |
| 附加 | 拖拽与文件对话框、三平台 CI、MinGW 交叉编译 | 完成 |
| Day 15 | 元素数据库 + 电子排布（Atomotheca 第 1 天） | 完成 |
| Day 16 | ImGui 元素周期表面板（18 族网格、族号表头、点击详情、离子切换、`--dump-table`） | 完成，最新 |
| Day 17 | 2D 能级图（`ImDrawList` 画方框、↑↓ 箭头、4s 在 3d 下方） | 待开始 |
| Day 18 | 3D s / p 轨道（球 + 哑铃，半透明，p_x/p_y/p_z 红绿蓝） | 待开始 |
| Day 19 | 3D d 轨道与球谐函数（d_xy / d_yz / d_z²，UI 下拉切换） | 待开始 |
| Day 20 | 电子云点云（`GL_POINTS`，概率密度撒点，点大小与透明度） | 待开始 |

Day 20 之后做架构拆分，见下面技术债。

## 技术债

| 问题 | 现状 | 计划 |
|---|---|---|
| `main.cpp` 1445 行 | 状态、回调、UI、渲染混在一起 | Day 20 后拆成 `app/` `render/` `ui/` |
| 应用状态没有归属 | `g_app` / `g_ui` / `g_selected` / 相机角度都是文件级全局变量，回调直接读写 | 拆分前先定数据流归属，否则只是搬代码 |
| 透明渲染未排序 | 第 18 天轨道需要半透明 | Day 18 必须处理：先不透明，再按距离画透明，关闭深度写入 |
| `element.cpp` 按值返回 `std::vector<Subshell>` | 调用频率低，暂无问题 | Day 17 每帧画能级图时改为缓存 |
| `elements.json` 只到 Kr（Z=36） | 5–7 周期未收录，f 区完全没有 | 界面按 `maxPeriod` 自适应，会显示 5 行淡框「未收录」；要扩需同时设计镧系锕系的行外摆放 |
| Web / WASM 未实机验证 | 只有编译级结论 | 后续在 CI 或本地补 |
| Windows MSVC 产物未实机验证 | CI 编过，未双击运行 | 需要一台 Windows |
| 中文 UI 缺字形 | ImGui 默认字体不含中文 | 面板标签暂用英文；要中文得加载字体文件 |

## 已知问题与排错

| 现象 | 根因 | 处理 |
|---|---|---|
| `[错误] GLFW 初始化失败！` 但显卡正常 | CMake 曾无条件 `GLFW_BUILD_X11=OFF` / `WAYLAND=OFF`，编出 null 后端：能编译能链接，`glfwInit()` 必失败 | 原生构建走 `find_package(glfw3)`；源码分支按 `CMAKE_SYSTEM_NAME` 判断，并用 `unset(... CACHE)` 清缓存。排查看 `ldd` 有无 X11 与 `x11_init.c.o` |
| 球体和圆柱渲染出内壁，原子穿透到前面 | 三角形绕序反了（CW 当 CCW） | 索引顺序统一为从外看逆时针；开背面剔除后画错会直接消失，是最快的发现方式 |
| Fe²⁺ 算成 `3d4 4s2` | 按 Aufbau 逆序删电子 | 阳离子按「先失最外层」：n 大的先失，n 相同 l 大的先失 |
| MSVC 报 C2666 重载不明确 | `MultiByteToWideChar` 参数个数与缓冲区长度算错 | 参数 6 个；按 `need` 分配后 `pop_back()` |
| MSVC 报 `min`/`max` 语法错误 | `<windows.h>` 的宏污染 | 编译选项加 `NOMINMAX` |
| MSVC 中文乱码 | 源码是 UTF-8 | 编译选项加 `/utf-8` |
| MinGW CI 报 `git: command not found`，退出码 127 | MSYS2 环境隔离，没装 git | 在 `install:` 列表补 `git` |
| 拷贝 exe 到别的机器缺 DLL | MinGW 默认动态链接 GCC 运行时 | 加 `-static -static-libgcc -static-libstdc++`，分发前 `objdump -p` 查依赖 |
| 着色器读取失败 | 文件名写错（`rect.flag`） | 逐字核对报错里的文件名 |
| `glad/glad.h` 找不到 | 文件被平铺到 `external/` 根 | 必须是 `external/glad/include/glad/glad.h` |

## 安全与注意事项

- 只读用户数据：不修改 `assets/`、不覆盖用户文件；新增示例分子另存新文件。
- 不自动执行 `git commit` / `git push`：给出可直接复制的建议命令，等用户确认。用户明确授权时才代执行。
- 不删构建目录（`rm -rf build` 会被安全策略拦），不碰 `~/`、`/etc`、`~/.config` 等个人目录。
- 不下载或执行二进制文件；不引入需要网络运行时的依赖。
- 不修改用户的 shell 配置、全局 CMake 配置、系统包状态；需要装包时把 `apt` 命令写给用户，由用户执行。
- 数据文件只走本地 `assets/data/`，不请求任何外部 API。

## 提交与 PR 指南

- 提交信息用 Conventional Commits 前缀 + 中文摘要，例如 `feat(orbital): 新增元素周期表数据校验`、`fix(build): GLFW 回退到系统库`。
- 一个逻辑改动一个提交；「改代码」与「补文档」分开提交。
- 提交前必须：`cmake --build build -j$(nproc)` 零告警 + 跑一遍「测试与验证」里的命令。
- 本项目没有 PR 流程，改动直接进 `main`；但提交前要告诉用户改了哪些文件、验证到什么程度。

## 与用户协作

- 直接说结论，不要开场客套，不奉承；用户写错了就直说。
- 有不同意见就说，指出问题并给替代方案。
- 说「已验证」之前必须真的跑过；没跑过就说「静态分析，未实机验证」。
- 不确定就说不确定，不要编造 API 或命令。
- 中文为主，技术术语保留英文原文（framebuffer、winding order、half-vector）。
- 用户是从零学图形学，解释概念要打比方，但不要用错误类比。
