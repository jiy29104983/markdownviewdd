# 回归测试与手工验证

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
| `markdownview_lifecycle_tests` | Dock／预览销毁顺序、链接与锚点、相对图片导出、主题格式和位置 | notepad-- 真实控件销毁顺序、系统 URL 打开器、真实主题视觉 |
| `markdownview_document_identity_tests` | 快速切换、防抖、路径变化、隐藏 Dock 导出、原子保存、滚动恢复、大文档手工刷新 | 真实宿主 ABI、真实磁盘对话框、设备性能结论 |
| `markdownview_multi_window_tests` | 每窗口控制器、重复初始化、窗口关闭和右键菜单隔离 | notepad-- 多进程或非基线窗口结构 |
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

失败时保存 `%TEMP%\markdownview-<进程号>.log` 及适用的 `.1`～`.3` 轮转文件，并分别标注静态检查、自动化测试、Windows Release
编译、Artifact 校验和真实宿主测试为 `passed`、`blocked`、`not run` 或 `not verified`。
