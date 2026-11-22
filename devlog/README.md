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

> 历史说明：Day 15 及更早的文档里提到的 `Prompt.md` 现已改名为 [AGENTS.md](../AGENTS.md)。
> 那些文档是当天的原始记录，不做回溯修改。

## 索引

| 天 | 主题 | 目录 | 主要产出 |
|---|---|---|---|
| 15 | 元素数据库（Atomotheca 模块第 1 天） | [day15-元素数据库](day15-元素数据库/) | `src/orbital/element.{h,cpp}`、`assets/data/elements.json`、`--dump` |

## 每天收尾时我会做三件事

1. 在对应 `dayNN-xxx/` 下写两份 md；
2. 给你可直接复制的 `git commit` 建议（写在「使用说明」第七节）；
3. 更新上面这张索引表。
