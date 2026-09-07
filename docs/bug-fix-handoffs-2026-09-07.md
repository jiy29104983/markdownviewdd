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

## BUG-003：HTML 导出选错文档、导出提示页或旧内容

### 交付信息

- **状态**：修复完成／待验证
- **基于的提交**：`e71f14f4c067b271711c128259cf4f6e164025bb`
- **修复提交**：本记录所在提交；交回时使用 `git rev-parse HEAD` 核验
- **前置单据及对应提交**：BUG-002，`e71f14f4c067b271711c128259cf4f6e164025bb`
- **修改文件**：
  - `README.md`
  - `docs/architecture.md`
  - `docs/bug-backlog-2026-09-07.md`
  - `docs/bug-fix-handoffs-2026-09-07.md`
  - `src/markdown_preview_dock.cpp`
  - `src/markdown_preview_dock.h`
  - `src/preview_controller.cpp`
  - `src/preview_controller.h`
  - `tests/preview_controller_document_identity_test.cpp`

### 问题核实与根因

静态核实原 `activeDocument()` 以原生预览控件是否可见来选择导出文档。Dock 隐藏后，原生
控件的 `isVisible()` 为假，导出会改取备用 `QTextBrowser`；该浏览器可能没有内容，也可能
保留 `showMessage()` 写入的提示或错误 HTML。由于提示 HTML 非空，原逻辑会将其视为有效
预览并允许保存。

菜单导出动作还直接调用 Dock，没有先同步 `editTabWidget` 当前页，也没有检查 BUG-002
建立的活动编辑器、预览编辑器、内容版本和已渲染版本。在文本变化后的防抖窗口或 A→B
切换后的轮询窗口中，因此可能导出旧版本或错误标签。该问题已由静态调用链确认；真实宿主
中的具体表现尚未运行复现。

### 实际修改方案

导出语义收敛为“当前活动 Markdown 文档的最新有效预览”。控制器新增当前 HTML 快照准备
入口：先同步当前标签，检查文档类型；若预览身份或版本尚未就绪，则即使 Dock 已隐藏也通过
宿主 `on_viewMarkdown`／`on_updataMarkdown` 同步完成一次最新渲染。渲染后再次核对编辑器
和内容版本，只有 BUG-002 状态模型与 Dock 预览归属完全匹配才生成快照。

Dock 不再根据控件可见性选择导出数据源，只从匹配身份和版本的原生 `QTextDocument` 生成
HTML 字节。`showMessage()` 会立即失效预览身份，提示页和错误页不能成为快照来源。HTML
字节和源文件路径在打开保存对话框前复制完成，即使模态对话框期间发生标签变化，待写入数据
也不会改变。

快照生成、保存交互和文件写入已分开。取消文件对话框直接返回，不调用写入；文件写入继续
使用 `QSaveFile`，短写时显式取消，提交失败返回清晰错误。导出动作会随当前文档是否为支持的
Markdown 类型启停。README 已写明导出对象、隐藏侧栏、提示页、取消和相对资源边界。

扩展现有 Qt Test，覆盖隐藏 Dock 后编辑并立即导出、A→B 未轮询即导出、提示页及身份不匹配
拒绝导出、非 Markdown 文档禁用动作，以及无效输入不改已有文件和原子替换成功。

### 相较计划的偏差

没有引入独立导出服务类，而是在现有 `PreviewController` 与 `MarkdownPreviewDock` 边界内
增加可测试的快照和写入接口，避免为单一功能扩大架构范围。没有复制、重写或嵌入相对图片；
跨目录资源迁移仍属于 BUG-008。文件另存为或重命名后的路径元数据刷新仍属于 BUG-005。

### 验收标准逐项结果

1. **隐藏与显示时导出对象一致**：快照接口不再读取控件可见性，隐藏 Dock 用例已覆盖最后
   编辑内容和 Dock 保持隐藏；测试运行 `blocked`。
2. **提示页不能导出**：`showMessage()` 显式失效预览身份，测试验证提示 HTML 不能取得
   快照；测试运行 `blocked`。
3. **立即导出包含最后一次编辑**：文本变化推进内容版本，导出准备会同步刷新未就绪版本；
   隐藏 Dock 后立即编辑和导出用例已覆盖；测试运行 `blocked`。
4. **快速切换不串文档**：快照准备先同步当前标签并在渲染后复核身份；A 已渲染、切 B 后
   未等待轮询即导出的用例验证只取得 B；测试运行 `blocked`。
