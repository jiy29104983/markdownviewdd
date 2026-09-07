# 架构说明

## 宿主约束

notepad-- 的插件入口由两个 C 符号组成：

```cpp
bool NDD_PROC_IDENTIFY(NDD_PROC_DATA *data);

int NDD_PROC_MAIN(
    QWidget *notepad,
    const QString &pluginFilePath,
    std::function<QsciScintilla *(QWidget *)> getCurrentEditor,
    std::function<bool(QWidget *, int, void *)> hostCallback,
    NDD_PROC_DATA *data);
```

`NDD_PROC_IDENTIFY` 返回插件元数据和菜单类型。此插件使用 `menuType = 1`，因此宿主会先创建插件根菜单，再调用一次 `NDD_PROC_MAIN` 让插件注册子菜单。入口 ABI 来自 notepad-- v3.8.3 官方 Gitee 标签中的 `src/include/pluginGl.h` 和 `src/plugin.h`。本项目在 `src/ndd_plugin_api.h` 中保留相同的字段类型与顺序，包括末尾的 `QAction *`；两个回调也保留宿主要求的 `QWidget *` 参数。

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

原生 `MarkdownView` 嵌入 Dock 后由 Dock 的 QWidget 父子关系持有；控制器仍保留编辑器
销毁时调用 `deleteLater` 的协作路径，以覆盖编辑器先关闭的情况。Dock 为当前及已经隐藏的
全部原生预览分别保存 `destroyed` 连接。普通预览关闭时仅在它仍是当前预览的情况下恢复
备用界面；Dock 析构开始时先停止布局计时器、断开滚动与全部预览销毁连接，再由 Qt 递归
删除子控件，避免派生类成员销毁后进入回调，也避免对同一预览重复删除。

控制器同时监听编辑器临时创建的右键 `QMenu`。发现原生 Markdown 动作后，插件先断开它与编辑器原生预览槽的连接，再检查文件类型、显示 Dock 并调用宿主槽渲染。原生预览创建后，插件按“`textChanged` 信号到编辑器自身接收者”的范围清除宿主即时刷新连接；插件和主窗口是不同接收者，因此各自的状态更新连接不受影响。每次文本变化还会重新检查该连接，防止宿主入口再次恢复逐键渲染。

文本变化只重启一个单次计时器，连续输入期间不会渲染。计时器等待时间根据上一次渲染耗时在 350 ms 到 2 秒之间调整，随后只调用一次宿主 `on_updataMarkdown`。侧栏隐藏时会停止待执行的渲染。文件类型和路径检查发生在真正渲染时，不进入逐键输入路径。

控制器分别记录活动编辑器、预览所属编辑器、内容版本、已渲染版本和预览状态。状态覆盖
无文档、不支持、待刷新、就绪和失败。标签切换或文本变化会立即推进内容版本并使旧预览
失去同步资格；只有编辑器身份和版本均匹配且状态为就绪时，才允许编辑器与预览双向滚动。
手工刷新会先重新读取当前标签，防抖任务和渲染后的零延迟回调也会再次核对身份与版本，
因此快速切换或关闭标签时，过期任务不能把旧文档恢复为当前预览。Dock 同时保存预览所属
编辑器和内容版本，作为后续导出及元数据更新判断内容归属的明确接口。

Qt 5.15 会分步排版较长的富文本，期间预览滚动条的范围可能多次变化。Dock 以短计时器合并这些变化，仅在侧栏可见且启用同步滚动时通知控制器重新应用编辑器滚动比例，不会强制一次性排版整篇文档。

## 渲染选择

notepad-- v3.8.3 已有 `MarkdownView`，内部使用 `QTextEdit::setMarkdown()`。插件通过 Qt 元对象调用宿主的 `on_viewMarkdown`，再把宿主创建的窗口嵌入 `QDockWidget`，避免复制渲染逻辑，也避免从插件模块调用静态链接的 QScintilla 实现。

## 文件路径与资源

notepad-- 给每个编辑器对象设置了名为 `filePath` 的 Qt 动态属性。插件读取此属性，将 Markdown 文件所在目录设置为原生预览文档的 `baseUrl`，所以 `![图](images/a.png)` 可以从文件目录解析。

## ABI 与发布

插件入口边界仍包含 Qt 类型、`std::function` 和 `QsciScintilla *`，不是稳定的纯 C ABI。不过插件不再解引用该编辑器指针，也不再链接 QScintilla。因此发布 DLL 必须匹配：

- CPU 架构；
- MSVC 工具链和运行库；
- Qt 主次版本；
- notepad-- v3.8.3 的插件 ABI、窗口对象名和 `on_viewMarkdown` 元对象槽。

插件不再需要 `qmyedit_qt5.lib`。当前实现明确以 notepad-- v3.8.3 为目标；若宿主以后修改插件回调、结构体、窗口对象名或槽函数，需要更新适配层。
