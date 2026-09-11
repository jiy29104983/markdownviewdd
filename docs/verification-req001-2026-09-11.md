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
| ELF 与入口符号 | `passed` | ELF x86-64，包含 `NDD_PROC_IDENTIFY` 和 `NDD_PROC_MAIN` |
| Qt 模拟界面截图 | `passed` | 10 张实际 Qt/offscreen 状态图，写入 `build/req001/screenshots`，检查窄侧栏的文字及按钮 |
| Windows Release 编译／Artifact | `not run` | 本地阶段不替代后续 GitHub Windows 构建，云端证据完成后补记 |
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
