# REQ-007：附着复制图标调整（2026-09-15）

用户否定顶部代码块选择栏，要求像常见 Markdown 编辑器一样直接点击块上的小复制图标。
本轮移除选择栏和全局复制反馈区，每个可见代码块左上角显示淡色语言，右上角附着复制图标；
成功原位变勾号 2.5 秒，失败和旧快照说明通过提示及无障碍描述提供，按钮支持 Tab／空格。

## 实现与边界

- 复制仍使用已核对的展示快照，未修改源码提取、换行、剪贴板验证及跨文档校验规则。
- 图标为 Qt 原生绘制，不引入图片资源或第三方依赖。页头在正文首行上方保留 28 逻辑像素，
  图标命中区 24×24，不覆盖代码文字；横向滚动时图标保持在预览右边缘。
- 仅创建可见代码块的页头，离开视口即释放；纵向滚动立即隐藏旧命中目标，再合并定位。
- 文首代码适配 Qt 首段忽略上边距的行为。主题／字号／缓存接入不累加间距，改为普通段落
  后恢复根 frame 间距。字符坐标、标题和搜索目标不因新增字符而发生偏移。
- 导出在临时文档副本恢复页头原始间距，控件、语言装饰和页头留白不进入 HTML。
- Qt 原生预览不呈现的空围栏没有可附着的正文块，仍可在预览右键菜单复制。
- 宿主、ABI、版本号、Qt 依赖和发布工作流不变。

## 本地验证

环境为 Linux aarch64、GCC 13.3.0、Qt 5.15.13、Release、C++14。
构建与验证入口：

```bash
cmake --build build/req007/test --parallel 4
MARKDOWNVIEW_SCREENSHOT_DIR="$PWD/build/req007-icons/screenshots" \
  ctest --test-dir build/req007/test -C Release --output-on-failure --no-tests=error
bash tests/release_publish_test.sh
```

新增／更新的代码组用例覆盖：直接点击不同块图标、Tab／空格复制、空块右键入口、图标位于
首行上方、窗口调宽、主题字号、根间距恢复、旧按钮失效、100 块仅创建可见控件、滚动后复制
第 100 块及回收屏外控件。原有快照、文本保真、失败、销毁及导出回归继续执行。

日志：`build/req007-icons/build-verified.log`、`ctest-verified.log`、`release-tests.log`，
以及 `build/req007/test/markdownview_*_tests.txt`。

## 性能复核

[原始测量](measurements/req007-icons-2026-09-15.json) 保留基线、首次整篇创建页头以及优化为
仅创建可见页头后的 20 次采样。100 块的同步路径 P95（毫秒）：

| 路径 | 原选择栏 | 整篇创建页头 | 仅可见页头 |
| --- | --- | --- | --- |
| 首次渲染 | 7.565 | 11.525 | 7.877 |
| 显式刷新 | 37.492 | 43.207 | 39.512 |
| 缓存接入并取得 HTML | 3.504 | 7.686 | 3.993 |
| HTML 导出 | 2.452 | 3.532 | 3.561 |

进程峰值 RSS 从 31644 KiB，经首次实现的 33576 KiB，降到 31788 KiB。触发复核线后，
通过只创建可见控件消除了整篇控件的开销。导出保留约 1.1 ms 的文档复制成本，以可靠移除
显示间距；缓存接入指标包含 HTML 获取，也有相应成本。三者 HTML 输出大小均为 67373 字节。
这不是所有路径无退化的声明；16 ms 延迟创建／定位及真实宿主布局成本不完全包含在同步
指标里，真实 Windows 设备性能仍需用户验证。

## 验证层次与证据位置

本节是提交前的本地验证记录；该提交的云端构建、Artifact 和运行链接在任务最终回复中
单列，并可从 GitHub Actions 查询。

| 层次 | 状态 | 说明 |
| --- | --- | --- |
| 静态检查 | passed | `git diff --check`、Markdown 链接、接口与依赖检查 |
| Linux Release 编译 | passed | `-Wall -Wextra -Wpedantic`，无新增警告 |
| 自动化测试 | passed | 八组 CTest，241 passed／0 failed，代码组 37 passed；53.66 秒；发布脚本隔离测试通过 |
| Windows Release 编译 | not run | 本地无 Windows 工具链，推送后运行现有工作流 |
| Artifact 校验 | not run | 等待此提交的工作流产物 |
| 真实宿主测试 | not verified | 用户执行 notepad-- v3.8.3 x64 加载、DPI 与键鼠验证 |

截图通过统一环境变量生成：`req007-code.png`（整体）、`req007-code-inline.png`（侧栏）、
`req007-code-copied.png`（成功）、`req007-code-stale.png`（旧快照）、`req007-code-dark.png`。
Linux 缺少中文字库，截图中文缺字；Windows 工作流同样生成截图并上传诊断 Artifact。
两者均为模拟宿主截图，不替代真实宿主验收。
