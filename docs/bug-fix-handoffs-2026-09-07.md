# 缺陷修复交回验证记录（2026-09-07）

本文档集中记录 `docs/bug-backlog-2026-09-07.md` 中各缺陷单的实施结果，供后续复核、
补充运行证据和发布审计使用。每个缺陷使用独立的 `BUG-xxx` 二级章节；后续修复应追加
对应章节，不覆盖其他缺陷记录。状态、静态检查、自动化测试、Windows Release 编译、
Artifact 校验和真实宿主测试必须分别记录。

## BUG-001：Dock 析构期间的回调与对象生命周期风险

### 交付信息

- **状态**：修复完成／待验证
- **基于的提交**：`120b87bb7fc3d047ed5f2bafb832151f51e86595`
- **修复提交**：本记录所在提交；交回时使用 `git rev-parse HEAD` 核验
- **前置单据及对应提交**：无
- **修改文件**：
  - `CMakeLists.txt`
  - `docs/architecture.md`
  - `docs/bug-fix-handoffs-2026-09-07.md`
  - `src/markdown_preview_dock.cpp`
  - `src/markdown_preview_dock.h`
  - `tests/markdown_preview_dock_lifecycle_test.cpp`

### 问题核实与根因

静态核实 notepad-- v3.8.3 的 `MarkdownView` 初始归编辑器所有并启用
`WA_DeleteOnClose`。插件嵌入预览后将其改为 Dock 子对象，同时保留编辑器销毁时调用
`deleteLater` 的协作路径。原有 `destroyed` 回调捕获 Dock 的 `this`，却没有显式析构
解绑；切换后已经隐藏的预览也保留该回调。Dock 递归删除子控件时，QObject 自动断连晚于
派生类成员析构，存在回调访问已失效成员的风险。

当前没有在真实 Windows 宿主中复现崩溃，证据性质仍为“风险待复现”，不能描述为已经
复现的宿主崩溃。

### 实际修改方案

Dock 现在保存当前及隐藏预览的全部 `destroyed` 连接。显式析构会先设置析构状态、停止
布局计时器、断开滚动连接和全部预览销毁连接，再交给 Qt 父子所有权清理子控件。

普通销毁回调使用独立的对象身份记录判断被销毁对象是否仍是当前预览。只有当前预览关闭
时才清理滚动状态并恢复备用浏览器；隐藏预览关闭不会改变当前界面。实现没有主动删除原生
预览，继续与编辑器的 `deleteLater` 及 Qt 父子所有权协作。

新增四个 Qt Test 用例，覆盖当前预览关闭、隐藏预览关闭、Dock 先销毁多个预览，以及
编辑器先触发 `deleteLater` 后销毁 Dock。

### 相较计划的偏差

没有增加新的预览所有权封装，也没有修改控制器。现有 Qt 父子所有权和 `deleteLater`
协作可以保留，问题集中在 Dock 回调连接的显式生命周期。

未取得 ASan、Windows 内存诊断及真实宿主运行证据，因为当前环境没有 Qt 5.15 开发包和
Windows 宿主。

### 验收标准逐项结果

1. **上述销毁顺序均不访问无效对象**：静态修复完成并添加对应测试源码；运行验证
   `blocked`。
2. **普通关闭预览正确恢复界面**：测试用例已覆盖；运行验证 `blocked`。
3. **已关闭编辑器的预览不会被重新激活**：保留属性清理与 `deleteLater` 协作，并覆盖
   排队删除场景；真实宿主验证 `not verified`。
4. **ASan 或 Windows 内存诊断**：`not run`。
5. **真实宿主关闭测试**：`not verified`。

### 验证证据

| 验证层级 | 结果 | 证据或原因 |
| --- | --- | --- |
| 静态检查 | `passed` | `git diff --check` 通过；核对宿主标签、提交、`MarkdownView` 所有权和槽实现；确认连接登记、析构解绑、ABI 未修改且 `notepad--/` 保持只读 |
| 自动化测试 | `blocked` | 已添加 `markdownview_lifecycle_tests`；CMake 在 `find_package(Qt5 5.15)` 处因找不到 `Qt5Config.cmake` 而失败，测试未运行 |
| Windows Release 编译 | `blocked` | 当前是 Linux 环境，没有 Qt 5.15.2 和 MSVC v142 |
| Artifact 校验 | `not run` | 没有生成 DLL 或打包 Artifact |
| 真实宿主测试 | `not verified` | 未在 notepad-- x64 中执行关闭和多标签测试 |

### 实际环境

- **Qt**：Qt 5.15.2 开发包不可用
- **MSVC**：不可用
- **notepad--**：`v3.8.3`，提交 `91105f68b74382128f3313ac5af8accdc77de918`
- **操作系统**：Linux x86_64
- **测试源码**：`tests/markdown_preview_dock_lifecycle_test.cpp`
- **截图或二进制报告**：未生成

### 未验证项与已知限制

Qt Test 尚未实际编译运行。Windows Release DLL、Artifact、ASan／Windows 内存诊断，
以及 notepad-- x64 中的编辑器先销毁、Dock 先销毁、窗口关闭和多标签手工测试均未完成。
后续复核不能仅根据静态检查将 BUG-001 标记为已关闭。

## BUG-002：标签切换期间预览与编辑器身份不一致

### 交付信息

