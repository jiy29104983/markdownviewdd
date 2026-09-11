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
并显示这些文件，避免 Windows 下标准输出为空时只看到退出码。云端一次执行全部四组 Qt 测试，
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
