# REQ-001 刷新模式与状态反馈：验证记录

本单在 `31ce757` 基线上实现，依据已确认的窗口模式、失败旧快照保留、保护资格与同步状态
分离、旧快照暂停双向滚动等约定。功能行为与内部状态转换见 [architecture.md](architecture.md)，
复现步骤见 [testing.md](testing.md)。本次未修改宿主源码、插件 ABI 或版本号。

## 验收对应

| 验收项 | 自动化证据 |
| --- | --- |
| AC-01 手动连续输入 | `manualModeDoesNotRenderOnShowEditOrReopen`，20 次编辑并等待超过 2 秒，渲染次数为 0 |
| AC-02 手动 A→B→A | `manualTabSwitchUsesOnlyMatchingSnapshot`，B 无正文，A 保留原版本，后台编辑不渲染 |
| AC-03 自动合并输入与入口一致 | `modeChangesCancelQueuedWorkAndResumeDebounce`，最终 B 只渲染一次，菜单与工具栏一致 |
| AC-04 保护按文档隔离 | `protectedDocumentKeepsModeAndFreshnessSeparate`、`slowDocumentPolicySurvivesTabSwitch` |
| AC-05 显式刷新、隐藏导出、失败重试 | `manualHiddenExportUsesCurrentTab`、`failedRefreshRetainsOnlyUntouchedSnapshot` 两组数据、`failedInitialRenderProvidesRetry` |
| AC-06 过期回调 | `modeChangesCancelQueuedWorkAndResumeDebounce`、`renderReentrancyCannotPublishChangedSource`、`renderTabSwitchAndModeChangeDiscardObsoleteResult` |
| AC-07 多窗口模式隔离 | `refreshModesAndContextMenusAreWindowLocal` |
| 补充边界 | 双向滚动门控、缓存淘汰、路径／类型变更、控件销毁、自动首次打开大文件维持既有策略 |

## 本地验证

Linux x86-64、GCC 13.3、Qt 5.15.13、C++14、Release、`BUILD_TESTING=ON`，使用已有临时 Qt，
未安装系统依赖。普通构建启用 `-Wall -Wextra -Wpedantic`；构建目录为忽略目录 `build/req001`。

| 层次 | 状态 | 证据 |
| --- | --- | --- |
| 静态检查 | `passed` | `git diff --check`；改动文档链接；宿主 ABI 和导出函数文件无差异 |
| Linux Release 编译、链接 | `passed` | 共享库及四组测试可执行程序构建成功，无编译警告 |
| Qt 动态回归 | `passed` | CTest 4/4；QtTest 67 项通过，0 失败、0 跳过，含各组初始化／清理及截图用例 |
| 发布脚本隔离测试 | `passed` | `bash tests/release_publish_test.sh`，mock `gh`，不访问真实 Release |
| ELF 动态加载与入口符号 | `passed` | ELF x86-64；`ctypes.CDLL` 实际加载共享库并解析 `NDD_PROC_IDENTIFY` 和 `NDD_PROC_MAIN` |
| Qt 模拟界面截图 | `passed` | 10 张实际 Qt/offscreen 状态图，写入 `build/req001/screenshots`，检查窄侧栏的文字及按钮 |
| Windows Release 编译 | `passed` | Actions `34559826637`，Qt 5.15.2／MSVC v142／x64，构建与回归全部成功 |
| Windows Artifact 校验 | `passed` | Artifact digest、包内 SHA256、文件清单、PE32+ x86-64 及两个导出入口全部通过 |
| notepad-- 真实宿主 | `not verified` | 尚未在真实 Windows/notepad-- 中加载 DLL 和执行手工操作 |

四组 QtTest 结果分别为：生命周期 13 项、文档与状态 40 项、多窗口 8 项、诊断 6 项。
本单新增 17 项结果（含参数化失败场景及截图用例），继承用例均在本次重新执行。

## 性能采样边界

改动前源码独立编译保留于 `build/req001-baseline`；可选 `tests/benchmark` 对原提交源码与候选
源码使用同一模拟宿主采样。原始 JSON 和诊断写入 `build/req001/performance`，采样 20 次并记录
中位数、P95、源码大小及行数；初步短段落密集样本单独保留，不与后续不同内容的样本直接比较。

这些数据只能说明同机模拟边界内的变化。真实宿主读取成本、同步调用之后的异步布局、进程冷
启动及 Windows 性能测量仍为 `not verified`，不能用本地 Qt 数据承诺真实输入延迟或所有大文档
不卡顿。完整采样说明与命令见 [testing.md](testing.md)。

## GitHub Windows 构建与产物

功能提交：`9d8537904d4f5499ffa7c3a4dcfb920b23564915`。
2026-09-11 推送 `main` 后触发 Actions **34559826637**，最终 `completed / success`。
构建使用 Windows 2022、Qt 5.15.2、Visual Studio 2022 的 MSVC v142 x64 工具链。
Windows Release DLL、四组 Qt 回归、发布脚本隔离测试、打包与上传均成功；标签发布 job
按普通分支构建规则跳过，本次没有创建版本标签或发布 Release。

Windows 诊断 Artifact `test-diagnostics-34559826637-1` 中的四组 QtTest 结果与本地一致：
**13 + 40 + 8 + 6 = 67 passed，0 failed，0 skipped**，另含 10 张 Windows Qt/offscreen
状态截图。模拟截图已经检查；它们不能证明 notepad-- 中的真实加载、输入或滚动。

