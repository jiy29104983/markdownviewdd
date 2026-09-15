# 回归测试与手工验证

2026 年 9 月 7 日二次审查修复的临时 Linux Release 编译及回归结果见
[verification-2026-09-07.md](verification-2026-09-07.md)。该记录与 Windows 实际宿主验证分开。

## 统一自动化入口

Windows、Qt 5.15.2 `msvc2019_64` 和 MSVC v142 环境中，从仓库根目录执行：

```powershell
.\scripts\run-regression-tests.ps1 -QtRoot "D:\Qt\5.15.2\msvc2019_64" -Clean
```

该命令先以 `BUILD_TESTING=ON` 完成 Release 构建，再执行全部 CTest 行为测试，最后用 Git for
Windows 提供的 `bash.exe` 运行隔离的 Release 发布测试。已有同配置构建时可以执行：

```powershell
.\scripts\run-regression-tests.ps1 -QtRoot "D:\Qt\5.15.2\msvc2019_64" -SkipBuild
```

Qt 测试固定使用 offscreen 平台，不打开真实外部链接。发布测试使用临时目录和 mock `gh`，
不访问 GitHub API、不创建或覆盖 Release。任一测试缺失、超时或失败时统一入口返回非零状态。

CTest 显式将 QtTest 文本结果写入 `build/markdownview_*_tests.txt`，统一入口和云端脚本读取
并显示这些文件，避免 Windows 下标准输出为空时只看到退出码。云端一次执行全部八组 Qt 测试，
失败也继续执行其他组及隔离发布测试；QtTest 文本记录与 `build/Testing/Temporary/LastTest.log`
一同作为独立诊断 Artifact 上传。直接调用 CTest 时，应同时查看这些 QtTest 文本记录。
任一回归失败仍会阻止插件打包和发布。

Windows 的 Qt 5.15 offscreen 后端使用基础字体数据库，CTest 将 `QT_QPA_FONTDIR` 指向
系统 `Fonts` 目录，避免 Qt 安装包未携带 `lib/fonts` 时缺失字体并影响文字命中测试。
链接测试在实际链接文字内部点击，并先断言 `anchorAt()` 命中目标；路径使用 Qt 临时目录，
避免把 Linux 的 `/tmp` 根路径假设带入 Windows。失败注释只摘录断言及其上下文，完整警告
保留在日志 Artifact 中，防止大量环境警告截断真正的错误。

需要定位单组测试时，可在完成配置后执行：

```powershell
ctest --test-dir build -C Release --output-on-failure
ctest --test-dir build -C Release -L lifecycle --output-on-failure
ctest --test-dir build -C Release -L document --output-on-failure
ctest --test-dir build -C Release -L multi-window --output-on-failure
```

## 自动化覆盖边界

| 测试 | 主要覆盖 | 不证明的事项 |
| --- | --- | --- |
| `markdownview_lifecycle_tests` | Dock／预览销毁顺序、链接与锚点、相对图片导出、主题格式和位置、用户滚动取消旧恢复目标 | notepad-- 真实控件销毁顺序、系统 URL 打开器、真实主题视觉 |
| `markdownview_document_identity_tests` | 快速切换、防抖、后台内容／路径变化后的缓存失效、隐藏 Dock 导出、原子保存、滚动信号传递与位置恢复、慢文档策略跨标签保存 | 真实宿主 ABI、真实磁盘对话框、设备性能结论 |
| `markdownview_multi_window_tests` | 每窗口控制器、重复初始化、窗口关闭和右键菜单隔离 | notepad-- 多进程或非基线窗口结构 |
| `markdownview_font_tests` | 宿主配置样本、18 个主题、只读与待写缓存、打开次数、默认字体、原生滚轮对照、比例格式、旧快照、缓存和多窗口 | 真实 Windows 路径、宿主保存操作、DPI 视觉和滚轮手感 |
| `markdownview_diagnostics_tests` | 日志初始化幂等、窗口标识、容量轮转、不可写目录和路径脱敏 | Windows 多进程日志与长期使用情况 |
| `release_publish_test.sh` | Release 不存在、相同附件、缺失附件、内容冲突和查询错误 | GitHub 在线权限、网络、并发和生产 Release |

