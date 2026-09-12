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
并显示这些文件，避免 Windows 下标准输出为空时只看到退出码。云端一次执行全部五组 Qt 测试，
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