候选 Artifact：`markdownviewdd-v0.2.8-windows-x64-9d85379`，ID `10183937502`。
从公开下载入口取得产物后验证：

- 下载 Artifact ZIP 的 SHA256 与 GitHub 元数据 digest 一致：
  `7456c06603eeeff97ec7ec8c2b1190ab36fb822dc5bbdaef44f06c31bc610c70`。
- 内部发布格式 ZIP 与配套 `.sha256` 一致：
  `9c65377b1c6867e1694b97c014fba942be9ea81db011b5747e50a721a39f3da2`。
- 内部 ZIP 仅有 `plugin/markdownviewdd.dll`、`README.md`、`LICENSE`。
- DLL 大小 **182784 字节**，PE32+ x86-64，导出 `NDD_PROC_IDENTIFY` 与 `NDD_PROC_MAIN`。
- DLL SHA256：`52747a88b88dc67fa2ed86875ac9b8fba7b80be8630a9b9e852b5de734b0c0e9`。
- 依赖为 Qt5 Core／Gui／Widgets、MSVC 运行库和 Windows 系统库，未新增渲染后端依赖。

本地下载与解包路径为 `build/req001/windows/`；该目录以及生成截图和 DLL 均不提交。
Artifact 按工作流保留 14 天，后续可在同一功能提交重新执行普通构建。

## 同机模拟采样结果

以下单位为 ms，单元格为“原提交 → 功能提交”的 P95，各样本预热一次后采样 20 次。

| 样本 | 首次同步渲染 | 显式同步刷新 | 最新 HTML 导出 | 缓存标签接入及快照 | 20 次输入 |
| --- | --- | --- | --- | --- | --- |
| 100 KiB 纯文本 | 3.512 → 3.503 | 137.400 → 137.804 | 1.452 → 1.394 | 58.686 → 54.354 | 0.088 → 0.133 |
| 1 MiB 纯文本 | 32.432 → 32.838 | 969.496 → 954.268 | 9.421 → 9.092 | 332.134 → 341.267 | 0.529 → 0.139 |
| 5 MiB 纯文本 | 78.480 → 83.206 | 5376.628 → 5525.363 | 44.349 → 42.992 | 2310.155 → 2459.966 | 0.518 → 0.138 |
| 100 组图片／表格 | 25.866 → 26.995 | 490.653 → 493.857 | 4.426 → 4.286 | 204.706 → 197.543 | 0.095 → 0.128 |
| 500 标题 | 11.613 → 11.482 | 1108.434 → 1130.201 | 4.796 → 4.669 | 568.068 → 597.300 | 0.094 → 0.137 |
| 未保存增长 | 3.430 → 3.465 | 320.857 → 308.331 | 1.508 → 1.542 | 171.316 → 174.220 | 0.273 → 0.276 |
| 双窗口 | 3.452 → 3.436 | 131.202 → 133.256 | 1.344 → 1.417 | 54.896 → 56.727 | 0.088 → 0.123 |

过程峰值 RSS（KiB，原提交 → 功能提交）：100 KiB 纯文本 33244 → 34256；1 MiB 纯文本 51228 → 52604；5 MiB 纯文本 115380 → 115864；100 组图片／表格 33532 → 34644；500 标题 33872 → 35140；未保存增长 34720 → 35360；双窗口 36472 → 37532。

首次同步渲染、显式刷新、导出及缓存接入 P95 的最大增幅为 6.5%，进程峰值 RSS 最大增幅为 3.7%；双窗口第二窗口创建及首次渲染为 9.015 → 9.874 ms（+9.5%）。

普通已保存文档的 20 次输入 P95 增加约 0.033～0.045 ms，百分比超过 10%，因此对
100 KiB、500 标题和双窗口样本进行复测。100 KiB 复测为 0.082 → 0.129 ms，500 标题
为 0.097 → 0.127 ms，双窗口反向顺序复测为 0.082 → 0.133 ms。稳定增加约每次输入
1.5～2.6 μs，保留该成本以满足即时状态反馈和版本通知；没有新增全文读取或逐键渲染。
受保护文件的每次输入日志改为保护状态转换时记录，1 MiB／5 MiB 样本输入突发反而更低。

双窗口首次复测的显式刷新出现 +16.0%、缓存接入 +15.3%，再次按候选→基线顺序测量后分别
为 131.929 → 131.937 ms（约 +0.0%）、55.195 → 56.041 ms（+1.5%），第二窗口创建
为 10.081 → 10.133 ms（+0.5%）。这一轮未重现前次较大退化，原始波动仍保留；不据此
承诺固定延迟。图片样本初版含 CRC 错误，已改为 Qt 现场生成 PNG，表格中的图片／表格结果
来自修正后的双方重测，且日志确认无 PNG 解码错误。

初步 100 KiB 短段落密集样本（与上表内容不同）在原提交上显式刷新 P95 为 4026.740 ms，缓存接入及快照为 2279.537 ms，说明块数量和富文本布局会显著影响现有同步路径。此记录仅作为既有性能风险证据，不能与上表长段落样本直接计算改进比例。后续性能治理需求应保留短段落／结构密集测量，不将本单描述为解决全部大文档卡顿。

完整中位数、P95、样本字节数／行数、峰值 RSS 和复测数据保存在
[req001-2026-09-11.json](measurements/req001-2026-09-11.json)，可用随仓库提交的
`tests/benchmark` 重现。CPU 硬件、真实 Windows/notepad-- 测量及异步布局的不足仍按前述
边界保留，模拟数据不能作为真实宿主性能验收。