5. **取消不写文件**：文件对话框返回空路径时直接返回；写入接口空路径测试源码已覆盖；
   实际文件对话框运行 `not verified`。
6. **写入失败不破坏已有文件**：保留 `QSaveFile` 原子临时文件与提交语义，短写显式取消；
   测试源码覆盖无效快照不改已有文件和成功原子替换，实际磁盘提交失败场景 `not verified`。

### 验证证据

| 验证层级 | 结果 | 证据或原因 |
| --- | --- | --- |
| 静态检查 | `passed` | `git diff --check` 通过；复核 BUG-002 身份接口、notepad-- v3.8.3 的 `on_viewMarkdown`／`on_updataMarkdown` 同步调用；ABI 和 `notepad--/` 未修改 |
| 自动化测试 | `blocked` | 已扩展 `markdownview_document_identity_tests`；`cmake -S . -B /tmp/markdownview-bug003-build -DBUILD_TESTING=ON` 在 `find_package(Qt5 5.15)` 因缺少 `Qt5Config.cmake` 失败 |
| Windows Release 编译 | `blocked` | 当前为 Linux 环境，没有 Qt 5.15.2 和 MSVC v142 |
| Artifact 校验 | `not run` | 未生成 DLL 或打包 Artifact |
| 真实宿主测试 | `not verified` | 未在 notepad-- x64 中执行隐藏／显示导出、立即编辑、快速切换、取消和写入失败测试 |

### 实际环境

- **Qt**：Qt 5.15.2 开发包不可用
- **MSVC**：不可用
- **notepad--**：`v3.8.3`，提交 `91105f68b74382128f3313ac5af8accdc77de918`
- **操作系统**：Linux x86_64
- **测试源码**：`tests/preview_controller_document_identity_test.cpp`
- **CMake 配置目录**：`/tmp/markdownview-bug003-build`
- **截图或二进制报告**：未生成

### 未验证项与已知限制

Qt Test 尚未实际编译运行。Windows Release DLL、Artifact，以及真实宿主中的隐藏侧栏导出、
编辑后立即导出、A→B 快速切换、取消保存和磁盘写入失败均未完成。跨目录导出的相对图片仍
不会自动复制或嵌入，按 BUG-008 处理；文件路径动态变化后的元数据刷新按 BUG-005 处理。
后续复核不能仅凭静态检查将 BUG-003 标记为已关闭。

## BUG-004：同进程多窗口只初始化一个插件控制器

### 交付信息

- **状态**：修复完成／待验证
- **基于的提交**：`5c54499e5204376ac629ad6012f9564d888708ff`
- **修复提交**：`9121b4b85b97b621b503ae1ba171d486eba051d0`
- **前置单据及对应提交**：BUG-001，`ba87695e48576e0711dadbcc2a97832b22efc5e4`
- **修改文件**：
  - `CMakeLists.txt`
  - `docs/architecture.md`
  - `docs/bug-backlog-2026-09-07.md`
  - `docs/bug-fix-handoffs-2026-09-07.md`
  - `docs/host-compatibility.md`
  - `src/plugin_exports.cpp`
  - `src/preview_controller.cpp`
  - `src/preview_controller.h`
  - `tests/plugin_multi_window_test.cpp`

### 问题核实与根因

静态核实原插件由进程级 `QPointer<PreviewController> g_controller` 保存唯一控制器。宿主
v3.8.3 的 `openFileInNewWin()` 会创建新的 `CCNotePad` 窗口，而每个窗口加载插件菜单时都通过
`sendParaToPlugin()` 将自身和新建根菜单传入 `NDD_PROC_MAIN`。首个窗口已设置全局控制器后，
第二个窗口会跳过菜单和 Dock 初始化却返回成功。

若改为每窗口实例但保留 `Qt::ApplicationShortcut`，同进程控制器还会争抢相同快捷键。控制器
安装在 `qApp` 上的事件过滤器也需明确核对右键菜单和当前编辑器确实属于自身宿主窗口。

### 实际修改方案

移除进程级全局控制器。入口现在从本次 `notepad` 的直接子对象中查找
`PreviewController`：未找到时为该窗口创建，找到时按重复入口处理。`installMenu()` 保存已安装
的根菜单，同一窗口和同一菜单的重复调用直接成功，不再添加第二组动作、计时器、Dock 或事件
过滤器；异常传入另一根菜单时返回失败。

