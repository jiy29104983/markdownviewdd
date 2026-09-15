# REQ-007：代码块阅读与复制验证

## 改动与已确认行为

- 新增代码块索引和文档外操作栏，复用宿主源码读取、展示版本、预览导航及样式路径。
- 围栏／缩进／常见列表与引用代码保留有效空白、Tab、特殊字符及内容终止换行。
  Qt 行首 Tab 展开与源码复制数据分离；缩进边界穿过 Tab 时仅结构部分转为空格。
- 每块语言和复制入口在选择栏可通过键盘访问；代码内上下文菜单可直接复制当前块。
  Qt 未显示的空围栏通过选择栏操作。反馈不改变正文尺寸，控件不混入 HTML。
- 默认视觉换行，可切换保持长行；用户级专属偏好跨重启恢复，已有窗口独立。
- 旧快照复制不读取最新源码；复制前即时检查活动宿主标签；失效、剪贴板失败、菜单及
  剪贴板回调重入、缓存淘汰与销毁均有回归。
- 新增依赖：无。宿主目录、插件 ABI、版本号与发布工作流未修改。

主要实现：`src/code_block_index.*`、`src/code_block_tools.*`、
`src/preview_controller.*`、`src/markdown_preview_dock.*`；构建清单覆盖 CMake 和 qmake。
样例、架构、测试说明及需求确认同步更新。

## 分层结果

| 层次 | 状态 | 证据与边界 |
| --- | --- | --- |
| 静态检查 | passed | `git diff --check`、改动 Markdown 相对链接、接口与依赖审查；无 ABI／宿主改动 |
| Linux Release 编译 | passed | Linux aarch64，GCC 13.3.0，Qt 5.15.13，C++14；警告选项 `-Wall -Wextra -Wpedantic`，无新增编译警告 |
| 自动化测试 | passed | 八组 CTest 全部通过，QtTest 合计 239 passed／0 failed，代码组 35 passed；隔离发布脚本通过，日志见下文 |
| Windows Release 编译 | passed | 提交 `6a3f2a2`，GitHub Windows 2022／VS 2022 + MSVC v142／Qt 5.15.2／x64，Release 编译通过 |
| Artifact 校验 | passed | 下述代码提交产物 SHA256、ZIP 文件清单、PE32+ x64 和导入依赖已验证 |
| 真实宿主测试 | not verified | 由用户在 notepad-- v3.8.3 x64 完成，不以 Linux／模拟测试替代 |

代码组以固定预期字符串验证：普通／未知语言、空块、尾部换行、空白行中的空格、Tab、
相邻与内嵌围栏、引用／列表／缩进代码、换行规范化、键盘／菜单、手动旧快照、缓存、
标签事件尚未处理、读取／剪贴板重入、部分渲染失败、销毁、搜索、原生缩放、主题、HTML
及窗口偏好。既有七组回归全部保留。

## 本地复现与日志

```bash
cmake -S . -B build/req007/test -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build/req007/test --parallel 4
MARKDOWNVIEW_SCREENSHOT_DIR="$PWD/build/req007/screenshots" \
  ctest --test-dir build/req007/test -C Release --output-on-failure --no-tests=error
bash tests/release_publish_test.sh
```

本次完整回归日志：`build/req007/ctest-verified.log`，八组通过，52.99 秒。
代码组最终增补用例日志：`build/req007/code-verified.log` 及
`build/req007/test/markdownview_code_tests.txt`。
隔离发布测试：`build/req007/release-tests.log`，`release publishing tests: passed`。
编译日志：`build/req007/build-verified.log`。

模拟截图：`build/req007/screenshots/req007-code.png`、`req007-code-stale.png`。
Linux 仅有西文字库，中文显示为缺字框；截图仅记录控件布局，中文内容由断言验证。
Windows 工作流同样生成截图，真实宿主视觉仍需用户验证。

## 性能与限制

