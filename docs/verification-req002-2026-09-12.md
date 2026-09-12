# REQ-002 保存字体跟随与比例排版：验证记录

本单在 `d8bbc12140c65704be41ecc7fe05769d86989243` 基线上实现，按 2026-09-12 确认的
六项限定交付。宿主固定为 notepad-- v3.8.3，提交
`91105f68b74382128f3313ac5af8accdc77de918`；未修改宿主源码、插件 ABI、导出签名或版本号。
本次为普通主分支构建，不创建标签或发布 Release。

## 实现结果

- HostAdapter 封装保存配置读取，使用 QFile 只读源文件和 QSettings 私有副本解析。
- 每次主动打开只读一次；其他入口复用窗口基准。失败采用默认字体与 12 pt，字体族不可用
  且字号有效时保留字号，不添加异常界面、监听、重试或读取大小阈值。
- 原生 Ctrl＋滚轮处理不变，记录其文档默认字号得到窗口 Z；正文、六级标题和等宽代码
  使用绝对比例排版，刷新、缓存重建和重新打开保留 Z，菜单提供“恢复宿主字号”。
- 纯样式操作不重新解析或改变内容版本；旧快照、窗口隔离、导出及阅读位置继续遵守各自规则。
- 批量提交格式修改，未改变的缓存跳过样式遍历，减少不必要的富文本布局。

设计见 [architecture.md](architecture.md)，自动化及 Windows 手工步骤见
[testing.md](testing.md)，手工输入见 [preview-demo.md](../examples/preview-demo.md)。

## 验收对应

| 验收项 | 自动化证据与边界 |
| --- | --- |
| AC-01、AC-17 打开时读取、其他入口复用 | `openingReadsOnceAndOtherEventsReuse`、`manualContextOpenAndPendingReopenDoNotParse`；覆盖最小化、主题事件、等待超过 1500 ms、重复显示和右键入口 |
| AC-02、AC-03 比例、原生缩放和语义 | `nativeWheelAndSemanticFormats` 的混合语法／空文档／纯标题／纯代码四组，与独立 QTextEdit 对照原生结果 |
| AC-04、AC-08 基准重读、Z 保留与恢复 | `reopenKeepsZoomAndRefreshPreservesRatios`；12 pt×125%→宿主保存14 pt→重开17.5 pt，内容版本和解析计数不变 |
| AC-05 编辑器 zoom 不参与 | `savedEditorZoomAndUnrelatedStylesAreIgnored`；忽略保存 zoom、光标周边样式、其他语言与编辑器控件字体 |
| AC-06、AC-07 缓存和窗口隔离 | `tabsCacheAndWindowsKeepIndependentState`；四标签淘汰重建、手动无缓存、新窗口、分别重开和恢复 |
| AC-09 阅读位置与旧恢复失效 | `staleSnapshotAndUserScrollCancelOldLayout`、`consecutiveStylesAndDestroyedPreviewCancelOldWork`、`synchronizedFontChangeKeepsEditorInControl` |
| AC-10 失败与恢复 | `failureResetsBaseAndOnlyReopenRecovers`、`invalidTheme`、`fontFieldDecoding`、`malformedIniAndMissingThemeKey`、`unreadableSourcesUseDefaultAndCleanSnapshots` |
| AC-11 字体不可用、DPI、键盘访问 | `fontAvailabilityAndGlobalSavedValues`、恢复菜单动作检查；字体回退自动化通过，DPI、字体实际视觉与键盘宿主操作待用户验证 |
| AC-12 隐藏及旧快照导出 | `hiddenExportDoesNotReadConfiguration`、手动旧快照导出测试；沿用最新有效内容，不增加字体读取、不改变模式或 Z |
| AC-13、AC-16 只读与待保存数据 | `readonlySnapshotsAndPendingHostWrites`、`qtUserScopePathDoesNotCreateOrFlushFiles`；文件清单／哈希不变，正常和失败读取均不冲刷待写缓存，副本清理 |
| AC-14 全局覆盖 | 宿主生成样本及 `fontAvailabilityAndGlobalSavedValues`；读取最终 Markdown 值，不二次叠加 AllGlobal |
| AC-15 路径与主题回退 | `themeMapping` 18 组、`hostGeneratedFixtures` 8 份原始宿主对照、`missingFilesAndFields`、`userFileNeverMergesTemplate` |
| 补充：幂等与非法比例 | `invalidStyleInputDoesNotMutateDocument`；拒绝非有限／非正比例，重复应用不增加文档修订或解析 |

旧的保存字体探针只作为独立合成输入的来源。正式测试使用
[宿主配置样本](../tests/fixtures/saved-host-font/README.md)及其哈希／读回结果，运行时
不依赖本地宿主库、探针二进制或 build 目录。不可读源文件测试使用目录占据 INI 路径及
不可创建快照目录，覆盖文件读取和副本失败；真实 Windows 权限策略仍由用户核验。

## 本地环境与命令