控制器、Dock、动作和计时器仍由各自宿主窗口的 Qt 父子所有权管理，关闭一个窗口会清理它
自己的实例，不触碰其他窗口。显示／隐藏快捷键改为 `Qt::WindowShortcut`。右键菜单事件只有
在菜单、当前编辑器和控制器宿主窗口三者归属一致时才桥接。

新增 `markdownview_multi_window_tests`，覆盖两个窗口分别预览和导出、同窗重复初始化的对象
及动作计数、关闭首窗后第二窗继续刷新和导出，以及两窗口右键菜单桥接互不影响。

### 相较计划的偏差

没有建立额外的进程级容器，而将宿主窗口的直接子对象集合用作受控注册表。该实现可由 Qt
所有权自动清理，避免悬空注册项，且足以满足按窗口实例化和重复入口幂等。未改动插件 ABI、
宿主源码或导出签名。

### 验收标准逐项结果

1. **两个同进程窗口独立预览、刷新、导出**：专用回归测试源码已覆盖；运行 `blocked`。
2. **操作互不影响**：测试分别核对 Dock 可见性、渲染次数、HTML 内容和右键菜单；运行
   `blocked`。
3. **关闭任一窗口不破坏另一窗口**：测试以 `QPointer` 核对首窗控制器清理后第二窗继续
   刷新和导出；运行 `blocked`。
4. **重复入口不增加重复动作**：测试核对控制器、Dock、计时器和菜单动作数量；运行
   `blocked`。
5. **没有快捷键歧义警告**：动作改用 `Qt::WindowShortcut` 并有属性断言；真实宿主警告
   检查 `not verified`。

### 验证证据

| 验证层级 | 结果 | 证据或原因 |
| --- | --- | --- |
| 静态检查 | `passed` | `git diff --check` 通过；复核宿主 `openFileInNewWin()`、`quickshow()`、插件菜单加载和 `sendParaToPlugin()`；ABI 与 `notepad--/` 未修改 |
| 自动化测试 | `blocked` | 新增 `markdownview_multi_window_tests`；CMake 在 `find_package(Qt5 5.15)` 因缺少 `Qt5Config.cmake` 配置失败 |
| Windows Release 编译 | `blocked` | 当前为 Linux 环境，没有 Qt 5.15.2 和 MSVC v142 |
| Artifact 校验 | `not run` | 未生成 DLL 或打包 Artifact |
| 真实宿主测试 | `not verified` | 未在 notepad-- x64 中执行新窗口、双窗口快捷键、关闭任一窗口和重复入口测试 |

### 实际环境

- **Qt**：Qt 5.15.2 开发包不可用
- **MSVC**：不可用
- **notepad--**：`v3.8.3`，提交 `91105f68b74382128f3313ac5af8accdc77de918`
- **操作系统**：Linux x86_64
- **测试源码**：`tests/plugin_multi_window_test.cpp`
- **截图或二进制报告**：未生成

### 未验证项与已知限制

Qt Test 尚未实际编译运行。Windows Release DLL、Artifact，以及真实宿主中的“在新窗口打开”、
两个窗口分别预览／刷新／导出、快捷键作用域、关闭任一窗口和重复入口均待复核。
`Diagnostics::resetLog()` 仍在每次窗口入口清空共享日志，该独立问题按 BUG-016 处理；后续复核
不能仅凭静态检查关闭 BUG-004。

## BUG-005：文件路径变化后预览元数据与资源路径不刷新

### 交付信息

- **状态**：修复完成／待验证
- **基于的提交**：`ebdf8f0ab8d1c8838552d18a18e0e29ae0e53c7f`
- **修复提交**：本记录所在提交；交回时使用 `git rev-parse HEAD` 核验
- **前置单据及对应提交**：BUG-002，`e71f14f4c067b271711c128259cf4f6e164025bb`；BUG-004，`9121b4b85b97b621b503ae1ba171d486eba051d0`
- **修改文件**：
  - `docs/architecture.md`
  - `docs/bug-backlog-2026-09-07.md`
  - `docs/bug-fix-handoffs-2026-09-07.md`
  - `src/preview_controller.cpp`
  - `src/preview_controller.h`
  - `tests/plugin_multi_window_test.cpp`
  - `tests/preview_controller_document_identity_test.cpp`

### 问题核实与根因

静态核实 notepad-- v3.8.3 在保存、另存为和重命名链路通过编辑器的 `filePath` 动态属性
更新路径。原控制器只在标签切换、渲染或导出时临时读取该属性；同一编辑器仅改变路径且没有
文本变化时，不会推进内容版本或安排刷新，因此旧预览继续保留旧文件类型、标题、
`QTextDocument::baseUrl` 和导出源路径。