这些测试使用可替换的宿主窗口、编辑器和原生预览，仅证明插件侧行为。Windows Release DLL
编译、Artifact 内容／架构检查和真实 notepad-- x64 加载测试必须单独记录，不能由 CTest 代替。

## 手工回归输入与步骤

在 [preview-demo.md](../examples/preview-demo.md) 中集中提供标题、引用、代码、表格、外部链接、
页内锚点、中文及空格相对图片、主题和长文档滚动输入。将候选 DLL 加载到
[host-compatibility.md](host-compatibility.md) 指定的 notepad-- x64 基线后，至少检查：

1. 打开、隐藏、重新显示和销毁预览，关闭标签、Dock 及宿主窗口。
2. 两个宿主窗口独立显示、刷新和关闭，不互相改变菜单与预览。
3. 快速切换标签并输入，确认预览、滚动和导出始终属于当前活动文档。
4. 首次保存、改名和改变扩展名后，标题、资源目录与菜单状态立即更新。
5. 关闭同步滚动后刷新保持阅读位置；重新开启后由编辑器位置主导。
6. 点击锚点、HTTPS、`file:` 和相对链接，拖选、复制和滚轮操作不误触链接。
7. 明暗主题下检查标题、引用、代码、表格和链接，并记录切换前后截图。
8. 隐藏 Dock 后导出最新 HTML，检查相对图片已嵌入，取消与失败保存不破坏既有文件。
9. 使用超过 1 MiB 及图片／表格密集文档记录首次打开、连续输入、停顿和手工刷新耗时。
10. 在 B 标签活动时使用“替换所有已打开文档”修改已预览的 A，返回 A 并导出，确认内容最新。
11. 关闭同步滚动，刷新后主动滚到其他位置，再调整 Dock 大小，确认不会回到旧的恢复位置。
12. 慢文档进入手工刷新状态后，切到普通文档再返回，确认继续暂停自动刷新，普通文档不受影响。

失败时保存 `%TEMP%\markdownview-<进程号>.log` 及适用的 `.1`～`.3` 轮转文件，并分别标注静态检查、自动化测试、Windows Release
编译、Artifact 校验和真实宿主测试为 `passed`、`blocked`、`not run` 或 `not verified`。

## REQ-001 刷新模式回归

控制器文档测试新增手动模式连续输入超过 2 秒、首次显示与重开侧栏、A→B→A 缓存归属、
后台变更、模式切换／过期计时器、受保护与普通文档切换、隐藏导出、失败重试、错误正文拒绝
导出、双向滚动门控、缓存淘汰、控件销毁及同步渲染中重入。多窗口测试同时检查菜单、工具栏
和右键入口的窗口隔离。文件名标签与刷新状态标签分别断言，不能再要求错误覆盖文件名。

手工使用 `examples/preview-demo.md` 的刷新模式步骤检查以下组合：

1. 手动首次显示无缓存；手动已同步；手动编辑后旧正文仍可读且双向同步暂停。
2. 自动输入停顿后更新；保护暂停；显式刷新后已同步并保留保护说明。
3. 两窗口使用不同模式；A→B→A 且 B 没缓存；隐藏后导出 B 的最新正文。
4. 失败后旧正文保持或错误提示、详情复制与重试；窄侧栏下文件名、模式、状态均可辨认。

设置环境变量 `MARKDOWNVIEW_SCREENSHOT_DIR` 可让行为测试将实际 Qt 控件的 10 种状态截图
写到指定目录。普通测试没有此变量时不写截图。Windows Actions 将其设为 `build/screenshots`，
截图随 `test-diagnostics-<run>-<attempt>` 上传。Qt/offscreen 截图只能证明模拟界面呈现，
不能作为 notepad-- 真机加载或操作证据。