- **状态**：修复完成／待验证
- **基于的提交**：`ba87695e48576e0711dadbcc2a97832b22efc5e4`
- **修复提交**：本记录所在提交；交回时使用 `git rev-parse HEAD` 核验
- **前置单据及对应提交**：BUG-001，`ba87695e48576e0711dadbcc2a97832b22efc5e4`
- **修改文件**：
  - `CMakeLists.txt`
  - `docs/architecture.md`
  - `docs/bug-backlog-2026-09-07.md`
  - `docs/bug-fix-handoffs-2026-09-07.md`
  - `src/markdown_preview_dock.cpp`
  - `src/markdown_preview_dock.h`
  - `src/preview_controller.cpp`
  - `src/preview_controller.h`
  - `tests/markdown_preview_dock_lifecycle_test.cpp`
  - `tests/preview_controller_document_identity_test.cpp`

### 问题核实与根因

静态核实原实现切换 A 到 B 后，`attachEditor()` 会先把控制器活动编辑器改为 B，而 Dock
在防抖渲染完成前仍显示 A 的原生预览。`scrollEditorToRatio()` 和
`updateSynchronizedScroll()` 仅检查编辑器存在、Dock 可见性及同步开关，没有核对预览
所属编辑器，因此 A 的预览与 B 的编辑器可在该窗口期互相驱动。菜单和工具栏刷新直接调用
`renderNow()`，也没有先重新读取 `editTabWidget` 当前页。

宿主 v3.8.3 使用 `editTabWidget` 管理当前编辑器，并由每个编辑器分别持有其
`MarkdownView`。因此预览身份必须至少包含编辑器对象和内容代次，不能用 Dock 可见性推断。

### 实际修改方案

控制器新增无文档、不支持、待刷新、就绪和失败五种状态，并分别记录活动编辑器、预览
编辑器、内容版本和已渲染版本。标签变化或文本变化会推进内容版本并立即使旧预览失去同步
资格。双向滚动仅在编辑器身份、版本、Dock 预览归属和就绪状态全部匹配时生效。

手工刷新进入渲染前重新读取当前标签。渲染完成、渲染失败和零延迟滚动回调也重新核对当前
编辑器及版本；过期结果被丢弃并为当前文档重新调度。原有 350～2000 ms trailing-edge
防抖保持不变。Dock 新增明确的预览归属记录与查询接口，为后续导出和元数据单据提供边界。

新增 Qt Test，覆盖 A 预览不能滚动新活动编辑器 B、轮询窗口内手工刷新命中 B，以及
A→B→A 后连续三次编辑只在停止输入后刷新一次。同时更新 BUG-001 生命周期测试以适配
Dock 新增的身份参数，未削弱原有覆盖。

### 相较计划的偏差

本单没有提取独立宿主适配层，也没有修改导出逻辑，遵守最小状态模型范围。仍保留 120 ms
轮询作为兼容兜底；全面改为标签事件和滚动事件驱动属于 BUG-015。

### 验收标准逐项结果

1. **A 的预览不能改变 B 的滚动位置**：身份门控完成并添加可区分修复前后行为的测试；
   运行验证 `blocked`。
2. **快速切换后最终只显示正确活动文档**：内容代次和渲染后复核已实现，测试覆盖
   A→B→A；运行验证 `blocked`。
3. **手工刷新针对当前标签**：`renderNow()` 先同步活动编辑器，测试覆盖轮询尚未触发时
   从 A 切 B 后立即刷新；运行验证 `blocked`。
4. **销毁和过期回调不恢复旧内容**：所有同步及渲染完成路径均要求当前身份和版本匹配；
   真实宿主关闭标签验证 `not verified`。
5. **连续输入仍保持防抖**：保留单次 trailing-edge 计时器，测试覆盖连续三次变化只产生
   一次最终刷新；运行验证 `blocked`。

### 验证证据

| 验证层级 | 结果 | 证据或原因 |
| --- | --- | --- |
| 静态检查 | `passed` | `git diff --check` 通过；宿主为 `v3.8.3` / `91105f68b74382128f3313ac5af8accdc77de918`；复核 `editTabWidget`、`on_viewMarkdown`、`on_updataMarkdown`；ABI 与 `notepad--/` 未修改 |
| 自动化测试 | `blocked` | 新增 `markdownview_document_identity_tests`；CMake 在 `find_package(Qt5 5.15)` 处因缺少 `Qt5Config.cmake` 配置失败，测试未编译运行 |
| Windows Release 编译 | `blocked` | 当前为 Linux 环境，没有 Qt 5.15.2 和 MSVC v142 |
| Artifact 校验 | `not run` | 未生成 DLL 或打包 Artifact |
| 真实宿主测试 | `not verified` | 未在 notepad-- x64 中执行 A/B 快速切换、关闭当前标签、双向滚动和连续输入测试 |

### 实际环境

- **Qt**：Qt 5.15.2 开发包不可用
- **MSVC**：不可用
- **notepad--**：`v3.8.3`，提交 `91105f68b74382128f3313ac5af8accdc77de918`
- **操作系统**：Linux x86_64
- **测试源码**：`tests/preview_controller_document_identity_test.cpp`
- **截图或二进制报告**：未生成

### 未验证项与已知限制

Qt Test 尚未实际编译运行。Windows Release DLL、Artifact，以及真实宿主中的 A→B→A、
关闭当前标签、预览与编辑器双向滚动、连续输入防抖体验均未完成。后续 BUG-003 可复用 Dock
的预览身份接口，但本单没有改变现有 HTML 导出语义；复核不能据静态检查直接关闭 BUG-002。