Linux x86-64、GCC 13.3、Qt 5.15.13、C++14、Release、`BUILD_TESTING=ON`；使用已有临时
Qt 环境，不安装系统依赖。构建启用 `-Wall -Wextra -Wpedantic`，日志无编译警告。
`VERIFY_QT_PREFIX` 指向测试环境已有 Qt 前缀，不能将其本机路径加入产品或 CI。

```bash
cmake -S . -B build/req002 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH="$VERIFY_QT_PREFIX"
cmake --build build/req002 --parallel 4
LD_LIBRARY_PATH="$VERIFY_QT_PREFIX/lib/x86_64-linux-gnu" \
QT_QPA_PLATFORM_PLUGIN_PATH="$VERIFY_QT_PREFIX/lib/x86_64-linux-gnu/qt5/plugins" \
ctest --test-dir build/req002 --output-on-failure --no-tests=error
bash tests/release_publish_test.sh
git diff --check
```

首次直接运行 CTest 时遗漏临时 Qt 动态库路径，因 `libmd4c.so.0`／`libQt5Gui.so.5` 缺失
未进入测试。补齐上述环境后五组实际执行通过，不将加载失败记录成测试断言失败或隐藏该边界。

| 层次 | 状态 | 结果 |
| --- | --- | --- |
| 静态检查 | `passed` | 差异格式、文档链接、源配置只读边界；ABI／导出文件与基线无差异 |
| Linux Release 编译 | `passed` | 共享库与五组测试构建成功，无编译警告 |
| Qt 自动化回归 | `passed` | CTest 5/5；13+40+8+6+61＝128 passed，0 failed，0 skipped（包含初始化／清理） |
| 发布脚本隔离测试 | `passed` | mock gh，未访问真实 Release |
| ELF 加载与入口 | `passed` | ctypes 实际加载 x86-64 共享库并解析 NDD_PROC_IDENTIFY／NDD_PROC_MAIN |
| Qt 模拟截图 | `passed` | 原有 10 张状态图及新增 100%／125%／深色字体图；生成于 build/req002/screenshots |
| GitHub Windows Release 编译／测试 | `not run` | 等待推送本单候选提交并执行现有工作流 |
| Windows Artifact | `not run` | 等待对应候选构建，校验摘要、包内清单、DLL 架构及入口 |
| 真实 Windows notepad-- | `not verified` | 用户负责字体、路径、操作、滚轮手感和 DPI，未作为代码交付前置条件 |

本地日志位于 `build/req002/build.log`、`build/req002/markdownview_*_tests.txt` 和
`build/req002/Testing/Temporary/LastTest.log`。这些构建产物与生成截图不提交。

## 性能记录

在修改渲染行为前，将原提交导出到隔离源码目录，独立编译原版采样程序并运行 7 类样本，
每类预热一次、测量 20 次。候选与基线使用同一 Qt、固定窗口和样本生成方式；初测、反向
复测以及 `--layers` 拆分数据均保留。复测使用候选→基线顺序，以下单位为 ms，均为 P95。

| 样本 | 首次同步渲染 | 显式刷新 | HTML 导出 | 缓存接入及快照 | 20 次输入 |
| --- | --- | --- | --- | --- | --- |
| 100k | 3.614 → 3.780 | 135.994 → 42.290 | 1.374 → 1.449 | 55.543 → 1.841 | 0.127 → 0.128 |
| 1m | 32.363 → 29.664 | 951.527 → 318.748 | 9.443 → 9.387 | 336.787 → 16.674 | 0.123 → 0.122 |
| 5m | 77.820 → 77.701 | 5539.384 → 1524.599 | 42.258 → 43.499 | 2438.014 → 48.559 | 0.122 → 0.123 |
| tables-images | 25.784 → 28.079 | 502.990 → 118.826 | 4.447 → 4.710 | 199.933 → 4.935 | 0.128 → 0.132 |
| headings | 11.692 → 10.828 | 1116.340 → 66.038 | 4.511 → 4.979 | 570.231 → 5.220 | 0.132 → 0.127 |
| unsaved | 3.476 → 3.587 | 307.582 → 98.560 | 1.526 → 1.581 | 175.005 → 2.292 | 0.292 → 0.288 |
| two-windows | 3.435 → 3.534 | 131.096 → 41.444 | 1.359 → 1.427 | 54.273 → 1.717 | 0.125 → 0.126 |

初测发现显示控件后再应用字体造成额外布局，首次同步渲染曾增长约 12%～77%；改为显示前
完成样式后按相反顺序复测，表中首次渲染增幅最大约 8.9%，峰值 RSS 最大增长约 2.7%。
显式刷新 P95 降低约 66%～94%，缓存接入及快照降低约 95%～99%；批量格式修改和未变化
缓存跳过遍历带来这些同机模拟结果，不能用来承诺真实宿主的固定时延。

完整中位数、P95、峰值 RSS、样本规模和初测数据保存在
[req002-2026-09-12.json](measurements/req002-2026-09-12.json)。大型样本与原始诊断日志在
`build/req002/performance/`，不提交。标题密集样本的导出复测与样式拆分记录见后续补充。