### 实际修改方案

复用控制器已有的应用事件过滤器，只处理 `watched == m_editor` 且属性名为 `filePath` 的
`DynamicPropertyChange`。控制器缓存已处理路径用于去重；路径改变时推进 BUG-002 的内容版本、
失效旧预览身份、立即更新文档标签和导出动作。Dock 可见时沿用现有防抖刷新，隐藏时在下次
显示或 BUG-003 导出快照前同步刷新。原生预览重新接入时，`adoptNativePreview()` 会在调用宿主
更新内容前把新目录写入 `QTextDocument::baseUrl`。

切换编辑器时路径缓存随活动编辑器一起替换，编辑器销毁时清空。事件处理严格核对当前编辑器
身份，因此已解绑编辑器和其他宿主窗口的属性变化不会影响本窗口状态。扩展现有两组 Qt Test，
覆盖中文／空格目录、同编辑器 md→txt、未命名→md、导出源路径和多窗口隔离。

### 相较计划的偏差

没有再为每个编辑器安装对象级事件过滤器，而是复用 BUG-004 已存在的应用级过滤器并用编辑器
身份严格限定，以免同一事件被重复分发。保留 120 ms 轮询用于标签切换兼容兜底，但本次路径
更新不依赖轮询。

### 验收标准逐项结果

1. **无需额外键入或切换即可更新**：动态属性事件同步失效旧状态并安排刷新；测试运行 `blocked`。
2. **扩展名变化及时改变预览状态**：md→txt 立即禁用导出并使旧预览失效；测试运行 `blocked`。
3. **新目录图片正确**：刷新前更新文档 `baseUrl`，中文／空格新目录断言已加入；测试运行 `blocked`。
4. **中文／空格路径正常**：临时目录下中文及空格路径测试源码已覆盖；测试运行 `blocked`。
5. **多窗口只更新自己的状态**：首窗路径变化不影响第二窗导出状态、快照和渲染计数；测试运行 `blocked`。

### 验证证据

| 验证层级 | 结果 | 证据或原因 |
| --- | --- | --- |
| 静态检查 | `passed` | `git diff --check` 通过；复核宿主 v3.8.3 固定提交中的 `filePath` 属性设置；ABI 与 `notepad--/` 未修改 |
| 自动化测试 | `blocked` | 扩展两组 Qt Test；CMake 在 `find_package(Qt5 5.15)` 因缺少 `Qt5Config.cmake` 配置失败 |
| Windows Release 编译 | `blocked` | 当前为 Linux 环境，没有 Qt 5.15.2 和 MSVC v142 |
| Artifact 校验 | `not run` | 未生成 DLL 或打包 Artifact |
| 真实宿主测试 | `not verified` | 未在 notepad-- x64 中执行重命名、另存目录、未命名保存、中文／空格路径和双窗口测试 |

### 实际环境

- **Qt**：Qt 5.15.2 开发包不可用
- **MSVC**：不可用
- **notepad--**：`v3.8.3`，提交 `91105f68b74382128f3313ac5af8accdc77de918`
- **操作系统**：Linux x86_64
- **CMake 配置目录**：`/tmp/markdownview-bug005-build`
- **测试源码**：`tests/preview_controller_document_identity_test.cpp`、`tests/plugin_multi_window_test.cpp`
- **截图或二进制报告**：未生成

### 未验证项与已知限制

Qt Test 尚未实际编译运行。Windows Release DLL、Artifact，以及真实宿主中的文件重命名／另存为、
同名相对图片目录切换、中文／空格路径和多窗口隔离均待复核。跨目录 HTML 导出资源打包仍按
BUG-008 处理；后续复核不能仅凭静态检查关闭 BUG-005。

## BUG-006：关闭同步滚动后刷新丢失阅读位置

### 交付信息

- **状态**：修复完成／待验证
- **基于的提交**：`b1fd83a22e0f3c066c7f0f4f2206ea576cd3b1b3`
- **修复提交**：本记录所在提交；交回时使用 `git rev-parse HEAD` 核验
- **前置单据及对应提交**：BUG-002，`e71f14f4c067b271711c128259cf4f6e164025bb`
- **修改文件**：
  - `docs/architecture.md`
  - `docs/bug-backlog-2026-09-07.md`
  - `docs/bug-fix-handoffs-2026-09-07.md`
  - `src/markdown_preview_dock.cpp`
  - `src/markdown_preview_dock.h`
  - `src/preview_controller.cpp`
  - `tests/preview_controller_document_identity_test.cpp`

