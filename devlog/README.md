# devlog · 开发日志

这个目录存**每一天的开发记录与说明文档**。代码在 `src/`，文档在这里，两边分开。

## 约定

每个开发日一个子目录，命名 `dayNN-主题`：

```
devlog/
├── README.md                      ← 本文，索引
├── day15-元素数据库/
│   ├── 使用说明-第15天-元素数据库.md   六段式交付说明 + 改动内容 + commit 建议
│   ├── 第15天实现功能原理.md           面向零基础的原理推导
│   └── README-建议修改版.md           只给建议，不直接覆盖根目录的 README
├── molecule-viewer-day15.zip      打包结果（已被 .gitignore 忽略）
└── molecule-viewer-win64-mingw.zip 历史 Windows 包（已被 .gitignore 忽略）
```

规则：

1. **每个子目录至少两份文档**：`使用说明-第N天-xxx.md`（怎么用、改了什么）和
   `第N天实现功能原理.md`（为什么这么做）。
2. **zip 不提交**。`.gitignore` 里有 `devlog/*.zip`，打包结果随时能重建，
   塞进 git 只会让仓库变大。
3. **文档进 git，代码改动进 commit**。这个目录回答「那天为什么这么改」，
   `git log` 回答「那天改了哪些行」，两者互补，不要互相替代。
4. 文件名保留 `第N天`，因为 [AGENTS.md](../AGENTS.md) 的交付约定就是按天编号；
   目录名用 `dayNN`，排序整齐。
5. **`使用说明` 的最后一节固定是「今日功能的变化与修改」**，且必须附一段
   可直接粘贴的 Release 正文。原因见下面「版本与发版」。

> 历史说明：Day 15 及更早的文档里提到的 `Prompt.md` 现已改名为 [AGENTS.md](../AGENTS.md)。
> 那些文档是当天的原始记录，不做回溯修改。

## 索引

| 天 | 主题 | 目录 | 主要产出 |
|---|---|---|---|
| 15 | 元素数据库（Atomotheca 模块第 1 天） | [day15-元素数据库](day15-元素数据库/) | `src/orbital/element.{h,cpp}`、`assets/data/elements.json`、`--dump` |
| 16 | 元素周期表面板（模块第 2 天） | [day16-元素周期表](day16-元素周期表/) | `src/ui/periodic_table.{h,cpp}`、族号表头、P 键开关、`--dump-table`、元素库补到 36 个 |
| 17 | 2D 能级图（模块第 3 天） | [day17-能级图](day17-能级图/) | `src/ui/energy_diagram.{h,cpp}`、洪特规则箭头、`energySequence()`、`--dump-levels` |
| 18 | 3D s / p 轨道形状（模块第 4 天） | [day18-轨道形状](day18-轨道形状/) | `src/orbital/shape.{h,cpp}`、`orbital.vert/.frag`、不透明/透明两阶段、`--dump-orbital` |

## 每天收尾时我会做四件事

1. 在对应 `dayNN-xxx/` 下写两份 md；
2. 在 `使用说明` 的最后一节写「今日功能的变化与修改」，并附一段 Release 正文；
3. 给你可直接复制的 `git commit` 建议（写在「使用说明」第七节）；
4. 更新上面这张索引表。

## 版本与发版

发布流程由 [.github/workflows/build.yml](../.github/workflows/build.yml) 负责，
**这份文件由用户自己维护，不要改**：

- push 到 `main` → 三平台编译验证，不发版；
- push `v*` tag → 三平台编译 + 自动创建 Release（附三个平台的包）；
- 手动触发 → 只编译。

Release 正文用的是 `generate_release_notes: true`，也就是 GitHub 按
**commit 标题**自动生成。它只能回答「改了哪些文件、加了什么功能」，
回答不了「这一天到底多了什么能力、有什么行为变化、哪里还不能用」。
所以每天文档的「今日功能的变化与修改」不是重复劳动，它是给人看的版本说明；
commit 标题是给机器聚类的。

| tag | 指向 | 覆盖范围 |
|---|---|---|
| `v0.1.0` | Day 13 收尾（`85fba71`） | 球棍/填充模型、文件解析、ImGui 面板、原子拾取 |
| `v0.2.0` | Day 18 收尾 + CI 修好（`dad288b`） | 元素数据库、周期表、能级图、s/p 轨道；MinGW job 改用 checkout 拉依赖 |
| `v0.3.0`（建议） | Day 19 收尾后 | d 轨道 + 球谐函数 |

打 tag 的时机：当天文档写完、编译零告警、用户确认功能没问题之后。

版本号的唯一来源是 `CMakeLists.txt` 里的 `project(... VERSION x.y.z)`，
它经 `APP_VERSION` 编译进程序，`--version` 与窗口启动日志都会打印。
**tag 名必须和这个数字一致**，否则用户下载的包跑起来会自报另一个版本：

- 之前 `project(VERSION 0.14.0)`（按天数写的）配 `v0.2.0` 的 tag，
  包里跑起来打印 `v0.14.0`，属于自相矛盾；打完 tag 之后已改成 `0.2.0`。
- 从此只按语义化版本走：`0.2.0` → 下一个功能版本 `0.3.0`，
  不再用天数当版本号（天数在 `devlog/` 目录名里已经表达了）。
- 发版顺序：改 `project(VERSION)` → 编译 → 提交 → 打 tag → push。
  `main.cpp` 里 `#ifndef APP_VERSION` 的后备值只是给非 CMake 构建兜底，
  也要跟着改，但以 CMake 为准。