### 同机性能基线采样

`tests/benchmark` 是独立、可选的 Qt 模拟宿主采样程序，不加入正常回归的耗时预算。
用相同程序测量未改动源码和候选源码，`MARKDOWNVIEW_SOURCE_DIR` 指定待测源码检出目录：

```bash
cmake -S tests/benchmark -B build/refresh-benchmark \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$VERIFY_QT_PREFIX" \
  -DMARKDOWNVIEW_SOURCE_DIR="$VERIFY_SOURCE_DIR"
cmake --build build/refresh-benchmark --parallel 4
build/refresh-benchmark/refresh_benchmark 100k -platform offscreen
```

可选样本名为 `100k`、`1m`、`5m`、`tables-images`、`headings`、`unsaved`、`two-windows`。
每类预热一次、采样 20 次，输出 JSON 中位数、P95 和 Linux 进程峰值 RSS（其他平台该值缺省）。
固定窗口为 1000×700；测量首次同步渲染、20 次输入突发、显式同步刷新、最新 HTML 导出及
缓存标签接入；缓存接入测量包含取得 HTML 快照，以适配基线的延迟接入语义。

此采样不代表真实宿主性能。首次渲染在已启动的同一进程中创建新控件，不能称为进程冷启动；
后续异步布局、真实输入到停顿刷新的延迟、实际图片 IO／布局与 Windows 峰值内存仍需真机测量。
1 MiB／5 MiB 样本按磁盘文件大小生成，未保存样本为 100 KiB 加 20 次递增输入；不宣称已实现
未保存缓冲区规模保护。P95 或峰值 RSS 变化超过 10% 时复测并记录原因。

## REQ-002 字体跟随回归

字体组运行方式为 `ctest --test-dir build -C Release -L font --output-on-failure`，使用临时
用户配置根目录、模拟安装目录和私有副本目录；不会写入真实用户配置。结果保存到
`build/markdownview_font_tests.txt`，现有 GitHub 工作流自动发现新增 CTest 组并上传诊断。

`tests/fixtures/saved-host-font/` 保留 8 份原始探针对照样本及宿主读回结果、字节哈希。
它们提供独立于插件实现的预期数据；正式回归不依赖 QScintilla、宿主源码或 build 目录。
新增异常场景按 2026-09-12 需求采用平台默认与 12 pt，不能照搬旧探针的异常回退行为。
自动化覆盖主题映射、缺失／非法键、QStringList 中文／逗号／转义、用户文件整体优先、
Default 初始化差异、字体不可用、全局保存后的最终值、源文件清单／哈希与待写 QSettings
不被冲刷，以及所有成功／失败路径的临时副本清理。

预览测试用计数适配器验证每次打开一次读取，其他入口复用结果，并与独立只读 QTextEdit
接收相同原生滚轮事件的结果逐项对照。混合语法、空文档、纯标题和纯代码均测试原生缩放、
刷新保留、恢复动作和选区保持；检查正文／H1～H6／代码实际文档格式、语义属性及小数字号。
另外覆盖手动模式首次／右键打开、隐藏导出、标签和缓存淘汰、多窗口、旧快照与异步位置
恢复失效、配置失败后恢复、编辑器 zoom 和其他语言样式不参与基准。

设置 `MARKDOWNVIEW_SCREENSHOT_DIR` 时另生成 `req002-font-100.png`、
`req002-font-125.png`、`req002-font-dark.png`。它们是 Qt 模拟界面截图，不能代表真实宿主。

### 真实 Windows 检查（由用户执行）

在兼容性基线 notepad-- v3.8.3 x64 中加载候选 DLL，并使用 `examples/preview-demo.md`：

