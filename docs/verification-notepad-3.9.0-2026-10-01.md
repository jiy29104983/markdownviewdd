# markdownviewdd 对 Notepad-- 3.9.0 的兼容性报告

核验日期：2026 年 10 月 1 日。对象为修复前的 markdownviewdd v0.2.9 发布源码，以及官方
Notepad-- 3.9.0 Windows x64 便携版中的原生 DLL 插件环境。

后续补查 3.8.3 发布 EXE，确认两版均为同一套 13 项主题表。本文发现的缺陷也影响
3.8.3 发布包，并非 3.9.0 独有的变化；旧 18 项表仅来自公开源码。当前源码已修复，
详见[双版本适配记录](verification-dual-host-2026-10-01.md)。以下保留修复前证据。

## 结论

**修复前的 v0.2.9 不能认定为完整兼容 Notepad-- 3.9.0。** 发布 DLL 的架构、Qt／MSVC
依赖及关键 Markdown 槽函数均满足静态检查；现有回归和源码导航控件探针通过。但
“跟随宿主已保存的 Markdown 字体”存在一项已复现的兼容性缺陷：插件沿用旧主题
编号表，3.9.0 的六个主题会读取错误配置或回退到默认字号。

核心预览的静态前置条件通过，实际 Windows 加载、刷新、窗口生命周期及交互仍为
`not verified`。用户本次明确回复“尚未测试”；执行环境为 Linux aarch64，没有
Windows 或 Wine 运行环境。本报告不把静态检查或 Linux 测试等同于 Windows 实测。

建议先修复主题映射，再完成本文末尾的实机验证，之后再决定是否将运行兼容性基线
从 v3.8.3 升级到 v3.9.0。本次只核验和记录，没有改动插件业务代码。

## 检查对象和来源

