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

## BUG-007：原生预览未接入链接与锚点处理

### 交付信息

- **状态**：修复完成／待验证
- **基于的提交**：`1868f3e6a117a794297cd538215df7d8b0dda635`
- **修复提交**：本记录所在提交；交回时使用 `git rev-parse HEAD` 核验
- **前置单据及对应提交**：BUG-002，`e71f14f4c067b271711c128259cf4f6e164025bb`
- **修改文件**：
  - `docs/architecture.md`
  - `docs/bug-backlog-2026-09-07.md`
  - `docs/bug-fix-handoffs-2026-09-07.md`
  - `src/markdown_preview_dock.cpp`
  - `src/markdown_preview_dock.h`
  - `tests/markdown_preview_dock_lifecycle_test.cpp`

### 问题核实与根因

静态确认备用 `QTextBrowser` 的 `anchorClicked` 已连接 `openLink()`，但正常预览实际显示宿主
`MarkdownView` 内的只读 `QTextEdit`。`adoptNativePreview()` 原先只为该控件设置 `baseUrl`、
样式并接入滚动条，没有链接激活入口，因此外部链接、相对链接和页内锚点不会进入插件逻辑。

### 实际修改方案

Dock 在原生 `QTextEdit` viewport 上安装局部事件过滤器。只有鼠标左键在同一链接上按下和释放、
移动距离小于系统拖动阈值、没有修饰键且没有形成文本选区时才调用链接处理；其他鼠标事件、
复制快捷键和滚轮事件仍由原生控件处理。

纯片段链接扫描当前 `QTextDocument` 的命名锚点并调整原生滚动条；相对链接继续使用当前 Markdown
文件目录生成的 `baseUrl` 解析。外部打开协议明确限制为 `http`、`https`、`mailto` 和 `file`；
不支持协议或系统打开失败时写入诊断日志，并在 Dock 文档栏显示非阻塞反馈。
`QDesktopServices::openUrl()` 封装为可注入的 `UrlOpener`，Qt Test 只记录请求 URL，不启动外部程序。

扩展 `markdownview_lifecycle_tests`，覆盖原生预览点击 HTTPS 链接、相对路径解析、页内锚点、
不支持协议拒绝以及拖选链接文字不误打开。

### 相较计划的偏差

未替换宿主原生 `QTextEdit`，也未引入浏览器级展示控件；采用 viewport 事件过滤器作为最小局部
适配。页内锚点使用 `QTextDocument` 已生成的命名锚点，不增加源代码行到 Markdown 节点映射。

### 验收标准逐项结果

1. **支持的外部链接能打开**：注入打开器记录 HTTPS URL 的测试源码已覆盖；运行 `blocked`。
2. **有效锚点跳转正确**：原生文档命名锚点定位并产生向下滚动的测试源码已覆盖；运行 `blocked`。
3. **相对路径与当前文档对应**：`/tmp/docs/current.md` 下 `guide/next.md` 的本地 URL 断言已覆盖；运行 `blocked`。
4. **不支持的协议不被打开**：`javascript:` 不进入打开器并显示失败反馈；测试运行 `blocked`。
5. **文字选择、复制、滚动保持正常**：拖选形成选区且打开次数为零的测试源码已覆盖；复制与滚轮因过滤器不处理对应事件而静态 `passed`，真实交互 `not verified`。

### 验证证据

| 验证层级 | 结果 | 证据或原因 |
| --- | --- | --- |
| 静态检查 | `passed` | `git diff --check` 通过；核对原生 `QTextEdit`、事件范围、协议白名单、相对 `baseUrl`；ABI 与 `notepad--/` 未修改 |
| 自动化测试 | `blocked` | 扩展 `markdownview_lifecycle_tests`；CMake 在 `find_package(Qt5 5.15)` 因缺少 `Qt5Config.cmake` 配置失败 |
| Windows Release 编译 | `blocked` | 当前为 Linux 环境，没有 Qt 5.15.2 和 MSVC v142 |
| Artifact 校验 | `not run` | 未生成 DLL 或打包 Artifact |
| 真实宿主测试 | `not verified` | 未在 notepad-- x64 点击 HTTPS、mailto、file、相对文档、页内锚点并执行拖选／复制／滚轮测试 |

### 实际环境

- **Qt**：Qt 5.15.2 开发包不可用
- **MSVC**：不可用
- **notepad--**：`v3.8.3`，提交 `91105f68b74382128f3313ac5af8accdc77de918`
- **操作系统**：Linux x86_64
- **CMake 配置目录**：`/tmp/markdownview-bug007-build`
- **测试源码**：`tests/markdown_preview_dock_lifecycle_test.cpp`
- **截图或二进制报告**：未生成

### 未验证项与已知限制

Qt Test 尚未实际编译运行。Windows Release DLL、Artifact 和真实宿主链接交互均待复核。当前只处理
`QTextDocument` 可识别的命名锚点和明确允许协议，不支持脚本执行、浏览器导航历史或源行级定位；
不能仅凭静态检查关闭 BUG-007。

## BUG-008：HTML 跨目录导出未处理相对资源

### 交付信息

- **状态**：修复完成／待验证
- **基于的提交**：`3e2b00848101fb2f9074761e446962f593897efd`
- **修复提交**：本记录所在提交；交回时使用 `git rev-parse HEAD` 核验
- **前置单据及对应提交**：BUG-003，`5c54499e5204376ac629ad6012f9564d888708ff`；BUG-005，`b1fd83a22e0f3c066c7f0f4f2206ea576cd3b1b3`
- **修改文件**：
  - `README.md`
  - `docs/architecture.md`
  - `docs/bug-backlog-2026-09-07.md`
  - `docs/bug-fix-handoffs-2026-09-07.md`
  - `src/markdown_preview_dock.cpp`
  - `tests/markdown_preview_dock_lifecycle_test.cpp`

### 问题核实与根因

