# REQ-003 标题大纲与章节定位验证记录

日期：2026-09-14。实现起点：`d8b9ddf4d4d3e84e88f73a5fb23bad978495ffed`。
宿主只读基线：notepad-- v3.8.3，`91105f68b74382128f3313ac5af8accdc77de918`。

## 行为与实现

- 新增大纲显示／隐藏、左右方向、拖动及键盘宽度设置；默认左侧、展开全部，状态只在窗口内保存。
- H1～H6 从当前渲染快照生成；标题记录携带文档身份、版本和块位置。同版本源码按完整有序标题序列核对源行。
- 最新快照激活标题可将源码与预览一起定位，连续同步关闭时仍可用；显式目标优先于旧比例同步、轮询和延迟布局。
- 旧快照只允许预览导航并提示刷新；当前章节标记独立于键盘选中项和折叠状态；缺少快照或标题有明确空状态。
- 源码读取和滚动经 HostAdapter 及 Qt 公共接口接入；未修改宿主或插件入口 ABI，未引入 QScintilla 链接依赖。
- 复杂或不能可靠核对的标题语法返回明确映射失败，仅保留预览导航；不根据同名标题首个匹配或比例猜测源码。
- 目标位于源码折叠内部时，展开必要源码祖先以保证可见；正文、光标、选区和修改标记保持不变。

主要改动位于 `heading_index.*`、`heading_outline.*`、`source_navigation.*`、`host_adapter.*`、
`markdown_preview_dock.*` 和 `preview_controller.*`。CMake／qmake 同步源文件，新增第六组 Qt 回归、
可选真实 QScintilla 控件探针与样本生成脚本。没有更换渲染器或增加运行时第三方依赖。

## 分层结果

| 层次 | 结果 | 证据与边界 |
| --- | --- | --- |
| 静态检查 | passed | ABI／宿主无改动、依赖检查、Markdown 链接及 git diff --check |
| Linux Release 编译 | passed | GCC 13.3、Qt 5.15.13、C++14；无新增编译警告 |
| Linux Qt 回归 | passed | 六组 CTest，158 项 QtTest，0 失败／跳过 |
| 发布脚本隔离测试 | passed | mock gh；未操作真实 Release |
| Linux 真实 QScintilla 控件探针 | passed | 正式 source_navigation.cpp 的独立 Qt 模块，13 场景，0 失败 |
| GitHub Windows Release／Qt 回归 | not run | 推送后补充该提交的流水线结果 |
| Windows Artifact | not run | 待工作流产物生成及下载校验 |
| 真实 Windows notepad-- | not verified | 由用户验证加载、实际源码跳转、键鼠、主题／DPI 与设备性能 |

当前六组计数：lifecycle 13、document identity 40、multi-window 8、diagnostics 6、font 62、outline 29。
Linux 日志位于 `build/req003/markdownview_*_tests.txt`、`build/req003/ctest.log` 与 `build/req003/build.log`。
Linux Qt offscreen 的平台提示及 Fontconfig 缓存提示保存在运行日志，不作为 Windows 兼容性结论。

## 验收覆盖

| 验收项 | 自动化覆盖 |
| --- | --- |
| AC-01 | 固定 H1～H6、中文／Emoji、同名、跳级、ATX／Setext 与行内格式；源行独立预期 |
| AC-02 | 无标题、无快照和切换标签时清空 |
| AC-03 | 双侧同一标题、文末、预览／源码延迟布局和兼容轮询 |
| AC-04 | 首标题之前和文末章节；用户预览滚动、高亮与反向同步 |
| AC-05 | A→B→A、修改标题、标签关闭、读取和导航回调重入失效 |
| AC-06 | 手动旧快照只跳预览，不刷新、不推动源码 |
| AC-07 | 500 标题构建／映射／交互测量、不同字号及键盘激活 |
| AC-08 | 左右、显隐、宽度、窗口隔离，不触发全文渲染或源码重读 |
| AC-09 | 手动已同步／保护已同步无虚假待刷新，版本不一致才提示 |
| AC-10 | 当前标记独立于选择与焦点，折叠祖先提示 |
| AC-11 | 图片／表格、围栏、缩进代码、行内格式、中文、重复、LF／CRLF／CR |
| AC-12 | 同步关闭仍主动双侧导航；源码用户滚动只在同步开启且最新时恢复主导 |
| AC-13 | 缺少能力、映射不可靠、无效目标及回调期间变更均拒绝误跳 |