| 对象 | 固定版本或证据 |
| --- | --- |
| 当前插件 | v0.2.9，仓库 HEAD `bdb97849ffadbe07527a70666d3c1293c7965bf9` |
| 发布 DLL | [v0.2.9 Release](https://github.com/jiy29104983/markdownviewdd/releases/tag/v0.2.9) 的 `plugin/markdownviewdd.dll`；使用本地已下载发布包，重新核对其公开记录的 SHA256 |
| 插件源码一致性 | `src/`、`resources/`、`tests/`、CMake、qmake 和 Windows 工作流与 v0.2.9 标签无差异；标签之后为文档修改 |
| 宿主源码 | [Gitee v3.9.0](https://gitee.com/cxasm/notepad--/releases/tag/v3.9.0)，提交 `91105f68b74382128f3313ac5af8accdc77de918` |
| 实际宿主二进制 | [作者 GitHub v3.9.0 Release](https://github.com/cxasm/notepad--/releases/tag/notepad-v3.9.0) 的 `Notepad--v3.9.0-win10-portable.zip`，本次下载并匹配发布 API 的 SHA256 |
| 宿主 EXE 版本资源 | `FileVersion` 和 `ProductVersion` 均为 `3.9.0.0` |

宿主 ZIP SHA256：

```text
5e19f287a9a01035f8e7171462f6298949e3625432b38db3b6f91a80b578f40a
```

插件 ZIP SHA256：

```text
2c12e00b711a1f96634d73828a302f86b713fe58243679b5fb67f274d9da0738
```

插件 DLL SHA256：

```text
3f2c1201626e1bb3ab1aa78cf27a05d36841c2a5637f4adfdfa58b16619d17d1
```

源码参考继续以 Gitee 为准，GitHub 仅用于获取作者发布的实际 Windows 包。两个渠道
的源码标签不同。本次没有核对安装器中的文件是否与便携版逐字节相同，也不将结论
扩展到 Qt 6、x86、macOS 或其他平台。

## 已确认的兼容性缺陷

### P2：六个主题的宿主字体配置读取错误

插件在 [`src/saved_markdown_font.cpp`](../src/saved_markdown_font.cpp) 的 `kThemes`
中固定了旧版 18 项主题表。`readSavedMarkdownFont()` 读取 `notepad/nddsets.ini`
的 `skinid`，随后按该数组拼接 `notepad/userstyle/<主题>/markdown.ini` 或
安装目录的 `themes/<主题>/markdown.ini`。

实际 3.9.0 EXE 的主题表为 13 项。通过解析二进制中的 QString 数组初始化和索引
访问指令，确认前七项一致，后六项与插件不同；发布包目录同时包含新的 `One Dark`
和 `bean green`。这项差异没有体现在仍指向旧提交的公开 release 源码标签中。

| skinid | 3.9.0 实际主题 | 插件选择的主题 | 影响 |
| --- | --- | --- | --- |
| 0–6 | Default、Bespin、Black board、Blue light、Choco、DansLeRuSH-Dark、Deep Black | 相同 | 未发现编号差异 |
| 7 | HotFudgeSundae | lavender | 读取旧主题配置；不存在时回退默认字体 |
| 8 | Monokai | HotFudgeSundae | 读取另一主题的配置 |
| 9 | One Dark | misty rose | 读取旧主题配置；不存在时回退默认字体 |
| 10 | Twilight | Mono Industrial | 读取旧主题配置；不存在时回退默认字体 |
| 11 | yellow rice | Monokai | 读取另一主题的配置 |
| 12 | bean green | Obsidian | 读取旧主题配置；不存在时回退默认字体 |

复现程序直接编译当前 `src/saved_markdown_font.cpp`，在临时目录中为真实主题表的
每项写入不同字号 `20 + skinid`，再调用正式 `readSavedMarkdownFont()`。它使用从
发布 EXE 提取的主题表作为输入，未改动插件逻辑或用户配置。结果：

| skinid | 应读取字号 | 实际读取字号 |
| --- | --- | --- |
| 0–6 | 20–26 | 全部正确 |
| 7 | 27 | 12，内置默认 |
| 8 | 28 | 27，HotFudgeSundae 配置 |
| 9 | 29 | 12，内置默认 |
| 10 | 30 | 12，内置默认 |
| 11 | 31 | 28，Monokai 配置 |
| 12 | 32 | 12，内置默认 |

**13 个配置场景中，六项不符合宿主主题预期。** 字体家族也使用同一错误路径，因而同样
受影响。实际是否明显可见取决于用户是否设置了不同字体，以及是否残留旧主题配置。
“恢复宿主字号”和按正文字号缩放标题等依赖此读取结果的功能也需回归。这不是已观察到
的 DLL 加载崩溃，也不证明主题配色跟随一定失败。

建议在字体适配层区分实际宿主主题协议，或改用可验证的活动主题来源；不能只把旧数组
直接替换为新数组而破坏旧版兼容。回归应同时覆盖旧表、新表、用户自定义字体和缺失配置。

## 二进制与接口核验

| 检查 | 结果 | 证据与边界 |
| --- | --- | --- |
| CPU 架构 | passed | 插件 DLL、宿主 EXE、Qt 和相关 MSVC DLL 均为 PE x64，Machine `0x8664` |
| Qt 版本 | passed | 宿主提供的 Qt5Core、Qt5Gui、Qt5Widgets 版本均为 `5.15.2.0`，与发布插件的构建目标一致 |
| 编译工具链特征 | passed | 宿主 EXE 和插件 DLL 的 PE LinkerVersion 均为 `14.29`；插件既有 Windows 工作流使用 MSVC v142 |
| 插件导出 | passed | DLL 正确导出 `NDD_PROC_IDENTIFY`、`NDD_PROC_MAIN`；宿主 EXE 保留对应加载名称 |
| Qt／MSVC 导入符号 | passed | 1,112 个导入符号全部在发布包附带的对应 DLL 中找到，缺失数为 0 |
| 系统 DLL 导入 | not verified | 其余 40 个符号来自 Windows Kernel32／UCRT，Linux 环境未实际解析系统加载链 |
| 源码插件 ABI | passed | 五个 QString、int、QMenu 指针、QAction 指针的顺序及两个带 QWidget 参数的 std::function 回调与 Gitee 源码一致；不等同于运行时 ABI 实测 |
| Markdown 元对象槽 | passed | 从实际 EXE 的 Qt 元对象表解析出 `void on_viewMarkdown()` 和 `void on_updataMarkdown()`，均为无参槽 |
| 源码导航元对象槽 | passed | 从实际 qmyedit_qt5.dll 元对象表解析出 `void ensureLineVisible(int)` |
| 控件与属性名称 | partial | 实际 EXE 含 `ScintillaEditView`、`MarkdownViewClass`、`editTabWidget`、`textEdit`、`filePath`；实际父子结构和事件时序需实机验证 |
| 预览所有权 | source check passed | Gitee 源码使用 `QPointer<MarkdownView>`；发布 EXE 的销毁／重建行为尚未实测 |
| 字体配置 | failed | 实际发布 EXE 的主题编号变化导致上述六项复现失败 |

导入符号明细：Qt5Widgets 589、Qt5Gui 184、Qt5Core 325、MSVCP140 1、
VCRUNTIME140 12、VCRUNTIME140_1 1。解析 Qt 导出表时显式放宽解析器的 8,192 项默认
上限，并校验已解析名称数等于 PE 表头声明数，避免大型导出表被截断后误报缺符号。

发布包以动态 `qmyedit_qt5.dll` 提供 QScintilla，当前插件未直接导入它，继续通过 Qt
元对象和可访问性接口访问编辑器。二进制存在相应槽及可访问性类标识，但实际
`QAccessibleTextInterface` 的注册与坐标行为仍需 Windows 测试。

## 构建与自动化验证

本次环境：Linux aarch64、GCC 13.3.0、Qt 5.15.13、C++14、Release、offscreen。

| 验证 | 结果 |
| --- | --- |
| 当前插件 Linux Release 构建 | passed，无编译警告 |
| CTest | 8/8 组通过，约 54.22 秒 |
| QtTest 汇总 | 247 passed、0 failed、0 skipped；包含各组初始化和清理 |
| 发布脚本隔离测试 | passed，使用 mock gh，未发布或修改远程 Release |
| 真实 QScintilla 控件探针 | 13 个场景通过：中文／Emoji、不同换行、换行与缩放、嵌套折叠、多选区、边界及缺失能力 |
| 3.9.0 主题映射专项复现 | failed：13 个场景中六项字体读取错误 |
| Windows Release 构建与回归 | passed，复核现有 v0.2.9 正式流水线；本次未重新运行 Windows 构建 |
| Windows 3.9.0 实际宿主 | not verified，用户确认尚未测试 |

现有 [Windows 正式流水线](https://github.com/jiy29104983/markdownviewdd/actions/runs/34950791883)
的 DLL 构建、Qt 回归和发布脚本测试均为 success，本次通过 GitHub API 重新核对。
当前插件源码与发布标签一致，因此该构建可作为当前 DLL 的既有构建证据。历史
[v0.2.9 发布验证记录](verification-release-v0.2.9.md) 的维护者实测确认没有明确指向本次
3.9.0 环境，不能据此标记本次宿主测试通过。

QScintilla 探针使用 Gitee 固定源码构建，Linux 编译时通过 `QMAKE_CXXFLAGS` 添加
`-include cstdint`，处理上游头文件缺少 `intptr_t` 声明的问题；未修改宿主源码。
探针只验证该公开源码对应的真实控件，不代表已运行发布包里的 Windows DLL。

既有字体回归使用旧主题表的模拟配置，所以八组测试全通过与本次专项复现失败并不矛盾。
同样，`v3.8.3` 和 `v3.9.0` 标签指向同一源码提交，不能证明两个发布二进制行为相同。

## Windows 实机验收清单

修复字体映射后，在上述 3.9.0 x64 包中安装候选插件，使用
[`examples/preview-demo.md`](../examples/preview-demo.md) 至少完成以下步骤，并记录
具体包版本、插件 SHA256、结果和诊断日志：

1. 启动无加载错误，插件菜单和 Ctrl+Shift+M 可打开侧栏，Markdown 正常渲染。
2. 输入、手动刷新、切换标签、保存／改名后，预览内容、文件名及相对图片归属正确。
3. 隐藏／关闭／重开预览，关闭文档和宿主；两个宿主窗口互不干扰，无崩溃。
4. 在全部 13 个主题中设置不同 Markdown 字号，重开预览后核对正文、标题比例、
   “恢复宿主字号”和 Ctrl+滚轮；重点覆盖本报告的六个错误编号。
5. 点击大纲跳转普通、折叠和中文／Emoji 源码，检查滚动、展开行为及编辑选区不被改动。
6. 搜索、代码复制、链接、图片和 HTML 导出可用；在明暗主题及常用 DPI 下检查显示。

当前报告仅能确认静态兼容条件和已复现缺陷。上述实机结果补齐前，保持 v3.8.3 的既有
运行兼容性基线，不把 v3.9.0 宣称为已完整验证。

## 可复核证据

- [结构化检查结果](measurements/notepad-3.9.0-compatibility-2026-10-01.json)：包和文件哈希、
  导入检查、元对象方法、逐组测试统计、控件探针与字体复现逐项结果。
- [主题表二进制取证](measurements/notepad-3.9.0-theme-mapping-2026-10-01.txt)：固定 EXE
  哈希及主题表初始化、索引访问指令。
- 本地完整过程产物位于已忽略的 `build/compatibility-3.9.0/`：`build.log`、`ctest.log`、
  `regression/markdownview_*_tests.txt`、`release-tests.log`、`binary-audit.json`、
  `metaobject-audit.json`、`host-probe.json`、`font_mapping_probe.cpp`、
  `font-mapping-probe.json` 和下载的宿主 ZIP。

主要复核命令：

```bash
git diff --exit-code v0.2.9 HEAD -- src resources tests CMakeLists.txt markdownview.pro .github/workflows/windows-release.yml
git -C notepad-- diff --exit-code v3.8.3 v3.9.0
cmake -S . -B build/compatibility-3.9.0/regression -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build/compatibility-3.9.0/regression --parallel 4
ctest --test-dir build/compatibility-3.9.0/regression -C Release --output-on-failure --no-tests=error
bash tests/release_publish_test.sh
QT_QPA_PLATFORM=offscreen build/compatibility-3.9.0/host-probe/host_probe build/compatibility-3.9.0/host-probe/libqt_boundary.so
QT_QPA_PLATFORM=offscreen build/compatibility-3.9.0/font-probe-build/font_mapping_probe build/compatibility-3.9.0/theme-map.json
```

最后一条为兼容性缺陷复现，当前返回码为 1，JSON 中 `mismatches` 为 6。