静态确认 BUG-003 固定的 `QTextDocument::toHtml()` 快照仍保留图片原始相对 `src`。BUG-005
更新 `baseUrl` 后，实时预览可以从 Markdown 所在目录加载图片，但这个基准地址不会自动变成
可迁移 HTML 内容。导出到其他目录或随后移动 HTML 时，浏览器会改从 HTML 所在目录解析
`images/a.png`，因此图片丢失。

### 实际修改方案

采用单文件导出策略。Dock 从身份和版本匹配的实时文档取得固定 HTML 字符串后，只扫描
`<img src>` 属性：相对路径和 `file:` 本地图片按文档 `baseUrl` 解析，文件可读且 MIME 类型为
`image/*` 时转换为 Base64 data URL。替换发生在字符串副本中，不修改实时 `QTextDocument`。

已有 data URL、HTTP/HTTPS 图片、其他协议和普通 `<a href>` 均保持原样；实现不会下载远程
资源，也不会因为一个本地链接恰好可读就把任意附件打包。缺失、不可读或无法识别为图片的
本地资源保留原 `src`，同时在导出 HTML 注入 `markdownview-export` 诊断注释并写入日志。
实际落盘继续使用 BUG-003 的 `QSaveFile` 原子提交，不创建伴随资源目录。

扩展 `markdownview_lifecycle_tests`，构造中文及空格目录下的 PNG、缺失图片、远程图片和普通
本地附件链接，验证本地图片内嵌、跨目录写入、未支持资源的明确记录，以及导出前后实时文档
HTML 完全不变。

### 相较计划的偏差

采用 backlog 优先建议的内嵌策略，没有实现伴随资源目录，因此不需要定义目录命名、同名文件
覆盖、部分复制失败清理或覆盖已有无关资源的行为。未设置图片体积上限，以保持“单个 HTML
可以迁移”的产品语义；文档明确说明 Base64 编码会增大文件体积。

### 验收标准逐项结果

1. **跨目录导出正确显示本地图片**：中文及空格路径 PNG 转为 `data:image/png;base64`，并写入另一个目录的测试源码已覆盖；运行 `blocked`。
2. **可迁移策略符合文档描述**：README 与架构文档已说明单文件内嵌、远程资源、失败语义和体积影响；静态 `passed`。
3. **原预览不变**：测试在生成快照前后比较实时 `document()->toHtml()`；运行 `blocked`。
4. **资源缺失有明确结果**：缺失资源保留原 `src`，HTML 注释与诊断日志列明；测试源码已覆盖，运行 `blocked`。
5. **保存失败不破坏已有文件和无关资源**：继续使用 `QSaveFile`，BUG-003 的原子写入测试仍保留；运行 `blocked`。本单不创建资源目录，因此不会覆盖或清理无关资源。

### 验证证据

| 验证层级 | 结果 | 证据或原因 |
| --- | --- | --- |
| 静态检查 | `passed` | `git diff --check` 通过；只重写固定快照中的 `<img src>`；远程图片和普通链接不打包；ABI 与 `notepad--/` 未修改；宿主参考基线为 `v3.8.3` / `91105f68` |
| 自动化测试 | `blocked` | 扩展 `markdownview_lifecycle_tests`；`cmake -S . -B /tmp/markdownview-bug008-build -DBUILD_TESTING=ON` 在 `find_package(Qt5 5.15)` 因缺少 `Qt5Config.cmake` 失败 |
| Windows Release 编译 | `blocked` | 当前 Linux 环境没有 Qt 5.15.2 与 MSVC v142 |
| Artifact 校验 | `not run` | 未生成 DLL 或打包 Artifact |
| 真实宿主测试 | `not verified` | 未在 notepad-- x64 执行跨目录导出、移动 HTML、中文／空格图片、缺失图片、远程图片及大图片测试 |

### 实际环境

- **Qt**：Qt 5.15.2 开发包不可用
- **MSVC**：不可用
- **notepad--**：`v3.8.3`，提交 `91105f68b74382128f3313ac5af8accdc77de918`
- **操作系统**：Linux x86_64
- **CMake 配置目录**：`/tmp/markdownview-bug008-build`
- **测试源码**：`tests/markdown_preview_dock_lifecycle_test.cpp`
- **截图或二进制报告**：未生成

### 未验证项与已知限制

Qt Test 尚未实际编译运行。Windows Release DLL、Artifact 和真实宿主跨目录导出均待复核。
远程图片仍依赖打开 HTML 时的网络可达性；缺失本地图片不会使整个导出失败，而是保留引用并
明确记录；大型图片会按 Base64 编码增大 HTML 文件。不能仅凭静态检查关闭 BUG-008。

## BUG-009：文档样式应用与主题刷新不完整

### 交付信息

- **状态**：修复完成／待验证
- **基于的提交**：`6d47180c3a10fae8ae18720d63e4af53e8851625`
- **修复提交**：本记录所在提交；交回时使用 `git rev-parse HEAD` 核验
- **前置单据及对应提交**：BUG-005，`b1fd83a22e0f3c066c7f0f4f2206ea576cd3b1b3`；BUG-006，`1868f3e6a117a794297cd538215df7d8b0dda635`
- **修改文件**：
  - `docs/architecture.md`
  - `docs/bug-backlog-2026-09-07.md`
  - `docs/bug-fix-handoffs-2026-09-07.md`
  - `src/markdown_preview_dock.cpp`
  - `src/markdown_preview_dock.h`
  - `src/preview_controller.cpp`
  - `tests/markdown_preview_dock_lifecycle_test.cpp`

### 问题核实与根因

静态确认主路径由宿主 `QTextEdit::setMarkdown()` 先生成文档，首次 `adoptNativePreview()`
随后才设置默认样式表；`setDefaultStyleSheet()` 不能追溯性地把浏览器 CSS 全面应用到已经生成的
Markdown 字符与块格式。复用原生预览时，原实现又在 `on_updataMarkdown()` 之前设置样式，随后
文档重建会覆盖格式。代码没有处理 palette 或 style 变化，因此主题切换只可能改变外层控件，
正文、链接、代码、引用和表格没有一致的刷新保证。

