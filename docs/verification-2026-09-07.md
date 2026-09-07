# 复核问题修复与临时环境验证（2026-09-07）

本次基于 `dc066e5` 修复二次审查指出的 7 个问题。原有 BUG-001～BUG-016 的交回记录
属于此前提交的历史证据，本记录补充本次修改及实际编译、测试结果，不代表已经完成
Windows 真实宿主验证。

## 修复对应关系

| 复核问题 | 修改结果 | 验证证据 |
| --- | --- | --- |
| 非成员导出函数非法使用 `this` | 显式传入 `const QObject *context`，保留窗口日志归属 | 插件及测试目标编译通过；HTML 图片快照测试通过 |
| 生命周期测试缺少 `QTextBlock` 定义 | 补充必要头文件 | 生命周期测试目标编译通过 |
| 反向同步屏蔽宿主滚动信号 | 保留滚动条 `valueChanged`，仅抑制插件自己的反馈回调 | `reverseScrollReachesHostReceiver` 同时验证滚动条值和宿主接收者值 |
| 后台文档修改后错误复用缓存 | 持续观察已接入编辑器的文本、路径和销毁；后台变化只使缓存失效 | `backgroundEditInvalidatesCachedPreview`、`backgroundPathChangeInvalidatesCachedPreview` |
| 用户滚动后又恢复到旧位置 | 用户输入取消旧恢复目标；主题异步恢复核对交互代次 | `userScrollCancelsOldReadingPosition`；原有位置恢复与主题测试 |
| 标签切换丢失慢文档策略 | 按编辑器保存完整渲染耗时，缓存命中不覆盖测量 | `slowDocumentPolicySurvivesTabSwitch`，同时验证普通文档自动刷新及真实快速渲染后的策略恢复 |
| 多窗口测试固定断言两个计时器 | 比较重复初始化前后的同一组计时器对象 | `repeatedInitializationIsIdempotent` |

同时修正原回归测试中的三个模拟问题：拖选事件显式携带左键按住状态；标题格式读取实际
文本片段而非段落标记；滚动／主题／缓存重建测试使用真实长文档和比例断言，延迟布局通过
追加文档内容触发，避免人为滚动范围被 Qt 自身布局重算后导致误判。

实现边界与行为说明见 [architecture.md](architecture.md)，测试入口和真实宿主手工步骤
见 [testing.md](testing.md)。本次没有修改宿主参考源码、插件 ABI 或发布工作流。

## 实际验证环境

- Linux x86-64，Ubuntu 24.04。
- GCC 13.3.0，C++14，`-Wall -Wextra -Wpedantic`。
- Qt 5.15.13，使用下载后解压到临时目录的开发及运行包，没有安装系统 Qt。
- 原仓库 `CMakeLists.txt`，`CMAKE_BUILD_TYPE=Release`、`BUILD_TESTING=ON`。
- Qt Test 使用 `offscreen` 平台。

本次编译直接使用修改后的仓库源码，不再使用上轮为绕过编译错误准备的临时源码副本。
构建目录和工具链位于临时目录，未将本机 Qt 路径或生成产物写入仓库配置。

## 验证结果

| 层次 | 结果 | 说明 |
| --- | --- | --- |
| 静态检查 | `passed` | `git diff --check`；新增／修改文档的本地链接检查 |
| 临时环境 Release 配置与编译 | `passed` | 插件共享库与四个 Qt Test 可执行程序均完成编译、链接；未出现编译警告 |
| CTest | `passed` | 4/4 组通过；42 个行为用例，计入各组初始化／清理共 50 项通过，0 失败、0 跳过 |
| 发布脚本隔离测试 | `passed` | `bash tests/release_publish_test.sh`；使用 mock `gh`，未访问或修改真实 Release |
| 临时共享库格式与导出符号 | `passed` | ELF x86-64；包含 `NDD_PROC_IDENTIFY` 和 `NDD_PROC_MAIN` |
| Windows Release DLL 编译 | `not run` | 临时 Linux 共享库不能替代 MSVC v142、Qt 5.15.2 的 Windows 构建 |
| Windows Artifact 校验 | `not run` | 未生成或下载 Windows ZIP／DLL／SHA256 Artifact |
| notepad-- 真实宿主测试 | `not verified` | 未运行 Windows 宿主加载、实际滚动、主题与多窗口手工测试 |

CTest 各组结果：生命周期 11 个行为用例、文档状态 22 个、多窗口 5 个、诊断日志 4 个。
测试中 offscreen 的窗口能力提示及最小注入适配器使用普通 QWidget 时缺少 `textChanged`
的提示，不表示真实宿主测试通过；实际宿主约束仍以兼容性文档为准。

## 临时环境复核命令

设置 `VERIFY_QT_PREFIX` 为临时 Qt 的前缀，`VERIFY_BUILD_DIR` 为独立构建目录；
运行时另配置临时 Qt 的库和平台插件搜索路径。命令主体为：

```bash
cmake -S . -B "$VERIFY_BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH="$VERIFY_QT_PREFIX"
cmake --build "$VERIFY_BUILD_DIR" --parallel 4
ctest --test-dir "$VERIFY_BUILD_DIR" --output-on-failure --parallel 4
bash tests/release_publish_test.sh
git diff --check
```

后续发布前仍需使用 [releasing.md](releasing.md) 中的 Windows 构建与验证流程，
本记录不将任一原始单据自动标记为真实宿主验证完成。