1. 在宿主保存 Markdown 正文为 12 pt，再打开预览；核对正文、六级标题、引用、列表、表格
   与代码。光标放在标题／代码或移走焦点，结果应一致；空文档和源码语言临时切换也要检查。
2. 预览内 Ctrl＋滚轮缩放，检查原生步进与手感。刷新、换标签、隐藏／重开仍保留缩放；
   编辑器内 Ctrl＋滚轮不改变预览，反向也不改变宿主字号。
3. 宿主保存为 14 pt，预览保持原基准；关闭再打开后才应用新基准并保留 Z。
   例如 12 pt 下缩放到 15 pt，重开后应为 17.5 pt。“恢复宿主字号”回到本次基准。
4. 打开期间切换明暗主题：颜色继续适配，字体仍用当次保存配置；保存主题后关闭再打开
   才更新字体。最小化恢复不算重新打开。检查菜单恢复动作可由键盘访问。
5. 手动旧快照在长文中部缩放，随后主动滚动、刷新、换标签；旧任务不得拉回旧位置，
   旧快照不能推动源码滚动。已同步且开启同步滚动时，仍由编辑器位置主导。
6. 两窗口采用不同缩放，只重开其中一个，新基准只影响该窗口；关闭窗口再新建从 100% 开始。
7. 缺少用户 Markdown 样式文件、字体不可用或损坏配置时，预览可用且无异常弹窗／状态栏。
   修复配置后需重开才生效；字体不可用但字号有效时应保留字号。
8. 在 125%／150% 显示缩放、窄侧栏和左右停靠下核对正文、标题、代码及中文字体，避免 DPI
   重复放大。提供对照截图与实际结果；未执行前真实宿主状态保持 `not verified`。

### 样式与布局采样

现有 `tests/benchmark` 增加 `--layers`，同一采样程序可对基线及候选源码分别执行：

```bash
build/refresh-benchmark/refresh_benchmark 100k --layers -platform offscreen
```

此模式把 `setMarkdown()`、预览接入及样式、主题事件处理、原生滚轮及比例衔接分别计时；另记录
显式请求 `documentSize()` 并处理当前排队事件的剩余布局时间。强制布局仅用于采样，不进入
产品路径，也不能声称测到了真实宿主的所有异步布局。候选版本另外测量隔离配置读取。
主题计时包括本轮排队事件和可能发生的布局，不冒充纯样式耗时。
每项预热一次再采样 20 次，原生滚轮基线缺少本次新增比例修正，比较时需说明行为增量。
本单性能与分层证据见 [verification-req002-2026-09-12.md](verification-req002-2026-09-12.md)。

## REQ-003 大纲、源码映射与主动导航回归

标准 CTest 新增 `markdownview_outline_tests`，使用 `-L outline` 可单独运行；统一入口
及 GitHub 工作流会自动发现此组，日志保存在 `build/markdownview_outline_tests.txt`。
新增覆盖包括 H1～H6、中文／Emoji、重复标题、ATX／Setext、行内格式、围栏与缩进代码、
引用／列表标题、LF／CRLF／CR、无标题／无快照、双侧定位、旧快照、左右布局与窗口隔离、
折叠祖先标记、键盘选择、源码读取／导航重入失效、延迟布局和同步开关。标题源行使用
固定样例的独立行号预期；模拟源码导航再按其文本块核对偏移，不复用产品映射函数计算预期。

500 标题测试预热一次、记录 20 次索引构建／映射／树更新和 20 次双侧激活的中位数、P95；
它使用 Qt 模拟宿主，不代表真实 Windows 设备性能。可复现样本生成方式：

```bash
python3 scripts/generate-heading-sample.py
```

样本默认保存为 `build/req003-headings-500.md`。设置 `MARKDOWNVIEW_SCREENSHOT_DIR`
时，大纲测试输出 `req003-outline-left.png` 和 `req003-outline-right.png`；云端与其他
模拟截图一同上传到诊断 Artifact。性能及分层验证结果见
[REQ-003 验证记录](verification-req003-2026-09-14.md)。