### 实际修改方案

保留宿主 Qt 5.15.2 Markdown 解析器，在其生成的 `QTextDocument` 上增加不重新解析源文本的格式
后处理。实现按 Qt Markdown 写入的标题级别、引用级别、代码围栏／语言属性、等宽字符片段和
锚点设置标题、引用、围栏代码、行内代码及链接格式；递归处理 `QTextTable` 的边框、单元格留白
和首行背景；控件 palette 统一正文、背景、替代背景、边框和链接颜色。

首次创建原生预览时在宿主初次渲染之后处理；复用预览并调用 `on_updataMarkdown()` 时在宿主更新
返回后调用相同入口。Dock 合并 `PaletteChange`、`ApplicationPaletteChange` 和 `StyleChange`，
零延迟计时器只刷新当前编辑器身份和内容版本仍匹配的文档，不调用宿主 Markdown 解析。刷新前
记录当前滚动比例，完成后通过身份／版本门控恢复，避免主题切换串文档或跳动。

扩展 `markdownview_lifecycle_tests`，输入标题、引用、行内代码、围栏代码、链接和表格，验证首次
后处理存在、暗色 palette 能更新原生预览和链接颜色，并验证主题刷新前后的阅读位置保持不变。

### 相较计划的偏差

没有替换宿主渲染器，也不承诺完整浏览器 CSS；支持范围明确限定为 Qt 富文本文档公开格式可表达
的控件 palette、标题、引用、代码、表格和链接。当前环境缺少 Qt 运行条件与 Windows 宿主，未能
生成验收要求中的前后截图，已保留为真实宿主待验证项。

### 验收标准逐项结果

1. **首次与刷新后样式一致**：首次 adopt 与后续 `on_updataMarkdown()` 返回后共用
   `applyDocumentStyle()`；静态 `passed`，自动化运行 `blocked`。
2. **明暗主题下文字、链接和代码可读**：使用 palette 的 `Base`、`Text`、`AlternateBase`、
   `Midlight` 和 `Link`，暗色主题测试源码已覆盖；运行 `blocked`，真实视觉 `not verified`。
3. **相关块格式符合明确支持范围**：标题、引用、围栏／行内代码、表格和链接后处理已实现，
   代表性 Markdown 测试源码已添加；运行 `blocked`。
4. **提供前后截图**：`not verified`；当前 Linux 环境缺少 Qt 5.15.2，且未运行真实宿主。
5. **主题切换不串文档、不改变阅读位置**：样式刷新由编辑器身份和内容版本门控，并按滚动比例
   恢复；位置保持测试源码已覆盖，运行 `blocked`；真实多标签切换 `not verified`。

### 验证证据

| 验证层级 | 结果 | 证据或原因 |
| --- | --- | --- |
| 静态检查 | `passed` | `git diff --check`；首次／后续渲染时序、主题事件合并、身份／版本门控和不调用宿主解析路径已复核；ABI 与 `notepad--/` 未修改 |
| 自动化测试 | `blocked` | 扩展 `markdownview_lifecycle_tests`；CMake 在 `find_package(Qt5 5.15)` 因缺少 `Qt5Config.cmake` 配置失败 |
| Windows Release 编译 | `blocked` | 当前 Linux 环境没有 Qt 5.15.2 与 MSVC v142 |
| Artifact 校验 | `not run` | 未生成 DLL 或打包 Artifact |
| 真实宿主测试 | `not verified` | 未在 notepad-- x64 检查首次打开、手工刷新、明暗主题、多标签和阅读位置，也未生成前后截图 |

### 实际环境

- **Qt**：Qt 5.15.2 开发包不可用
- **MSVC**：不可用
- **notepad--**：`v3.8.3`，提交 `91105f68b74382128f3313ac5af8accdc77de918`
- **操作系统**：Linux x86_64
- **CMake 配置目录**：`/tmp/markdownview-bug009-build`
- **测试源码**：`tests/markdown_preview_dock_lifecycle_test.cpp`
- **截图或二进制报告**：未生成

### 未验证项与已知限制

Qt Test 尚未实际编译运行。Windows Release DLL、Artifact、真实宿主明暗主题视觉、嵌入 HTML
细节与前后截图均待复核。支持范围不等价于完整浏览器 CSS；后续 BUG-011 仍需单独测量大文档
格式遍历和总体渲染耗时，不能仅凭静态检查关闭 BUG-009。

## BUG-010：发布重跑覆盖已公开 Release 附件

### 交付信息

- **状态**：修复完成／待验证
- **基于的提交**：`559d64b4b28efe227af5241ae44935ff80ec4d09`
- **修复提交**：本记录所在提交；交回时使用 `git rev-parse HEAD` 核验
- **前置单据及对应提交**：无；本单为独立发布流程修复
- **修改文件**：
  - `.github/workflows/windows-release.yml`
  - `docs/releasing.md`
  - `docs/bug-backlog-2026-09-07.md`
  - `docs/bug-fix-handoffs-2026-09-07.md`
  - `scripts/publish-release-assets.sh`
  - `tests/release_publish_test.sh`

### 问题核实与根因

原发布步骤以 `gh release view` 的布尔结果决定上传或创建。查询成功时直接执行
`gh release upload --clobber`，所以同一公开标签的重跑可用重新构建的 ZIP 和 SHA256 覆盖
已公开附件；查询失败时不区分 HTTP 404、权限、网络或限流，任何错误都进入创建分支。

### 实际修改方案

