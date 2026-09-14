# REQ-004 预览内搜索验证记录

日期：2026-09-14。实现起点：`e9828ba6762d7d005e80685be74f0dfdc61ba5be`。
最终功能提交：`7efa684428d19fc78a8962550ca28a3522fe6819`，已推送 `origin/main`。

## 交付行为

预览工具栏新增“查找”，支持单行字面查询、大小写开关、精确计数、临时高亮、前后环绕
和局部快捷键。输入及刷新重算保持阅读位置，只有上一处／下一处主动定位；编辑器的
Ctrl+F 保留宿主行为。搜索只使用当前展示快照，旧快照显示待刷新，不读取源码、不增加
全文渲染或驱动源码跳转，正文格式、原选区及导出 HTML 保持不变。

- 匹配不重叠，不跨段落／单元格；同段可以跨粗体、链接等格式。默认不区分大小写。
- 关闭搜索清空查询和大小写状态，焦点回预览；隐藏 Dock 保留查询并暂停任务，重开校验快照。
- 刷新后重新计算全部坐标，以段内前后各 24 字符上下文和位置距离恢复原目标，失败则选首项。
- 75 ms 防抖、每片段最多 4096 字符，单批目标 4 ms／65536 字符，批间间隔 5 ms。
  完整结果保存整数位置，空间随命中数增长；同时高亮至多 256 项，界面明确说明高亮范围。
- 查询代次、文档身份、展示版本、文档修订号与 QPointer 防止旧任务覆盖新结果。
  正文替换时同步失效并延后清理高亮；文档销毁时在私有数据仍存活的 destroyed 信号中清理。
- 复用大纲的 `navigatePreviewPosition()` 和滚动仲裁，搜索导航取消旧的大纲源码目标。
- 高亮只维护可见范围及当前项，范围不变时复用；原生惰性排版未就绪时继续计数并提示
  “正文排版中，高亮稍后显示”，不通过命中测试或 documentSize 强制推进原生排版。

主要改动：`src/preview_search.*`、Dock、控制器、CMake／qmake、专项测试、样例和文档。
没有修改宿主、插件 ABI、导出签名或版本，没有新增运行时依赖。
更详细的边界见 [架构说明](architecture.md)，操作与回归入口见 [测试说明](testing.md)。

## 分层结果

| 层次 | 结果 | 证据与边界 |
| --- | --- | --- |
| 静态检查 | passed | 改动范围、构建接入、ABI／宿主无改动、Markdown 链接及 git diff --check |
| Linux Release 编译 | passed | GCC 13.3、Qt 5.15.13、C++14、无新增编译警告 |
| Linux Qt 回归 | passed | 七组 CTest，195 项 QtTest，0 失败／跳过；48.60 秒 |
| 发布脚本隔离测试 | passed | mock gh，未操作真实 Release |
| 地址检查 | passed | RelWithDebInfo + AddressSanitizer；4 个生命周期用例，含初始化／清理共 6 passed；关闭 Qt 平台泄漏统计 |
| GitHub Windows Release／回归 | passed | 7efa684；工作流 34826851376；Qt 5.15.2／MSVC v142／x64；195 项通过 |
| Windows Artifact | passed | 官方 digest、包内 SHA256、3 个文件、PE32+ x64、两项入口均通过 |
| 真实 Windows notepad-- | not verified | 用户负责加载、输入法、键鼠、主题／DPI、截图和设备性能验证 |

QtTest 计数：lifecycle 13、document identity 40、multi-window 8、diagnostics 6、
font 62、outline 30、search 36。地址检查不重复计入 195。
Linux 日志位于 `build/req004/markdownview_*_tests.txt`、`ctest-final.log`、`build.log`、
`release-tests.log`、`asan-final.log`。Qt offscreen 平台提示不代表真实宿主兼容性。

## 验收覆盖

| 验收项 | 自动化证据 |
| --- | --- |
| AC-01 | 中文、标点、大小写、Emoji、空白、非重叠、KMP 前缀／分片边界；标题／正文／表格／代码及跨格式 |
| AC-02 | 首尾环绕提示、当前项可见、原选区不变、搜索不触发源码定位 |
| AC-03 | 空查询、无结果、多行粘贴和第一行为空；关闭清理与默认状态恢复 |
| AC-04 | 分批中换查询／文档／版本、换标签、隐藏重开、正文替换／销毁、窗口销毁、部分刷新失败 |
| AC-05 | 搜索前后 QTextDocument HTML 与导出字节完全相同；保留非搜索显示选区 |
| AC-06 | 手动编辑后搜索旧快照、提示待刷新、宿主渲染次数不增加 |
| AC-07 | 编辑器 Ctrl+F 触发宿主；预览／搜索栏触发插件；模拟 IME 组合、Enter／Shift+Enter／Esc 和退出焦点 |
| AC-08 | 100 KiB／1 MiB／5 MiB 精确数量、256 项高亮上限、完整计数提示、分批／高亮／事件响应检查及原生布局对照 |

## 搜索性能与原生排版边界

规模样本为固定 64 字符行（8 次 `needle ` 加 `padding\n`），分别生成 100 KiB、1 MiB、
5 MiB，精确命中 12800、131072、655360 次。匹配批次、高亮更新及已显示快照搜索的事件
心跳间隔均以 250 ms 为明显阻塞的回归失败线。它不是对真实设备的性能承诺。

下表是同次规模验证中，对已显示快照搜索 `needle` 的单次读数，不冒充 20 次统计：

