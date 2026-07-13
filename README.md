# markdownview--

markdownview-- 是面向 [notepad--](https://github.com/cxasm/notepad--) v3.8 的 Markdown 侧边栏预览插件。它复用 notepad-- 自带的 Markdown 渲染，把原本独立弹出的预览窗口放进编辑器右侧，并随当前标签页和文档内容更新。

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

- notepad-- v3.8.0 插件版，x64
- Windows 10、Windows 11，或带桌面体验的 Windows Server 2016
- notepad-- 安装目录中已有的 Qt 5.15.2 运行库

插件按 notepad-- v3.8.0 的窗口结构开发。其他版本如果调整了 Markdown 预览或编辑器界面，可能需要重新适配。

## Windows 构建环境

- Visual Studio 2019 或 Visual Studio Build Tools 2019，安装 MSVC v142 x64 工具集
- Qt 5.15.2 `msvc2019_64`
- CMake 3.16 或更高版本

VS Code 可以用作编辑器和构建入口，但仍需安装上述编译工具和 Qt。

## 使用 CMake 构建

先将 Qt 5.15.2 `msvc2019_64` 的安装目录写入 `QTDIR`，并把 Qt 工具加入 `PATH`。然后在仓库根目录打开 “x64 Native Tools Command Prompt for VS 2019”并执行：

```bat
set "QTDIR=Qt 5.15.2 msvc2019_64 的安装目录"
set "PATH=%QTDIR%\bin;%PATH%"

cmake -S . -B build ^
  -G "Visual Studio 16 2019" -A x64 ^
  -DCMAKE_PREFIX_PATH="%QTDIR%"

cmake --build build --config Release
```

生成文件：

```text
build\plugin\markdownviewdd.dll
```

也可以在 PowerShell 中运行构建脚本：

```powershell
$env:QTDIR = "Qt 5.15.2 msvc2019_64 的安装目录"
.\scripts\build-windows.ps1
```

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

notepad-- v3.8.0 已经提供 `ScintillaEditView::on_viewMarkdown()` 和 `MarkdownView`。插件调用宿主的原生预览功能，再把 `MarkdownView` 嵌入 `QDockWidget`。文本读取和 Markdown 解析由 notepad-- 完成；插件负责合并连续的文本变化通知，防止重复渲染。

插件每 120 ms 检查一次当前标签页，用于跟随文档切换。自动预览会等待输入停顿后再刷新，64 KB 及以上的文件会多等一会儿。当前文件路径来自编辑器的 `filePath` 属性，主要用于解析相对路径图片。

## 已知限制

- Qt 5.15 的 Markdown 渲染不是完整浏览器，不支持 Mermaid、数学公式、JavaScript 和复杂网页样式。
- 滚动同步按两侧滚动条比例估算，无法精确对应 Markdown 源代码行和预览节点。
- 当前版本只针对 notepad-- v3.8.0 x64 测试和适配。

遇到加载或预览问题时，可以查看诊断日志：

```bat
type "%TEMP%\markdownview.log"
```

## 许可

本项目采用 GNU GPL v3.0 or later。参考项目 MarkdownViewer++ 使用 MIT License；本项目没有复制其 C#/.NET 渲染代码。