新增独立发布脚本，通过 GitHub API 查询标签 Release。只有明确 HTTP 404 才调用
`gh release create`，其他查询错误原样报告并失败。Release 已存在时，脚本按准确文件名定位
附件，通过资产 API 下载已有内容并使用 `cmp` 逐字节比较：内容一致则跳过，缺失附件在所有
已有同名附件验证一致后仅补传缺失文件，内容不同或同名附件结果不唯一则拒绝修改。所有上传
都不使用 `--clobber`，并保留竞态发生时由 GitHub 拒绝同名上传的安全失败语义。

release job 增加固定 SHA 的 `actions/checkout`，用于取得仓库脚本；原版本一致性校验、
`contents: write` 最小发布权限、ZIP/SHA256 生成和 Artifact 传递均保留。发布手册新增公开附件
不可变规则，以及完整相同、部分缺失、内容冲突和查询错误的处理说明。

### 相较计划的偏差

没有在真实或隔离 GitHub 仓库改动 Release，而是用 mock `gh` 覆盖五类行为，避免以生产附件
验证破坏性场景。内容判定使用逐字节比较，不仅校验 ZIP，也校验 SHA256 文件本身；这比只比较
文件名或相信校验文件更严格。

### 验收标准逐项结果

1. **不同附件不能覆盖同一公开版本**：冲突测试确认在任何发布写调用前失败；`passed`。
2. **相同结果可幂等执行**：完整相同场景逐一跳过附件且不调用 upload/create；`passed`。
3. **查询错误不进入创建分支**：HTTP 403 模拟直接失败，HTTP 404 才创建；`passed`。
4. **附件缺失有明确恢复策略**：已有 ZIP 一致后只上传缺失 SHA256，且无 `--clobber`；
   自动化 `passed`，真实 GitHub `not verified`。
5. **保留既有发布约束**：版本校验、`contents: write`、ZIP 与 SHA256 生成逻辑未移除；
   静态 `passed`。

### 验证证据

| 验证层级 | 结果 | 证据或原因 |
| --- | --- | --- |
| 静态检查 | `passed` | `git diff --check`；两个 shell 文件 `bash -n`；复核版本校验、权限、打包、SHA256 与 Artifact 路径均保留 |
| 自动化测试 | `passed` | `./tests/release_publish_test.sh`；覆盖不存在、完整相同、缺失、冲突、HTTP 403 查询错误 |
| Windows Release 编译 | `not run` | 本单不修改 DLL 源码，且未触发 GitHub Actions |
| Artifact 校验 | `not run` | 未生成或下载真实 Artifact；测试使用临时 mock 资产 |
| 真实宿主测试 | `not verified` | 发布脚本修改与 notepad-- 预览行为无关 |

### 实际环境与未验证项

验证环境为 Linux x86_64、GNU bash、`jq`、`cmp` 和 mock `gh`；Qt、MSVC、notepad-- 均不适用。
测试终端结果为 `release publishing tests: passed`，未生成持久日志或截图。尚未在隔离 GitHub
仓库或真实标签 workflow rerun 中验证在线 API 权限、附件二进制下载响应及并发竞态，也未修改
任何现有生产 Release。后续复核不能只凭本地 mock 测试关闭 BUG-010。

## BUG-011：全文同步渲染缺少大文档性能治理

### 交付信息

- **状态**：修复完成／待验证
- **基于的提交**：`3e4b16f2fcf40f1ec2700b0a6e40740590ba06c5`
- **修复提交**：本记录所在提交；交回时使用 `git rev-parse HEAD` 核验
- **前置单据及对应提交**：BUG-002 `e71f14f4c067b271711c128259cf4f6e164025bb`；
  BUG-006 `1868f3e6a117a794297cd538215df7d8b0dda635`；
  BUG-009 `559d64b4b28efe227af5241ae44935ff80ec4d09`
- **修改文件**：`docs/architecture.md`、`docs/bug-backlog-2026-09-07.md`、
  `docs/bug-fix-handoffs-2026-09-07.md`、`src/markdown_preview_dock.cpp`、
  `src/markdown_preview_dock.h`、`src/preview_controller.cpp`、
  `src/preview_controller.h`、`tests/preview_controller_document_identity_test.cpp`

### 问题核实与根因

静态确认文本变化已有 trailing-edge 防抖，但计时器到期后仍在 GUI 线程同步调用宿主全文
渲染；最近耗时只用于把延迟扩大到最多 2 秒，不能阻止较大或已证明较慢的文档在停顿后再次
阻塞输入。显示 Dock、轮询和重复调度也没有在自动计时器入口跳过已经是当前内容版本的预览。
原界面没有独立的“内容已变化但自动渲染暂停”反馈。

当前 Linux 环境缺少 Qt 5.15.2 开发包和 Windows 宿主，无法取得真实设备上普通、大型以及
表格／图片密集文档的前后输入延迟与完整布局数据。因此同步全文路径成立，但实际卡顿阈值仍
按“影响待测量”处理，不能描述为已证明消除所有大文档卡顿。

### 实际修改方案

保留宿主原生 `QTextEdit`、`on_viewMarkdown`／`on_updataMarkdown` 和 GUI 线程调用边界。自动
计时器触发时先同步活动编辑器，并核对编辑器身份、内容版本和 Dock 预览版本；未变化直接
跳过，不再次解析。

采用两类有界阈值：当前文件达到 1 MiB，或最近一次从控制器进入渲染到宿主调用及同步样式
处理返回达到 750 ms。首次打开仍允许渲染；满足任一阈值后，后续文本变化只推进内容版本、
停止自动计时器，并在文档栏显示“预览已暂停自动刷新，待手工刷新”。菜单和工具栏手工刷新、
以及导出最新快照等明确操作仍能同步生成最终版本。每次显式渲染完成后重新评估文件大小与
耗时，小文件且耗时恢复到阈值以下时继续使用原 350 ms 至 2 秒自适应防抖。

诊断日志新增宿主 `on_viewMarkdown`／`on_updataMarkdown` 调用耗时、文档样式刷新耗时和控制器
总耗时。Qt 富文本返回后的异步布局仍通过已有滚动范围变化观察，不把同步返回耗时冒充完整
排版时间。