### 可选真实 QScintilla 控件探针

此独立项目使用已构建的固定宿主库，编译阶段不下载依赖、不编译或修改宿主：

```bash
cmake -S tests/host_capability -B build/host-capability \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$VERIFY_QT_PREFIX" \
  -DHOST_QSCINTILLA_INCLUDE_DIR="$VERIFY_HOST_INCLUDE" \
  -DHOST_QSCINTILLA_LIBRARY="$VERIFY_HOST_LIBRARY"
cmake --build build/host-capability --parallel
build/host-capability/host_probe build/host-capability/libqt_boundary.so -platform offscreen
```

运行时可通过 `QT_QPA_PLATFORM=offscreen` 设置平台；程序接受动态模块路径一个参数，
Qt 会先消费 `-platform` 参数。主程序直接使用正确参数的 Scintilla 行号／坐标接口做独立
断言；动态模块使用正式 `source_navigation.cpp`，只链接 Qt。结果为 JSON，失败返回非零。
覆盖自动换行、缩放、嵌套折叠、文末、中文／Emoji、重复标题源行、不同换行、多选区及无效能力。

### 真实 Windows 检查（用户执行）

1. 加载候选 DLL，打开 `examples/preview-demo.md` 与 500 标题样本，核对六级标题和重复项。
2. 设置中切换左右、显隐，并用鼠标拖动大纲与正文之间的分隔条调整宽度；检查刷新次数、模式、缩放和阅读位置。
3. 激活后确认源码与预览为同一标题并尽量靠顶；图片／表格密集、自动换行、源码折叠、文末和不同缩放都要检查。
4. 关闭连续同步后激活仍执行本次双侧跳转；重新开启后在源码主动滚动，应恢复编辑器主导。
5. 手动修改源码而不刷新：显示实际待刷新，激活只移动旧预览；刷新成功后提示消失并恢复双侧定位。
6. 保持大纲键盘选择，同时滚动预览；当前标记更新但选中项、焦点和折叠状态不变。折叠祖先显示后代提示。
7. A→B→A、关闭标签、主题／DPI切换、窗口缩放及两个宿主窗口分别操作，检查目标归属和延迟任务。
8. 保存真机截图／录像、日志和 20 次交互的中位数／P95。尚未执行时记录 `not verified`。

## REQ-004 预览内搜索回归

新增 `markdownview_search_tests`，通过 `ctest --test-dir build -C Release -L search
--output-on-failure` 单独执行，统一入口与 Windows 工作流自动发现第七组测试。
覆盖中文、标点、大小写、Emoji、非重叠匹配、片段／块／表格边界、4096 字符分片边界、
环绕、输入不滚动、多行粘贴、宿主快捷键隔离及模拟输入法组合事件。另覆盖上下文恢复、
旧快照与无快照、标签和窗口隔离、隐藏／关闭／销毁、更新中换查询、部分刷新失败、
正文与 HTML 字节不变、第三方显示选区保留、主题字号变化和大纲／搜索导航仲裁。

长文档使用可复现的 64 字符行（8 次 `needle ` 加 `padding\n`），分别生成
100 KiB、1 MiB、5 MiB 纯文本，精确预期为 12800、131072、655360 次命中。
检查完整计数、高亮至多 256 项、事件循环心跳及分批耗时。分批目标 4 ms，自动化以
单批、高亮更新及已显示快照搜索的事件心跳间隔均以 250 ms 作为明显阻塞的失败线；
这不是对真实 Windows 设备的性能承诺。另用未接入插件搜索的原生 QTextEdit 排版
同一 5 MiB 样本，记录原生惰性布局事件耗时，避免混淆首次排版与后续搜索。
`measurementsAndScreenshots` 预热一次、采样 20 次，记录含 75 ms 防抖的搜索中位数、
P95 及批处理 P95。完整原始数据和性能比较见
[REQ-004 验证记录](verification-req004-2026-09-14.md)。

