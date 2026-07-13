# 架构说明

## 宿主约束

notepad-- 的插件入口由两个 C 符号组成：

```cpp
bool NDD_PROC_IDENTIFY(NDD_PROC_DATA *data);

int NDD_PROC_MAIN(
    QWidget *notepad,
    const QString &pluginFilePath,
    std::function<QsciScintilla *()> getCurrentEditor,
    std::function<bool(int, void *)> hostCallback,
    NDD_PROC_DATA *data);
```

`NDD_PROC_IDENTIFY` 返回插件元数据和菜单类型。此插件使用 `menuType = 1`，因此宿主会先创建插件根菜单，再调用一次 `NDD_PROC_MAIN` 让插件注册子菜单。入口 ABI 来自 notepad-- 官方仓库中的 `src/include/pluginGl.h`、`src/plugin.h` 和《插件编程开发说明.docx》。本项目在 `src/ndd_plugin_api.h` 中保留同样的字段类型和顺序。

## 组件关系

```text
notepad-- / CCNotePad
  ├─ 插件根菜单
  │    └─ PreviewController 注册 QAction
  ├─ editTabWidget 当前页 ──> PreviewController
  │                              │ Qt 元对象调用
  │                              v
  │                    ScintillaEditView::on_viewMarkdown
  │                              │ 宿主内部读取与更新
  │                              v
  └─ QMainWindow ─────── MarkdownView ──嵌入──> QDockWidget
```

`PreviewController` 从宿主的 `editTabWidget` 取得当前页。由于 API 没有暴露标签切换事件，它定期比较当前指针。插件监听文本变化并合并连续刷新，实际的文本读取和 Markdown 解析仍由 notepad-- 完成。

`MarkdownPreviewDock` 由宿主主窗口持有，使用 `QMainWindow::addDockWidget` 加入右侧停靠区。关闭宿主窗口时 Qt 的父子对象所有权会清理控制器、Dock 和动作。

控制器同时监听编辑器临时创建的右键 `QMenu`。发现原生 Markdown 动作后，插件先断开它与编辑器原生预览槽的连接，再检查文件类型、显示 Dock 并调用宿主槽渲染。原生预览创建后，插件还会断开宿主注册的即时刷新连接，避免大文档在每次按键时重新排版。

## 渲染选择

MarkdownViewer++ 使用 Markdig 将 Markdown 转换为 HTML，再由 WinForms HTMLRenderer 展示。notepad-- v3.8 已有 `MarkdownView`，内部使用 `QTextEdit::setMarkdown()`。插件通过 Qt 元对象调用宿主的 `on_viewMarkdown`，再把宿主创建的窗口嵌入 `QDockWidget`，避免复制渲染逻辑，也避免从插件模块调用静态链接的 QScintilla 实现。

## 文件路径与资源

notepad-- 给每个编辑器对象设置了名为 `filePath` 的 Qt 动态属性。插件读取此属性，将 Markdown 文件所在目录设置为原生预览文档的 `baseUrl`，所以 `![图](images/a.png)` 可以从文件目录解析。

## ABI 与发布

插件入口边界仍包含 Qt 类型、`std::function` 和 `QsciScintilla *`，不是稳定的纯 C ABI。不过插件不再解引用该编辑器指针，也不再链接 QScintilla。因此发布 DLL 必须匹配：

- CPU 架构；
- MSVC 工具链和运行库；
- Qt 主次版本；
- notepad-- v3.8 的窗口对象名和 `on_viewMarkdown` 元对象槽。

插件不再需要 `qmyedit_qt5.lib`。当前实现明确以 notepad-- v3.8 为目标；若宿主以后重命名相关窗口或槽函数，需要更新适配层。