扩展 `markdownview_document_identity_tests`，增加以下可重复场景：

- 已是当前内容版本的自动调度不会重复渲染；
- 1 MiB 文件编辑后超过普通防抖窗口仍不自动渲染，并显示待刷新状态；
- 800 ms 模拟慢渲染后等待超过原最大 2 秒自动窗口仍不再次渲染；
- 手工刷新后预览包含最新内容。

### 相较计划的偏差

没有引入后台解析或更换渲染器。宿主 QWidget、QTextEdit 和原生槽不能安全移到工作线程，若
更换解析边界或渲染器会扩大兼容性范围。1 MiB 和 750 ms 是本次防御性治理阈值，并非真实宿主
性能结论；真实设备前后性能数据保留为关闭前必需复核项。

### 验收标准逐项结果

1. **提交同条件前后性能数据及采用阈值**：1 MiB／750 ms 阈值及 800 ms 模拟慢渲染场景已
   编码；静态 `passed`，Qt 测试运行 `blocked`，真实宿主前后数据 `not verified`。
2. **普通文档没有明显退化**：保留原 350 ms trailing-edge 防抖，既有连续输入测试与新增
   未变化调度跳过测试源码覆盖；运行 `blocked`。
3. **大文档策略可预测且可手工刷新**：任一阈值满足即显示明确状态并停止自动计时器；手工
   刷新测试源码覆盖最终内容；运行 `blocked`。
4. **连续输入不恢复逐键全文解析**：宿主即时连接仍会断开；阈值内继续合并，阈值外暂停自动
   全文渲染；静态 `passed`。
5. **最终预览版本正确**：手工刷新和导出继续经过编辑器身份／内容版本门控，测试源码断言
   最新文本；运行 `blocked`，真实快速输入 `not verified`。

### 验证证据

| 验证层级 | 结果 | 证据或原因 |
| --- | --- | --- |
| 静态检查 | `passed` | `git diff --check`；前置提交均为 HEAD 祖先；阈值、状态反馈、版本跳过、手工刷新、诊断计时和范围边界复核；ABI、导出入口与 `notepad--/` 未修改 |
| 自动化测试 | `blocked` | 扩展 `markdownview_document_identity_tests`；CMake 在 `find_package(Qt5 5.15)` 因缺少 `Qt5Config.cmake` 配置失败 |
| Windows Release 编译 | `blocked` | 当前 Linux 环境没有 Qt 5.15.2 与 MSVC v142 |
| Artifact 校验 | `not run` | 未生成 DLL 或 Artifact |
| 真实宿主测试 | `not verified` | 未在 notepad-- x64 测量普通、较大、表格／图片密集文档的首次打开、连续输入、停顿刷新和手工刷新 |

### 实际环境与未验证项

- **Qt**：Qt 5.15.2 开发包不可用
- **MSVC**：不可用
- **notepad--**：`v3.8.3`，提交 `91105f68b74382128f3313ac5af8accdc77de918`
- **操作系统**：Linux x86_64
- **CMake 配置目录**：`/tmp/markdownview-bug011-build`
- **测试源码**：`tests/preview_controller_document_identity_test.cpp`
- **日志、截图或性能报告**：未生成

Qt Test 尚未实际编译运行，Windows Release DLL 与 Artifact 未生成。真实宿主输入响应、宿主
同步调用、Qt 异步布局和不同设备上的同条件前后性能数据均待复核。未保存且首次渲染尚未暴露
慢耗时的超大内存文档，只能在首次渲染之后按耗时进入手工策略；本单不能据此直接标记关闭。

## BUG-012：缺少统一的关键行为回归测试入口

### 交付信息

- **状态**：修复完成／待验证
- **基于的提交**：`a968289319f30158ddc2dcddbfd0ea6e7337e583`
- **修复提交**：本记录所在提交；交回时使用 `git rev-parse HEAD` 核验
- **前置单据及对应提交**：BUG-001 `ba87695e48576e0711dadbcc2a97832b22efc5e4`；
  BUG-002 `e71f14f4c067b271711c128259cf4f6e164025bb`；
  BUG-003 `5c54499e5204376ac629ad6012f9564d888708ff`；
  BUG-004 `9121b4b85b97b621b503ae1ba171d486eba051d0`；
  BUG-005 `b1fd83a22e0f3c066c7f0f4f2206ea576cd3b1b3`；
  BUG-006 `1868f3e6a117a794297cd538215df7d8b0dda635`；
  BUG-007 `3e2b00848101fb2f9074761e446962f593897efd`；
  BUG-008 `6d47180c3a10fae8ae18720d63e4af53e8851625`；
  BUG-009 `559d64b4b28efe227af5241ae44935ff80ec4d09`；
  BUG-010 `3e4b16f2fcf40f1ec2700b0a6e40740590ba06c5`；
  BUG-011 `a968289319f30158ddc2dcddbfd0ea6e7337e583`
- **修改文件**：`.github/workflows/windows-release.yml`、`CMakeLists.txt`、`README.md`、
  `docs/bug-backlog-2026-09-07.md`、`docs/bug-fix-handoffs-2026-09-07.md`、
  `docs/releasing.md`、`docs/testing.md`、`examples/assets/相对 图片.xpm`、
  `examples/preview-demo.md`、`scripts/build-windows.ps1`、
  `scripts/run-regression-tests.ps1`

### 问题核实与根因

BUG-001～BUG-011 已经累积三套 Qt Test 可执行目标和一套隔离的 Release 发布脚本测试，但
执行入口分散。CMake 目标没有分类标签和超时；Windows 构建脚本没有显式固定
`BUILD_TESTING=ON`；GitHub Actions 只构建、打包和发布，不执行 CTest；发布幂等测试也没有
并入统一命令。因此当前云端构建成功不能说明本轮关键行为回归已运行。

