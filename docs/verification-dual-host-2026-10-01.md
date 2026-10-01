# Notepad-- 3.8.3 与 3.9.0 双版本适配

日期：2026 年 10 月 1 日。本次修复当前源码的宿主字体映射，使同一 Windows x64
插件 DLL 能按两个官方发布包的相同配置协议工作。已发布的 v0.2.9 附件保持原样；
用户实际安装时需使用包含本次修复的候选 DLL。

## 结论与依据更正

**可以同时适配两版，字体映射无需按版本分支。** 实际核对
[3.8.3 官方发布包](https://github.com/cxasm/notepad--/releases/tag/notepad-v3.8.3)和
[3.9.0 官方发布包](https://github.com/cxasm/notepad--/releases/tag/notepad-v3.9.0)，
两版 EXE 的当前主题及指定主题名称访问器均使用相同的 13 项数组。

此前根据 Gitee 公开源码把 18 项表当作“3.8.3 主题表”不准确；两个源码标签指向同一
旧提交，实际发布程序已经使用不同的表。因此修复会同时改善 3.8.3 和 3.9.0 下的
字体跟随，不需要猜测版本、扫描主题目录或增加用户版本开关。

| skinid | 两版发布程序的主题 |
| --- | --- |
| 0–6 | Default、Bespin、Black board、Blue light、Choco、DansLeRuSH-Dark、Deep Black |
| 7–12 | HotFudgeSundae、Monokai、One Dark、Twilight、yellow rice、bean green |

两版包的 SHA256 均匹配官方 Release API；EXE 版本资源分别为 `3.8.3.0` 和 `3.9.0.0`。
原始映射、包／EXE 哈希和反汇编定位 RVA 分别固定在
[3.8.3 证据](../tests/fixtures/host-theme-maps/3.8.3.json)及
[3.9.0 证据](../tests/fixtures/host-theme-maps/3.9.0.json)。

## 代码变化

- `src/saved_markdown_font.cpp` 将旧 18 项表修正为两版发布程序共用的 13 项表，修复
  六个编号对应错误主题目录的问题。用户字体仍优先于安装模板；未找到目标配置时仍
  使用内置字体，不借用其他主题。越界编号从 13 起返回 `invalid-saved-theme`。
- 插件入口、ABI 结构体、Markdown 渲染、预览所有权和滚动接口保持既有实现。
- 字体测试从两个发布包证据文件读取预期值，逐一覆盖 26 个版本／主题组合。每个
  组合同时放置多主题的不同字号和残留旧主题配置，检查精确选中、用户优先、只读行为
  和目标文件缺失回退；补充 13–17 的越界编号验证。

## 验证与边界

本地环境为 Linux aarch64、GCC 13.3.0、Qt 5.15.13、C++14、Release、offscreen。

| 检查 | 结果 |
| --- | --- |
| 两版主题协议与发布包哈希 | passed：两版的两个主题访问器均得到相同 13 项表 |
| Linux Release 编译及八组 Qt 回归 | passed：无编译警告，8/8 组、260 项通过，0 失败／跳过，56.27 秒；含各组初始化与清理 |
| 双版本字体场景 | passed：3.8.3／3.9.0 各 13 个主题，模板与用户配置均匹配 |
| 原六项错误的独立复现程序 | passed：13 个主题全部匹配，mismatches 从 6 降为 0 |
| 发布脚本隔离测试 | passed，未操作真实 Release |
| 本次 Windows Release 构建与回归 | passed：Qt 5.15.2、MSVC v142、x64；8/8 组共 260 项通过，发布脚本测试通过 |
| 候选 Artifact | passed：ZIP／Artifact 校验和、三项文件清单、DLL x64 和两个入口导出均通过 |
| 候选 DLL 对两版运行库的导入检查 | passed：分别匹配 3.8.3 和 3.9.0 的全部 1,112 个 Qt／MSVC 导入符号 |
| 修复后的 Windows 宿主加载与交互 | not verified：当前环境无 Windows，用户此前确认 3.9.0 尚未实测 |

本地日志位于已忽略的 `build/compatibility-3.9.0/`：`dual-build.log`、
`dual-ctest.log`、`regression/markdownview_font_tests.txt`、
`dual-font-mapping-probe.json`、`dual-release-tests.log`。

[草稿 PR #3](https://github.com/jiy29104983/markdownviewdd/pull/3)；
[Windows 验证流水线](https://github.com/jiy29104983/markdownviewdd/actions/runs/36874277432)。
该流水线对应代码提交 `2af72ffe8344c673df6b849cf3f95789350beed2`，构建 PR 合并引用
`1164d4c`。后续验证记录提交仅改文档，不改变经验证的插件源码和测试。

候选包 `markdownviewdd-v0.2.9-windows-x64-1164d4c.zip` 的 SHA256：

```text
d8cd682a833c086202829b5faeda357ac63fdd07edf9d9d1c068e4b2c8565564
```

包内 `plugin/markdownviewdd.dll` 为 405,504 字节，SHA256：

```text
5275bcadaeb6460b66b556e7c5fa383801b286cdade1531e6f22ffab5b9c1051
```

候选包及解包后的 Windows 测试文本保存在本地
`build/compatibility-3.9.0/windows-candidate/`，结构化核验结果在
`build/compatibility-3.9.0/dual-validation.json`。Windows 系统 DLL 的实际加载仍需
宿主实测；符号对照只覆盖发布包中的 Qt／MSVC 依赖。

原 v0.2.9 的 Windows 构建成功记录不替代本次修复后的构建。自动化通过也不代表已
完成实际宿主加载。两版的手工验收至少包括加载／重开、六个受影响主题的自定义字体、
恢复宿主字号、Ctrl+滚轮、切换标签、多窗口和标题跳转；完整步骤见
[修复前报告的实机验收清单](verification-notepad-3.9.0-2026-10-01.md#windows-实机验收清单)。