| 平台 | 规模 | 完整搜索 | 最大匹配批次 | 最大高亮更新 | 最大事件间隔 |
| --- | --- | --- | --- | --- | --- |
| Linux | 100 KiB | 99.740 ms | 2.248 ms | 0.850 ms | 11.854 ms |
| Linux | 1 MiB | 296.802 ms | 2.379 ms | 3.546 ms | 12.297 ms |
| Linux | 5 MiB | 1413.340 ms | 3.082 ms | 23.255 ms | 25.959 ms |
| Windows | 100 KiB | 104.337 ms | 2.897 ms | 0.855 ms | 14.348 ms |
| Windows | 1 MiB | 326.920 ms | 3.304 ms | 4.211 ms | 14.112 ms |
| Windows | 5 MiB | 1766.270 ms | 3.500 ms | 20.370 ms | 23.909 ms |

500 段落样本预热一次后采样 20 次，搜索耗时包含 75 ms 防抖：

| 平台 | 中位数 | P95 | 匹配批次 P95 |
| --- | --- | --- | --- |
| Linux | 84.333 ms | 84.611 ms | 1.039 ms |
| Windows | 87.185 ms | 88.096 ms | 1.314 ms |

首个 Windows 候选虽全部测试通过，原始日志仍出现约 2.9 秒事件间隔；复核发现首次原生
排版和搜索高亮几何查询混在一起。后续增加事件归因、无搜索的原生对照及暖快照响应检查，
并延后高亮至布局就绪。最终 Windows 5 MiB 对照：未接入搜索的原生 QTextEdit 首次布局
约 17.29 秒，最大单个 `QTextDocumentLayout` 定时事件 1248.21 ms；搜索同时进行时最大
同类事件 1248.68 ms，高亮本身至多 22.56 ms。两者均保留原生排版自身的秒级事件，
不能把批次 4 ms 宣称成整个应用任何时刻都不阻塞。

该极端样本有 81920 行：搜索与首次布局重叠时计数约 24.88 秒，已显示快照的后续搜索约
1.77 秒，最长事件间隔约 24 ms。未改写原生 QTextDocument 布局器；首次排版治理属于
后续渲染性能范围，真实 Windows 宿主首次显示、刷新和设备数据仍需用户验证。
本需求已消除搜索高亮强制推进尚未完成的原生布局，并保留精确计数与完整结果导航。

全部原始测量、查询及事件类型保存在 [测量 JSON](measurements/req004-2026-09-14.json)。
测试 `nativeLayoutWithoutSearch`、`longDocument` 和 `measurementsAndScreenshots` 提供
可复现样本／采样方式，初始布局与暖快照分开记录。

## 既有工作流的同机对照

起点 `e9828ba` 与搜索关闭的功能候选使用同一 benchmark、固定 Qt／窗口／样本。
每类预热一次后采样 20 次，覆盖 100 KiB、1 MiB、5 MiB、图片／表格、500 标题、
未保存增长及双窗口。首轮先基线后候选；输入通知出现约 0.02 ms 增量，去掉关闭搜索栏
时的不可见标签更新，再按候选→基线顺序复测全部样本。两轮原始数据完整保留。

反向顺序复测的全部操作 P95 与进程峰值 RSS 均未恶化超过 10%。代表性 P95：
100 KiB 首次同步显示 3.610→3.639 ms；5 MiB 首次同步显示 76.725→75.648 ms；
500 标题连续 20 次输入通知 0.355→0.365 ms。该对照只用于搜索关闭时的既有路径，
不替代上文搜索开启成本。后续两个优化只作用于打开的搜索栏。

## Windows 工作流与候选包

功能提交 `7efa684428d19fc78a8962550ca28a3522fe6819` 的工作流 `34826851376`
于 2026-09-14 09:17:44 UTC 完成为 `success`。Release DLL、七组 Qt 测试、发布脚本隔离
测试、打包及上传成功；普通 main 构建跳过标签版本检查和 Release 发布。

候选 Artifact：`markdownviewdd-v0.2.8-windows-x64-7efa684`，保留期为既定 14 天。

- Artifact 外层 SHA256：`c567b87066e7b9ae51f1b55724c7d0478e237ae8e8c1bed5d94d1938a22217ce`。
- 包内 ZIP SHA256：`f544fbec5d96860737caa0f47ffb32c03efd1823af466bf8ec7ba752d9219e5d`。
- ZIP 仅含 `plugin/markdownviewdd.dll`、`README.md`、`LICENSE`。
- DLL 为 PE32+ x86-64、319488 字节，含 `NDD_PROC_IDENTIFY`、`NDD_PROC_MAIN`。
- DLL SHA256：`c0c7c1a828f4d099defde2aab24cc69e35c5110c052798e70ea31b1167a67a23`。
- Qt 依赖沿用宿主运行库，无 QScintilla／qmyedit 导入。
- 诊断 Artifact SHA256：`ad7ba536c560f95a86e562ace66dd42dd52ff71c02200b86d22ea7809fbae0fc`。

使用公开 Artifact 入口下载，候选与诊断 ZIP 均与 GitHub API 的官方 digest 核对一致，
包内校验文件也通过。包及检查结果位于 `build/req004/windows-7efa684/`，原始日志和截图
位于 `build/req004/windows-diagnostics-7efa684/`。

已检查 Linux 和 Windows Qt 模拟截图 `req004-search.png`、`req004-search-stale.png`，
核对当前项／其他项高亮、计数、范围提示及待刷新状态。这些不是 notepad-- 真实宿主截图。

实现、Linux、GitHub Windows 工作流及 Artifact 校验完成。需求文件和共同索引已在本地
标为完成，仍按仓库规则忽略；本次没有打标签或发布 Release。真实宿主结果保持
`not verified`，由用户按测试文档补齐。