这些覆盖证明插件侧契约；实际 Windows notepad-- 的同名标题、源码折叠、滚轮、DPI、
窗口布局和延迟事件仍需按 [手工步骤](testing.md) 逐项验证，不能把自动化覆盖当作真机验收。

## 性能与截图

500 标题用例预热一次、每项 20 次：索引提取／映射／树构建与双侧激活分别记录。
最终同次记录：索引／映射／树构建中位数 3.885 ms、P95 4.034 ms；双侧激活中位数
0.980 ms、P95 1.564 ms。完整数据保存在 [测量记录](measurements/req003-2026-09-14.json)。
普通无行内标记标题不调用片段 Markdown 解析，避免每个标题创建解析文档；有格式的标题仍
使用小片段核对。批量建树期间合并展开信号与绘制，避免逐节点重复更新当前章节标记。滚动不重建索引，章节查询只检查最近标题的几何位置。

大纲占用正文宽度后会增加换行，测量保留默认显示与隐藏大纲对照。共同样本覆盖约 100 KiB、
1 MiB、5 MiB、图片／表格、500 标题、未保存增长及双窗口；记录完整行为增量，不宣称消除
现有大文档或字体读取成本。模拟程序的峰值 RSS 和耗时不等于真实 Windows 设备数据。

模拟截图：`build/req003/screenshots/req003-outline-left.png` 与 `req003-outline-right.png`；
已检查标题显示、左右方向、当前章节标记及源码／预览同一目标。GitHub 工作流也会上传对应截图。
样本生成：`python3 scripts/generate-heading-sample.py`。

## 验证入口

```bash
cmake -S . -B build/req003 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH="$VERIFY_QT_PREFIX"
cmake --build build/req003 --parallel
ctest --test-dir build/req003 --output-on-failure --no-tests=error
bash tests/release_publish_test.sh
```

真实 QScintilla 探针构建方式见 [测试说明](testing.md)，使用指定宿主静态库和包含目录。
本地 Linux 正式插件的动态依赖与导入符号已检查，无 Qsci／Scintilla 导入；它不能代替 Windows Artifact 校验。

## 同机比较与复核

所有样本预热一次后采样 20 次。超过 10% 的项目已反向顺序复测；500 标题的主要增量是
新建可见树、状态与缓存切回时的树恢复，不是全文额外解析。发现批量展开触发重复标记更新后，
合并建树信号与绘制；最终 500 标题对照如下（P95，ms）：

| 操作 | 基线 | 候选 |
| --- | --- | --- |
| first-render-sync | 11.421 | 12.746 |
| explicit-refresh-sync | 67.619 | 72.594 |
| cached-tab-activation-sync | 5.378 | 6.427 |
| typing-burst-20 | 0.133 | 0.361 |

批量输入记录的是 20 次文本变更通知的总耗时，新增状态／标题交互失效有亚毫秒增量，
没有恢复逐键解析。首次显示／缓存切回保留必要的新大纲构建成本；不能只用交互 P95 代替它。
图片／表格与 5 MiB 首轮的输入波动在复测后收敛；双窗口首次打开复测由 11.074 ms 增至
12.704 ms，增加约 1.63 ms，包含每窗口独立树、设置菜单和布局的创建。

100 KiB 分层初测中，显示大纲后的缩放剩余布局由 12.835 ms 增至 14.154 ms；隐藏大纲、
保留正文宽度的对照由 12.181 ms 至 12.539 ms（约 +2.9%）。同宽下其他分层 P95 均在
10% 以内。默认可见侧栏缩窄正文导致的换行布局属于可见功能成本，提供显隐和可调宽度。
源码 accessibility 读取与真实滚动不在既有模拟性能程序中，正式真实控件探针证明行为，
不冒充真机性能预算；实际 Windows 数据仍待用户采样。