首次改动前保留基线提交 `30a52d9` 的独立基准可执行文件；同机候选对照包含 100 KiB、
1 MiB、5 MiB、图片／表格、500 标题、未保存增长与双窗口，预热后每项 20 次。
基准程序为 `tests/benchmark`；现有模拟编辑器没有可靠源码无障碍能力，因此该对照
覆盖渲染／样式与能力不可用路径，不能证明真实宿主源码读取或成功提取的成本。
成功索引另用 100 个代码块、预热一次后 20 次测量，结果写入代码测试日志。
原生异步布局、真实 Windows 峰值内存和设备手感不在这些同步耗时结论内。

复杂源码（例如原始 HTML 块或无法确认的容器）会保守禁用该快照的代码复制。
保持长行采用整个预览的水平滚动范围，未提供代码块内部独立滚动；普通段落、表格和
选区有自动化检查，DPI／键鼠手感仍以真实宿主步骤为准。

初轮完整原始对照见 [测量数据](measurements/req007-2026-09-15.json)。100 块成功索引
中位数 2.601 ms、P95 2.621 ms。连续 20 次模拟编辑在部分样本增加约 0.02～0.07 ms，
相对比例超过 10% 复核线；已去除代码栏模式／新旧状态未变时的重复控件更新，并完成
全部自动化回归。微秒级路径的对照不作为“无退化”承诺，复测原始值也保留在同一测量文件中。

复测连续 20 次模拟编辑的 P95（毫秒）：

| 样本 | 基线 | 候选 | 变化 |
| --- | --- | --- | --- |
| 1m | 0.2148 | 0.2391 | +11.3% |
| 5m | 0.2286 | 0.1987 | -13.1% |
| tables-images | 0.2050 | 0.2203 | +7.5% |
| unsaved | 0.3862 | 0.3055 | -20.9% |

重复状态更新已移除；该路径仍有新增身份／模式检查，微秒级绝对差值与事件调度噪声
无法由 20 次模拟样本分离。保留超过复核线的数据，不宣称所有路径无退化；自动化计数
确认复制和显示设置不增加源码读取或渲染，真实设备性能仍待用户验证。

## GitHub Windows 与 Artifact 证据

代码提交 `6a3f2a2566072fb577c94143d63d3efbd96a05f7` 对应
[Windows build and release #34928056118](https://github.com/jiy29104983/markdownviewdd/actions/runs/34928056118)
已完成且结论为 `success`。Release DLL、Qt 回归、隔离发布脚本、诊断上传和打包全部成功。
Windows QtTest 与 Linux 一致：八组共 239 passed／0 failed，代码组 35 passed。
Release 发布 job 在普通 main 推送中按工作流条件跳过。

下载的 Artifact 外层 ZIP 先与 GitHub API 的 SHA256 digest 核对；包内校验文件再验证
`markdownviewdd-v0.2.8-windows-x64-6a3f2a2.zip`，SHA256 为：

```text
e5cacbbc4939a0d6d7519d7206c3c59fc1f09276ceb9e02e1724a47f98d0234c
```

ZIP 文件清单仅 `plugin/markdownviewdd.dll`、`README.md`、`LICENSE`。
DLL 独立解析 PE 头确认为 PE32+／machine `0x8664`，其 SHA256 为：

```text
41b3bbe520e113b7151056a520d081fde96bda48f7a2c3634d2450a8387567f9
```

导入依赖：Qt5Widgets、Qt5Gui、Qt5Core、MSVCP140、VCRUNTIME140、VCRUNTIME140_1、
Windows CRT runtime／heap API 和 KERNEL32，未引入 QScintilla 或其他新增运行库。
下载与校验文件位于 `build/req007/artifacts-34928056118/`。其中 `req007-code.png`、
`req007-code-stale.png` 为 Windows offscreen 截图，中文正确显示；它们证明模拟控件
呈现，不代表 notepad-- 真机加载、DPI 和键鼠验收。

此记录引用实际已验证的代码提交。后续仅补充使用说明和本记录的提交仍须完成其对应
工作流；最终任务回复提供该提交及最新运行链接。