原 `examples/preview-demo.md` 只提供基础 Markdown、表格和外部链接，没有实际相对图片文件，
也没有把页内锚点、主题、长文档滚动、销毁顺序、多窗口、快速切换和导出边界整理为可重复的
真实宿主输入。

### 实际修改方案

新增 `scripts/run-regression-tests.ps1` 作为 Windows 统一入口。默认先调用现有 Release 构建
脚本，再使用 `ctest --no-tests=error --output-on-failure` 运行全部 Qt 行为测试，最后通过
Git for Windows 的 `bash.exe` 执行隔离发布测试。任一测试不存在、超时或失败都会让命令以
非零状态结束；`-SkipBuild` 可复用已经配置和构建的目录。

`scripts/build-windows.ps1` 显式传入 `-DBUILD_TESTING=ON`。三个 Qt 测试继续保持各自职责，
不复制测试源码，只增加 `behavior` 与专项 CTest 标签，并设置 60 秒或 120 秒超时。Windows
工作流在 `Build Release DLL` 与 `Package plugin` 之间增加独立的 `Run regression test suite`
步骤，所以编译和测试结果分别可见，测试失败会阻止 Artifact 和 Release 生成。

新增 `docs/testing.md`，记录统一命令、分组定位命令、覆盖矩阵、模拟宿主边界和真实宿主检查
清单。扩充 `examples/preview-demo.md` 并增加 `examples/assets/相对 图片.xpm`，集中提供含中文和
空格路径的本地图片、页内锚点、主题、滚动、长文本及 BUG-001～BUG-011 手工场景。

Qt 链接测试继续使用注入的 URL opener，不启动真实外部程序。Release 测试继续使用临时目录
和 mock `gh`，不访问 GitHub API，不创建或覆盖生产 Release，也不依赖个人绝对路径。

### 相较计划的偏差

没有把三套已有 Qt Test 合并成单一巨型可执行文件，也没有创建第四套重复行为测试；本单统一
的是执行入口、分类、超时、CI 门禁和验证说明。当前 Linux 环境没有 PowerShell、Qt 5.15.2
和 MSVC v142，无法实际执行 Windows 统一入口或编译 Qt Test，因此只运行了平台可用的隔离
发布测试，Windows CI 和真实宿主结果仍保留为待验证。

### 验收标准逐项结果

1. **提供可执行命令和测试结果**：`docs/testing.md` 提供统一及分组命令；发布幂等测试实际
   `passed`；Qt／Windows 统一入口 `blocked`。
2. **代表性回归测试能区分修复前后行为**：既有测试覆盖生命周期、多窗口、快速切换、路径
   变化、导出与资源、滚动恢复、防抖、链接、主题和大文档策略；覆盖矩阵静态 `passed`，实际
   Qt Test 运行 `blocked`。
3. **不打开真实链接、不覆盖生产 Release、不依赖个人路径**：URL opener 使用注入回调，发布
   测试使用 `mktemp` 与 mock `gh`，统一入口仅接受参数或标准 Qt 环境变量；静态 `passed`。
4. **Windows Release 构建与测试分别可见**：工作流保留构建步骤并增加独立测试步骤；YAML
   解析 `passed`，实际 GitHub Actions `not run`。
5. **真实宿主未运行时明确标注**：`docs/testing.md` 和本记录均标记 `not verified`。

### 验证证据

| 验证层级 | 结果 | 证据或原因 |
| --- | --- | --- |
| 静态检查 | `passed` | `git diff --check`；GitHub Actions YAML 解析；CTest 目标、标签、超时、`--no-tests=error`、`BUILD_TESTING=ON`、CI 步骤顺序、mock `gh` 和 URL opener 边界复核；Markdown 相对链接目标存在；BUG-001～BUG-011 提交均为 HEAD 祖先；ABI、导出入口和 `notepad--/` 未修改 |
| 自动化测试 | `partial` | `./tests/release_publish_test.sh` passed；Qt Test 因缺少 `Qt5Config.cmake` blocked；PowerShell 统一入口因环境无 PowerShell not run |
| Windows Release 编译 | `blocked` | 当前 Linux 环境没有 Qt 5.15.2、MSVC v142 和 Windows runner；未触发 GitHub Actions |
| Artifact 校验 | `not run` | 未生成或下载 DLL、ZIP、SHA256 或 GitHub Artifact |
| 真实宿主测试 | `not verified` | 未在 notepad-- v3.8.3 x64 执行新增手工回归清单 |

### 实际环境与未验证项

- **Qt**：Qt 5.15.2 开发包不可用
- **MSVC**：不可用
- **notepad--**：`v3.8.3`，提交 `91105f68b74382128f3313ac5af8accdc77de918`
- **操作系统**：Linux x86_64
- **其他工具**：GNU bash、CMake 3.28.3；无 PowerShell
- **CMake 配置目录**：`/tmp/markdownview-bug012-build`
- **测试输出**：`release publishing tests: passed`
- **统一命令与覆盖矩阵**：`docs/testing.md`
- **手工输入**：`examples/preview-demo.md`、`examples/assets/相对 图片.xpm`

`scripts/run-regression-tests.ps1` 尚未在 Windows PowerShell 中实际执行；三个 Qt Test 尚未
编译运行；GitHub Actions 新增测试步骤、Windows Release DLL、Artifact 内容和 SHA256 均待
云端验证。真实 notepad-- x64 的销毁、多窗口、快速切换、路径变化、导出、相对图片、链接、
主题、滚动及大文档性能仍需按 `docs/testing.md` 单独复核。模拟宿主测试不能证明真实 ABI 或
宿主窗口结构兼容性，因此本单保持“修复完成／待验证”。

## BUG-013：宿主适配散落在控制器和 Dock 中

### 交付信息

