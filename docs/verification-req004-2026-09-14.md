# REQ-004 预览内搜索验证记录

日期：2026-09-14。实现起点：`e9828ba6762d7d005e80685be74f0dfdc61ba5be`。

## 行为与实现

新增预览工具栏“查找”及窗口独立的 `PreviewSearch`。支持单行字面查询、大小写开关、
精确命中计数、当前项／其他项临时高亮、前后环绕与快捷键。输入时保持阅读位置；
只有用户执行上一处／下一处才导航。搜索仅使用当前展示快照，手动待刷新时明确提示，
不增加源码读取、全文渲染或源码跳转。导出与正文格式保持不变。

- 匹配不重叠，不跨段落／表格单元格，同段可跨富文本格式。默认不区分大小写。
- 关闭搜索清空查询和大小写状态，隐藏 Dock 保留查询并暂停任务；重开后校验快照。
- 刷新后用段内前后各 24 字符上下文及距离恢复当前项，全部坐标重新计算；不能对应则选首项。
- 75 ms 防抖、每块最多读取 4096 字符、单批目标 4 ms／65536 字符、批间间隔 5 ms。
  完整位置索引空间随结果数增长，同时高亮至多 256 项；无结果截断，计算中明确显示进度。
- 大纲与搜索共用 `navigatePreviewPosition()` 和延迟布局仲裁，搜索取消旧的大纲源码目标。
- 查询代次、文档身份、展示版本、文档修订号及 QPointer 防止过期回调。
  正文替换期间同步失效并延后清理高亮；文档销毁则在私有数据仍存活时同步清理旧选区。
  格式后处理单独维护修订号，高亮更新有重入保护。

主要改动：`src/preview_search.*`、Dock、控制器、CMake／qmake、测试及文档。
不修改宿主、插件 ABI 或版本，不新增运行时依赖。Qt 大小写折叠与输入框上限见
[架构说明](architecture.md)。需求和共同索引仍按仓库约定保留为本地忽略文件。

## 分层结果

| 层次 | 结果 | 证据与边界 |
| --- | --- | --- |
| 静态检查 | passed | 改动范围、构建接入、ABI／宿主无改动、Markdown 链接及 git diff --check |
| Linux Release 编译 | passed | GCC 13.3、Qt 5.15.13、C++14、无新增编译警告 |
| Linux Qt 回归 | passed | 七组 CTest，194 项 QtTest，0 失败／跳过；约 44 秒 |
| 发布脚本隔离测试 | passed | mock gh，未操作真实 Release |
| 地址检查 | passed | RelWithDebInfo + AddressSanitizer；文档替换／销毁与刷新上下文恢复两个用例；关闭 Qt 平台泄漏统计 |
| GitHub Windows Release／回归 | not run | 等待本次提交推送后的工作流验证 |
| Windows Artifact | not run | 等待工作流产物下载及校验 |
| 真实 Windows notepad-- | not verified | 用户负责加载、输入法、键鼠、主题／DPI 和设备性能验证 |

QtTest 计数：lifecycle 13、document identity 40、multi-window 8、diagnostics 6、
font 62、outline 30、search 35。地址检查单独记录为两个用例加 init／cleanup，共 4 passed，
不重复计入 194。日志位于 `build/req004/markdownview_*_tests.txt`、`ctest.log`、`build.log`、
`release-tests.log`、`asan-fixed.log`。Qt offscreen 平台提示不代表真机兼容性。

## 验收覆盖

| 验收项 | 自动化证据 |
| --- | --- |
| AC-01 | 中文、标点、大小写、Emoji、空白、非重叠、KMP 前缀／分片边界；标题／正文／表格／代码及跨格式 |
| AC-02 | 首尾环绕提示、当前项可见、原选区不变、搜索不触发源码定位 |
| AC-03 | 空查询、无结果、多行粘贴和第一行为空；关闭清理与默认状态恢复 |
| AC-04 | 分批中换查询／文档／版本、换标签、隐藏重开、正文替换、文档销毁、窗口销毁、部分刷新失败 |
| AC-05 | 搜索前后 QTextDocument HTML 与导出字节完全相同；保留非搜索显示选区 |
| AC-06 | 手动源码修改后结果仍来自旧快照、提示待刷新、宿主渲染次数不增加 |
| AC-07 | 编辑器 Ctrl+F 触发宿主快捷键；预览／搜索栏触发插件；模拟 IME 组合、Enter／Shift+Enter／Esc 和退出焦点 |
| AC-08 | 100 KiB／1 MiB／5 MiB 精确命中数、256 项高亮上限、计数提示、事件心跳和分批／总体耗时 |

这些结果证明插件侧契约。真实 Windows 操作按 [手工步骤](testing.md) 验证，
尤其是输入法候选框、真实焦点、滚动手感、主题和不同缩放。

## 搜索测量与截图

500 段落预热一次、采样 20 次：含防抖的搜索中位数 84.930 ms、P95 85.199 ms，
批处理 P95 0.980 ms。以下长样本为同次 Linux 模拟测试的单次规模验证，不冒充 20 次统计：

| 纯文本规模 | 命中数 | 完整搜索耗时 | 最大匹配批次 | 最大事件心跳间隔 |
| --- | --- | --- | --- | --- |
| 100 KiB | 12,800 | 165.280 ms | 4.012 ms | 15.450 ms |
| 1 MiB | 131,072 | 1132.600 ms | 4.014 ms | 117.439 ms |
| 5 MiB | 655,360 | 6783.970 ms | 4.367 ms | 101.784 ms |

4 ms 是匹配循环让出目标，不包含 Qt 布局和高亮更新；上表的事件间隔记录这些额外成本，
不能只用匹配批次宣称所有 UI 操作都在 4 ms 内。大样本首轮仍可能因 Qt 布局出现约百毫秒间隔。
正式设备的输入响应与内存峰值留待用户测量。结果索引不截断，完成后总数精确且全部可导航。

已检查 Qt 模拟截图 `build/req004/screenshots/req004-search.png` 和
`req004-search-stale.png`，覆盖普通／当前命中、完整计数及高亮范围提示、待刷新快照。
源码中提供 `longDocument` 和 `measurementsAndScreenshots` 可复现生成／采样方式。

## 本地复现

```bash
cmake -S . -B build/req004 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH="$VERIFY_QT_PREFIX"
cmake --build build/req004 --parallel
ctest --test-dir build/req004 --output-on-failure --no-tests=error
bash tests/release_publish_test.sh
```

Windows 统一入口仍为 `scripts/run-regression-tests.ps1`，使用 Qt 5.15.2、MSVC v142、x64。
GitHub 工作流自动发现第七组 CTest 并上传原始测试输出及截图。