设置 `MARKDOWNVIEW_SCREENSHOT_DIR` 后保存 `req004-search.png` 和
`req004-search-stale.png`；Windows 诊断 Artifact 同时上传。截图为 Qt 模拟窗口，
真实宿主检查仍由用户执行：

1. 加载兼容性基线 notepad-- x64，打开 `examples/preview-demo.md`；点击预览工具栏
   “查找”或在预览按 Ctrl+F，搜索中文、`needle`、标点、粗体、链接、表格和代码。
2. 比较大小写开关，连续 Enter／Shift+Enter，检查首尾环绕提示与当前项可见性；
   阅读到中部后修改查询，视图不能被持续拉回首个命中。
3. 在编辑区 Ctrl+F 仍打开宿主查找；预览与搜索栏使用插件查找。中文输入法选词的
   Enter／Esc 不应误导航／关闭；键盘粘贴、右键粘贴与拖入多行文本均只使用第一行。
4. 切换到手动模式，修改源码：旧快照结果继续可用并提示待刷新；搜索不触发刷新或
   源码跳转。刷新后核对上下文恢复；删除原目标后回到首项。
5. 查询期间快速换标签、隐藏／重开 Dock、关闭标签和窗口；查询按窗口隔离，隐藏保留、
   关闭搜索清空；无快照显示刷新提示，不搜索错误页。
6. 搜索过程中导出 HTML：正文格式和原选区不变，文件不含搜索高亮或控件。
   明暗主题、Ctrl＋滚轮缩放、DPI 与窄 Dock 下检查当前项和普通命中的区分。
7. 长文档记录搜索总耗时、输入响应和内存。计算中明确显示部分进度，完成后显示精确
   总数；受限的是同时高亮的数量，全部命中仍可前后导航。检查搜索与大纲交替激活，
   以及之后源码用户滚动的同步行为。

## 标题映射与搜索可见性修复回归（2026-09-15）

大纲组增加引用／嵌套引用和列表内代码围栏的隐式结束、显式闭合、根围栏内引用标记以及
空 ATX 标题样例；源码行号使用固定预期，确保后续真实标题可以映射。
搜索组增加默认换行策略的 60 列宽表格，检查下一处后命中起止位置及窗口调整后仍可见，
原选区、HTML、渲染次数和源码导航次数不变；用户横向滚动后再次调整窗口不得恢复旧目标。
另检查窗口扩大且滚动值保持 0 时，
新增可见命中补充高亮，总数、查询代次和渲染次数不变。

真实宿主补充检查：引用围栏结束后的大纲双侧跳转；窄 Dock 搜索宽表格右侧单元格并前后导航；
搜索完成后扩大窗口或改变大纲宽度，确认新增可见命中获得高亮。真机结果仍需单列记录。

## 正文标题层级缩进（2026-09-15）

字体组检查 H1 与正文左对齐、H2～H6 实际显示位置递进、Setext 标题、引用内标题、
反复主题切换不累加、原生缩放与刷新保持、恢复字号和 HTML 保留缩进。
手工使用六级标题样例，在窄侧栏、明暗主题及 125%／150% DPI 下核对长标题换行和层级辨识；
正文段落不随章节缩进。真实 Windows 宿主效果由用户验证。

## REQ-007 代码块阅读与复制

新增 `markdownview_code_tests`，使用 `ctest --test-dir build -C Release -L code
--output-on-failure` 运行；统一入口和云端工作流自动发现第八组。测试配置保存在临时目录，
不写用户实际偏好。复制断言逐字符检查预期字符串，覆盖语言／未知声明、空块、Tab（包括
Qt 展开的行首 Tab）、空行、尾部换行、特殊字符、相邻和内嵌围栏、引用与列表、缩进代码、
LF／CRLF／CR。另覆盖键盘、上下文菜单、剪贴板失败与重入、源码读取重入、手动旧快照、
切标签通知窗口、缓存淘汰、文档销毁、原生 Ctrl＋滚轮、主题、搜索、导出和多窗口偏好。