### 问题核实与根因

静态确认正常预览由宿主 `on_updataMarkdown` 调用 `QTextEdit::setMarkdown()`，重建文档时会
重置预览滚动位置。原有 `previousRatio` 恢复只存在于未被正常原生渲染链路调用的备用
`renderMarkdown()`，所以关闭同步滚动后，自动刷新和手工刷新都可能跳回顶部；图片或长富文本
触发的延迟布局还会在首次恢复之后继续改变滚动范围。

### 实际修改方案

控制器在调用真实原生渲染前，按当前编辑器捕获原生预览滚动比例。渲染成功且编辑器身份与
内容版本仍匹配时，Dock 保存恢复目标，通过现有短计时器在当前布局完成后恢复，并在后续
`rangeChanged` 到来时再次按新范围应用相同比例。比例统一限制在 0～1，内容缩短时不会越界。

Dock 单独记录当前原生预览所属编辑器，避免 BUG-002 的内容身份失效后无法在渲染前取到旧位置。
切换到其他编辑器、预览销毁或重新开启同步滚动时会取消旧恢复目标。程序恢复滚动条时使用
`QSignalBlocker`，而预览到编辑器方向仍只响应用户滚动动作，因此不会把恢复写入反向反馈给编辑器。

扩展现有文档身份 Qt Test，模拟宿主每次渲染先归零，并在 10 ms、45 ms 两次扩大滚动范围；
覆盖自动刷新、手工刷新、跨文档隔离及重新开启同步后的编辑器主导行为。

### 相较计划的偏差

保留备用 `renderMarkdown()` 的现有恢复逻辑，因为该函数仍是独立备用渲染能力；本单把等价且
带身份／版本门控的恢复明确接入真实原生路径，没有引入精确源代码行到 Markdown 节点映射。

### 验收标准逐项结果

1. **同步关闭时刷新不无故跳顶**：自动刷新和手工刷新测试源码均覆盖；运行 `blocked`。
2. **同步开启时保持原有功能**：重新开启同步后由编辑器 80% 位置驱动预览；运行 `blocked`。
3. **切换文档不继承其他文档位置**：A 的恢复目标在采用 B 时取消；运行 `blocked`。
4. **长文档布局后位置稳定**：模拟两次延迟范围变化后仍恢复 75%；运行 `blocked`。
5. **恢复不造成反向滚动反馈**：恢复写入由 `QSignalBlocker` 屏蔽，静态检查 `passed`；真实交互 `not verified`。

### 验证证据

| 验证层级 | 结果 | 证据或原因 |
| --- | --- | --- |
| 静态检查 | `passed` | `git diff --check` 通过；原生 `on_updataMarkdown` 路径已接入；身份／版本门控、范围限制及旧目标取消已复核；ABI 与 `notepad--/` 未修改 |
| 自动化测试 | `blocked` | 扩展 `markdownview_document_identity_tests`；CMake 在 `find_package(Qt5 5.15)` 因缺少 `Qt5Config.cmake` 配置失败 |
| Windows Release 编译 | `blocked` | 当前 Linux 环境没有 Qt 5.15.2 与 MSVC v142 |
| Artifact 校验 | `not run` | 未生成 DLL 或打包 Artifact |
| 真实宿主测试 | `not verified` | 未在 notepad-- x64 中执行同步关闭后的自动／手工刷新、图片延迟布局、内容缩短、标签切换和重新开启同步测试 |

### 实际环境

- **Qt**：Qt 5.15.2 开发包不可用
- **MSVC**：不可用
- **notepad--**：`v3.8.3`，提交 `91105f68b74382128f3313ac5af8accdc77de918`
- **操作系统**：Linux x86_64
- **CMake 配置目录**：`/tmp/markdownview-bug006-build`
- **测试源码**：`tests/preview_controller_document_identity_test.cpp`
- **截图或二进制报告**：未生成

### 未验证项与已知限制

Qt Test 尚未实际编译运行。Windows Release DLL、Artifact，以及真实宿主中的阅读位置保持、
内容缩短和图片延迟布局均待复核。阅读位置按滚动比例保持，不提供精确源行或 Markdown 节点映射；
后续 BUG-015 仍需处理更广泛的滚动缓存与范围变化架构问题，不能仅凭静态检查关闭 BUG-006。