- **状态**：修复完成／待验证
- **基于的提交**：`33df2f127aa7276ba9f2b3adf417c84540088955`
- **修复提交**：本记录所在提交；交回时使用 `git rev-parse HEAD` 核验
- **前置单据及对应提交**：BUG-012 `33df2f127aa7276ba9f2b3adf417c84540088955`
- **修改文件**：`CMakeLists.txt`、`markdownview.pro`、`docs/architecture.md`、
  `docs/host-compatibility.md`、`docs/bug-backlog-2026-09-07.md`、
  `docs/bug-fix-handoffs-2026-09-07.md`、`src/host_adapter.cpp`、
  `src/host_adapter.h`、`src/markdown_preview_dock.cpp`、
  `src/markdown_preview_dock.h`、`src/preview_controller.cpp`、
  `src/preview_controller.h`、`tests/markdown_preview_dock_lifecycle_test.cpp`、
  `tests/preview_controller_document_identity_test.cpp`

### 问题核实与根因

静态核实确认，控制器直接查找 `editTabWidget`、读取 `filePath`、识别右键 Markdown 动作、
查找和缓存 `MarkdownViewClass`、调用 `on_viewMarkdown`／`on_updataMarkdown` 并断开宿主即时
刷新连接；Dock 又自行查找原生预览的 `textEdit`，并了解宿主窗口中菜单栏、状态栏和工具栏的
裁剪方式。这些名称、动态属性、连接和窗口结构假设分散在调度层与展示层。该问题属于架构
改进，未声称已经在真实宿主中导致故障。

### 实际修改方案

新增可注入的 `HostAdapter` 接口及 notepad-- 默认实现。默认实现集中当前编辑器解析、路径读取
与变化事件识别、右键动作识别和宿主连接移除、原生预览创建／刷新、预览内部文本控件解析、
窗口装饰隐藏、即时刷新断连及编辑器／预览所有权协作。预览探测使用 `PreviewResult` 返回窗口、
文本控件、是否新建、耗时和明确错误；刷新也返回耗时和错误。控制器只负责状态、版本、调度、
菜单和滚动编排，并在失败时展示适配器错误；Dock 只接收已经解析的窗口和 `QTextEdit`，不再
查找任何宿主对象名或裁剪宿主窗口结构。

增加注入适配器测试源码：测试宿主没有 `editTabWidget`、`MarkdownViewClass`、`textEdit` 对象名
或宿主 Markdown 槽，仍可通过替换适配器完成首次渲染和 HTML 快照，证明控制器行为可脱离
notepad-- 结构测试。既有测试改为显式把已解析的 `QTextEdit` 交给 Dock。

### 相较计划的偏差

未把滚动条访问、文件扩展名判断或 Dock 的展示辅助函数拆成更多类，因为它们不是宿主专用
能力；保持最小适配层，避免在 BUG-014、BUG-015 前引入额外框架。适配器为普通 C++ 接口，
默认实例由控制器拥有，测试注入实例由调用方拥有；未修改插件入口 ABI 或宿主回调类型。

### 验收标准逐项结果

1. **宿主专用对象名和槽调用集中可查**：静态扫描确认 `editTabWidget`、`filePath`、
   `MarkdownViewClass`、`textEdit`、`on_viewMarkdown`、`on_updataMarkdown` 及插件宿主动态属性
   只在 `src/host_adapter.cpp` 定义和使用，`passed`。
2. **控制器不再散布兼容性探测**：控制器仅调用 `HostAdapter` 接口，Dock 接收解析结果；
   架构与兼容性文档已同步，静态 `passed`。
3. **单元测试可替换适配层**：新增 `TestHostAdapter` 和无宿主对象名测试源码，静态
   `passed`；Qt Test 实际运行因环境缺 Qt 而 `blocked`。
4. **真实基线宿主功能不退化**：默认适配器保留原对象名、槽调用、连接及所有权语义；既有
   BUG-012 回归源码继续编入相同测试目标，静态 `passed`；真实 notepad-- 功能 `not verified`。
5. **ABI 和静态 QScintilla 边界不变**：`src/ndd_plugin_api.h`、`src/plugin_exports.cpp` 和
   `notepad--/` 未修改；插件仍仅通过 Qt 元对象和 QWidget 边界访问宿主，`passed`。

### 验证证据

| 验证层级 | 结果 | 证据或原因 |
| --- | --- | --- |
| 静态检查 | `passed` | `git diff --check`；宿主名称集中扫描；BUG-012 祖先关系；notepad-- v3.8.3 / `91105f68` 基线核对；CMake/qmake 源文件清单；ABI、导出入口和宿主参考目录未修改 |
| 自动化测试 | `partial` | `./tests/release_publish_test.sh` passed；Qt Test 配置因缺少 `Qt5Config.cmake` blocked；新增注入适配器回归源码未运行 |
| Windows Release 编译 | `blocked` | 当前 Linux 环境没有 Qt 5.15.2、MSVC v142 和 Windows runner |
| Artifact 校验 | `not run` | 未生成 DLL、ZIP、SHA256 或 GitHub Artifact |
| 真实宿主测试 | `not verified` | 未在 notepad-- v3.8.3 x64 检查菜单、预览、刷新、导出、多窗口、主题和滚动回归 |

### 实际环境与未验证项

- **Qt**：Qt 5.15.2 开发包不可用
- **MSVC**：不可用
- **notepad--**：`v3.8.3`，提交 `91105f68b74382128f3313ac5af8accdc77de918`
- **操作系统**：Linux x86_64
- **CMake 配置目录**：`/tmp/markdownview-bug013-build`
- **可用测试输出**：`release publishing tests: passed`
- **新增测试源码**：`tests/preview_controller_document_identity_test.cpp`

三个 Qt Test 尚未编译运行；Windows Release DLL、Artifact 和真实宿主回归均未验证。默认
适配器仍有意绑定 notepad-- v3.8.3 的对象名、动态属性和槽名称，宿主升级时必须按兼容性文档
重新验证；可注入模拟测试只证明控制器与适配边界，不证明真实 ABI 或窗口结构兼容性。

## BUG-014：多文档原生预览缓存没有容量控制

### 交付信息