100 个代码块索引在预热一次后采样 20 次，日志记录中位数／P95。设置
`MARKDOWNVIEW_SCREENSHOT_DIR` 时生成 `req007-code.png` 和 `req007-code-stale.png`，
随云端诊断 Artifact 上传。Linux 仅安装西文字体时截图中文缺字，布局和文字断言分别记录；
不得把这种截图当作真实宿主视觉通过。分层结果见
[REQ-007 验证记录](verification-req007-2026-09-15.md)。

真实 Windows 手工验证由用户执行：

1. 加载兼容性基线 DLL，使用样例末尾的 REQ-007 章节，检查每块左上角语言、右上角
   小复制图标；直接点击及 Tab／空格可复制对应块。空围栏通过预览右键菜单访问。
2. 在不同代码块内右键，或将预览文本光标置于代码内按菜单键，复制完整块；拖选和原有
   选区复制仍可用。复制到能显示空白符的编辑器，核对 Tab、缩进、空行和末尾换行。
3. 开关“代码块视觉自动换行”，检查超长行、普通段落和表格的横向范围；两种模式复制
   相同。重启检查偏好恢复；两窗口设置相互不即时改变。
4. 手动模式修改源码后复制旧块，确认当前预览提示；切标签、刷新、淘汰缓存、关闭窗口，
   旧菜单不能复制别篇内容。源码能力不可用或剪贴板失败应明确反馈，不报成功。
5. 搜索高亮、明暗主题、Ctrl＋滚轮、窗口缩放及 125%／150% DPI 下核对控件、焦点和
   选择；复制成功不改变正文尺寸。导出 HTML 不含语言标签装饰、按钮或成功提示。
6. 保存真实宿主截图／日志及结果。真实宿主状态在用户执行前保持 `not verified`。

### 代码块附着复制图标调整

代码组增加直接点击不同块图标、按钮不覆盖第一行、页头随预览宽度调整、主题／字号后
间距不累加、旧按钮立即失效，以及文首代码刷新为普通段落后恢复根间距。导出检查恢复
原间距，顶部选择栏不存在。手工补充检查纵向和水平滚动、窄侧栏、明暗主题及 DPI 下的
图标位置、提示和键盘焦点。该轮证据见
[附着图标验证](verification-req007-icons-2026-09-15.md)。

## 预览界面重设计回归（2026-09-15）

生命周期组新增 `redesignedChromeRetainsState`：左右停靠→浮动→显式返回、外部改变浮动状态、
查询／模式／HTML 保留、关闭再打开、窄面板大纲临时收起与用户隐藏选择、无主窗口降级、所属
主窗口销毁，以及主题截图。原有文档组改为操作模式菜单和详情弹窗内的复制按钮。
`MARKDOWNVIEW_SCREENSHOT_DIR` 增加 `ui-docked-search`、`ui-floating`、`ui-returned`、
`ui-narrow`、`ui-error`、`ui-dark` 截图；均为 Qt 模拟界面。

真实宿主应按[界面方案](requirments/preview-ui-redesign.md)验收标题拖动、按钮附近拖动、
明暗主题／宿主字体、100%／125%／150%／200% DPI 及跨屏移动。模拟窗口与 Windows 编译
不代替这些验收。分层结果见[本轮验证记录](verification-ui-redesign-2026-09-15.md)。

界面生命周期用例也检查图标 2 倍像素请求；本地可通过 `QT_SCALE_FACTOR=1.25`、`1.5`、`2`
分别运行 `markdownview_lifecycle_tests redesignedChromeRetainsState -platform offscreen`，
核对逻辑宽度和窗口往返。此方式不模拟真实跨 DPI 屏幕迁移。

