# markdownview--

markdownview-- 是面向 [notepad--](https://gitee.com/cxasm/notepad--) v3.8.3 的 Markdown 侧边栏预览插件。它复用 notepad-- 自带的 Markdown 渲染，把原本独立弹出的预览窗口放进编辑器右侧，并随当前标签页和文档内容更新。

## 功能

- 在 notepad-- 左侧或右侧停靠 Markdown 预览窗口
- 支持 `.md`、`.markdown`、`.mdown`、`.mkd`、`.mkdn`、`.mdwn`
- 编辑文档时实时刷新预览
- 切换标签页后自动跟随当前 Markdown 文档
- 将编辑器右键菜单中的 Markdown 预览重定向到侧边栏
- 按滚动条比例同步编辑区和预览区
- 从 Markdown 文件所在目录加载相对路径图片
- 跟随 notepad-- 的明暗配色
- 将当前预览导出为 HTML

## 运行环境

- notepad-- v3.8.3 插件版，x64
- Windows 10、Windows 11，或带桌面体验的 Windows Server 2016
- notepad-- 安装目录中已有的 Qt 5.15.2 运行库

插件按 notepad-- v3.8.3 的窗口结构和插件 ABI 开发。其他版本如果调整了 Markdown 预览、编辑器界面或插件回调签名，可能需要重新适配。
详细的宿主版本、固定提交和 ABI 验证边界见
[`docs/host-compatibility.md`](docs/host-compatibility.md)。

## Windows 构建环境

- Visual Studio 2019 或 Visual Studio Build Tools 2019，安装 MSVC v142 x64 工具集
- Qt 5.15.2 `msvc2019_64`
- CMake 3.16 或更高版本

VS Code 可以用作编辑器和构建入口，但仍需安装上述编译工具和 Qt。

## GitHub Actions 云端构建与发布

仓库通过 `.github/workflows/windows-release.yml` 在 GitHub 托管的
`windows-2022` 环境中构建插件，因此发布者不需要在本机安装 Visual Studio、
Qt 或 CMake。

向 `main` 推送代码、创建 Pull Request 或手工运行工作流时，会生成带 SHA256 校验文件
的 Windows x64 Artifact。推送格式为 `vMAJOR.MINOR.PATCH` 的标签时，工作流会校验
源码版本并创建 GitHub Release。发布包包含 `plugin/markdownviewdd.dll`、`README.md`
和 `LICENSE`，不重复附带宿主已经提供的 Qt 运行库。

云端构建和打包成功不能替代 notepad-- 中的真实加载与功能测试。版本同步、候选
Artifact 验证、标签创建、失败处理和发布检查步骤见
[`docs/releasing.md`](docs/releasing.md)。

## 一键构建

把代码下载到 Windows 后，双击仓库根目录的 `build-windows.bat`。脚本默认使用：

- Qt：`C:\Qt\5.15.2\msvc2019_64`
- Visual Studio：2019 x64
- 构建类型：Release

构建成功后，DLL 位于：

```text
build\plugin\markdownviewdd.dll
```

如果 Qt 安装在其他目录，请从 PowerShell 执行：

```powershell
.\scripts\build-windows.ps1 -QtRoot "D:\Qt\5.15.2\msvc2019_64"
```

需要清空旧的 `build` 目录时，加上 `-Clean`：

```powershell
.\scripts\build-windows.ps1 -Clean
```

## 手动使用 CMake 构建

先将 Qt 5.15.2 `msvc2019_64` 的安装目录写入 `QTDIR`，并把 Qt 工具加入 `PATH`。然后在仓库根目录打开 “x64 Native Tools Command Prompt for VS 2019”并执行：

```bat
set "QTDIR=Qt 5.15.2 msvc2019_64 的安装目录"
set "PATH=%QTDIR%\bin;%PATH%"

cmake -S . -B build ^
  -G "Visual Studio 16 2019" -A x64 ^
  -DCMAKE_PREFIX_PATH="%QTDIR%"

cmake --build build --config Release
```

生成文件同样位于 `build\plugin\markdownviewdd.dll`。

## 使用 qmake 构建

```bat
set "QTDIR=Qt 5.15.2 msvc2019_64 的安装目录"
set "PATH=%QTDIR%\bin;%PATH%"

qmake markdownview.pro
nmake release
```

生成文件同样位于 `build\plugin\markdownviewdd.dll`。

## 安装

1. 关闭 notepad--。
2. 升级旧版本时，先从 `plugin` 目录删除 `markdownview.dll`。
3. 将 `markdownviewdd.dll` 复制到 `Notepad--.exe` 同级的 `plugin` 目录。
4. 重新启动 notepad--。

安装到默认目录时，可在管理员 CMD 中执行：

```bat
copy /y build\plugin\markdownviewdd.dll ^
  "%ProgramFiles%\Notepad--\plugin\markdownviewdd.dll"
```

## 使用

打开 Markdown 文件后，可以通过以下任一方式显示预览：

- “插件 → Markdown 预览 → 显示/隐藏预览”
- 快捷键 `Ctrl+Shift+M`
- 编辑器右键菜单中的“在侧边栏预览 Markdown”

插件菜单还提供立即刷新、同步滚动、导出 HTML 和关于信息。预览窗口可以停靠在左右两侧，也可以拖成浮动窗口。

## 实现方式

notepad-- v3.8.3 已经提供 `ScintillaEditView::on_viewMarkdown()` 和 `MarkdownView`。插件调用宿主的原生预览功能，再把 `MarkdownView` 嵌入 `QDockWidget`。文本读取和 Markdown 解析由 notepad-- 完成；插件负责合并连续的文本变化通知，防止重复渲染。

插件每 120 ms 检查一次当前标签页，用于跟随文档切换。插件会阻止宿主在每次按键时同步重排全文，只在输入停顿后刷新一次；等待时间会根据上一次渲染耗时在 350 ms 到 2 秒之间自动调整。Qt 分步排版大文档时，插件会合并预览区滚动范围变化，并仅在侧栏可见时重新计算同步位置。当前文件路径来自编辑器的 `filePath` 属性，主要用于解析相对路径图片。

## 已知限制

- Qt 5.15 的 Markdown 渲染不是完整浏览器，不支持 Mermaid、数学公式、JavaScript 和复杂网页样式。
- 滚动同步按两侧滚动条比例估算，无法精确对应 Markdown 源代码行和预览节点。
- 当前版本按 notepad-- v3.8.3 x64 的源码和插件 ABI 适配。

遇到加载或预览问题时，可以查看诊断日志：

```bat
type "%TEMP%\markdownview.log"
```

## 许可

本项目采用 GNU GPL v3.0 or later。