- **状态**：修复完成／待验证
- **基于的提交**：`3c78fa6f04b96a72e5aeb93097d6eaedadc9f600`
- **修复提交**：本记录所在提交；交回时使用 `git rev-parse HEAD` 核验
- **前置单据及对应提交**：BUG-001 `ba87695e48576e0711dadbcc2a97832b22efc5e4`；
  BUG-013 `3c78fa6f04b96a72e5aeb93097d6eaedadc9f600`
- **修改文件**：`docs/architecture.md`、`docs/host-compatibility.md`、
  `docs/bug-backlog-2026-09-07.md`、`docs/bug-fix-handoffs-2026-09-07.md`、
  `src/host_adapter.cpp`、`src/host_adapter.h`、`src/preview_controller.cpp`、
  `src/preview_controller.h`、`tests/preview_controller_document_identity_test.cpp`

### 问题核实与根因

静态核实确认，Dock 切换文档时只隐藏并移出旧原生预览，每个已浏览且未关闭的编辑器都可
继续保留一份 `MarkdownView`、`QTextDocument` 和图片资源。notepad-- v3.8.3 的
`ScintillaEditView::m_markdownWin` 是 `QPointer<MarkdownView>`，所以这是无容量上限的缓存策略，
不能仅凭保留对象把它描述为内存泄漏。当前环境无法运行 Qt 测试或真实 Windows 宿主，实际
RSS、富文本对象和图片缓存规模仍待测量。

### 实际修改方案

每个宿主窗口的 `PreviewController` 维护最近使用顺序，最多保留 3 份原生预览，当前活动文档
始终不参与淘汰。切换前保存当前文档的滚动比例；淘汰后只丢弃重型原生预览，轻量阅读状态
继续保留。关闭同步滚动时再次访问已淘汰文档，宿主创建新预览后恢复该文档自己的比例。

淘汰统一经过新增的 `HostAdapter::releasePreview()`。默认 notepad-- 适配器先清除编辑器上的
预览身份和当前状态属性，断开适配器安装的编辑器销毁到预览 `deleteLater` 连接，再隐藏并
延迟删除预览。宿主自身的 `QPointer` 会在对象销毁后归零，因此下一次 `on_viewMarkdown()`
能够创建新实例。Dock 继续通过既有 `destroyed` 跟踪清理当前控件和滚动连接；缓存条目使用
`QPointer<QWidget>`，关闭标签后自动剪枝，避免悬挂编辑器地址。

### 相较计划的偏差

容量 3 是用于控制对象数量的保守固定上限，不声称来自真实宿主内存曲线；没有引入按字节
估算、后台解析或复杂缓存框架。隐藏 Dock 不主动清空缓存，而是继续遵守同一容量并停止待执行
渲染，保持再次显示时的复用语义。由于本机缺少 Qt 5.15.2，真实对象计数与 Windows RSS
测量保留为交回复核项。

### 验收标准逐项结果

1. **超出容量后对象数量受控**：新增 4 文档依次预览、处理 `DeferredDelete` 后断言仅有
   3 个 `MarkdownViewClass` 的测试源码，静态 `passed`，运行 `blocked`。
2. **返回已淘汰文档可以重新预览**：测试断言首文档渲染次数从 1 增至 2，且重建后总对象数
   仍为 3；静态 `passed`，运行 `blocked`。
3. **关闭标签后资源释放**：测试用 `QPointer` 保存预览，删除对应编辑器并处理延迟删除后
   断言为空；静态 `passed`，运行 `blocked`。
4. **无悬挂指针、重复删除或逐键刷新恢复**：淘汰由适配器清理属性并与宿主 `QPointer`、Dock
   销毁跟踪协作；既有文本变化时再次断开宿主即时刷新路径不变。静态 `passed`，真实宿主
   `not verified`。
5. **提供前后测量**：自动化场景定义从 4 个已创建文档到最多 3 个存活预览的对象计数；Qt
   实际运行 `blocked`，真实宿主 RSS 和图片缓存 `not verified`。

### 验证证据

| 验证层级 | 结果 | 证据或原因 |
| --- | --- | --- |
| 静态检查 | `passed` | `git diff --check`；BUG-001/BUG-013 祖先关系；notepad-- v3.8.3 / `91105f68`；宿主 `m_markdownWin` 为 `QPointer`；淘汰入口集中；ABI、导出入口与宿主目录未修改 |
| 自动化测试 | `partial` | `./tests/release_publish_test.sh` passed；Qt 配置缺少 `Qt5Config.cmake` blocked；新增缓存测试未运行 |
| Windows Release 编译 | `blocked` | 当前 Linux 环境没有 Qt 5.15.2、MSVC v142 和 Windows runner |
| Artifact 校验 | `not run` | 未生成 DLL、ZIP、SHA256 或 GitHub Artifact |
| 真实宿主测试 | `not verified` | 未在 notepad-- v3.8.3 x64 测量对象、RSS、图片缓存及淘汰重建 |

### 实际环境与未验证项

- **Qt**：Qt 5.15.2 开发包不可用
- **MSVC**：不可用
- **notepad--**：`v3.8.3`，提交 `91105f68b74382128f3313ac5af8accdc77de918`
- **操作系统**：Linux x86_64
- **CMake**：3.28.3
- **编译器探测**：GNU C++ 13.3.0
- **CMake 配置目录**：`/tmp/markdownview-bug014-build`
- **可用测试输出**：`release publishing tests: passed`
- **新增测试源码**：`tests/preview_controller_document_identity_test.cpp`

Qt Test 尚未编译运行；Windows Release DLL、Artifact 与真实宿主验证均未完成。容量 3 的真实
内存收益、切换体验、宿主 `QPointer` 归零和重建、关闭标签、隐藏 Dock、图片缓存释放、阅读
位置恢复及无逐键宿主刷新仍需在 notepad-- v3.8.3 x64 中复核，因此保持“修复完成／待验证”。