## 首次滚轮缩放字号修复（2026-09-15）

字体组新增 `wheelStartsAtDisplayedSize`：模拟宿主控件继承 8 pt 或 13 px 字体、
预览正文设置为 20 pt，分别验证首次向上／向下与连续 Ctrl＋滚轮均从当前显示字号开始，
并覆盖恢复字号、刷新后再次缩放，以及标题比例和选区保留。独立原生 QTextEdit 对照
通过 `setFont()` 设置起点，避免只改文档字体而复制产品缺陷。

真实 Windows 补充检查：首次打开后分别放大／缩小，恢复字号后再次缩放；
换标签、重开及主题切换后检查起点，确认标题、正文和代码一起缩放。

本轮本地验证（Linux ARM64、Qt 5.15.13、GCC 13.2.0）：

| 验证层 | 状态 | 结果与边界 |
| --- | --- | --- |
| 静态检查 | passed | `git diff --check`、修改文档的本地链接检查，编译无新增警告 |
| 自动化测试 | passed | 修复前新增四个数据行均失败；修复后八组 CTest 全通过，发布脚本隔离测试通过 |
| Linux Release 编译 | passed | 插件及全部测试目标编译成功 |
| Windows Release 编译 | blocked | 当前环境无 Visual Studio／Windows Qt 工具链 |
| Artifact 校验 | not run | 本轮未生成 Windows DLL／发布包 |
| 真实宿主测试 | not verified | 尚未在 Windows notepad-- 中加载本轮产物 |

本地原始日志位于已忽略的 `build/ui-redesign/wheel-before.txt`、`wheel-build.log`
和 `wheel-ctest.log`；发布脚本隔离测试日志为 `build/ui-redesign/wheel-release-test.log`。

## 宿主下拉菜单回归（2026-09-15）

文档组新增 `reorganizedMenuSharesReadingActions`，覆盖共享大纲动作、左右位置与窗口隔离、
隐藏时切换位置、勾选大纲打开预览、手动模式打开查找不渲染、旧快照搜索、再次打开选中查询，
以及无文档／不支持文档时刷新和导出禁用。已有多窗口及字体回归同步适配子菜单与新文案。
设置 `MARKDOWNVIEW_SCREENSHOT_DIR` 时生成 `menu-reading.png`，仅为 Qt 模拟界面证据。

真实 Windows 需补充检查：根菜单和两个子菜单的键盘助记键、Ctrl+Shift+M 和预览内 Ctrl+F；
从主菜单与面板设置分别切换大纲左右位置；窄面板显式显示大纲时的加宽提示；
深浅主题及 100%／125%／150% DPI 下的文字、勾选、快捷键、箭头和分隔线。
真实宿主结果独立记录，不由模拟截图或 Windows 编译替代。

本轮本地环境为 Linux ARM64、Qt 5.15.13、GCC 13.3.0，Release 构建。
验证日志保存在已忽略的 `build/ui-redesign/` 目录下：`menu-build-final.log`、
`menu-ctest-final.log` 和 `menu-release-test.log`。Windows 编译及 Artifact 结果以
本次提交对应的 GitHub Actions 为准；真实宿主测试仍为 `not verified`。

| 验证层 | 本地状态 | 结果与边界 |
| --- | --- | --- |
| 静态检查 | passed | diff 空白检查、文档本地链接检查、编译无警告 |
| 自动化测试 | passed | 最终八组 CTest 全通过（54.82 秒），发布脚本隔离测试通过 |
| Linux Release 编译 | passed | 插件及全部测试目标通过 |
| Windows Release 编译 | blocked | 本地无 Windows 工具链，另查对应提交的云端结果 |
| Artifact 校验 | not run | 本地未生成 Windows 产物，另查云端产物 |
| 真实宿主测试 | not verified | 未在真实 notepad-- 中加载本轮产物 |
