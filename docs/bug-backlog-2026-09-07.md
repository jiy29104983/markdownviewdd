# 代码审查修复单（2026-09-07）

## 使用说明

本清单基于代码审查基线 `120b87b`，插件版本 `0.2.7`。宿主参考基线为 notepad--
`v3.8.3`，固定提交 `91105f68b74382128f3313ac5af8accdc77de918`。
代码位置中的行号属于审查基线；修复后请通过函数名重新定位，不要依赖旧行号。

共 16 单，按建议执行顺序编号：10 单功能、安全及发布缺陷，1 单性能治理，
1 单回归验证建设，4 单架构与维护性改进。改进项不等同于已经发生的故障。
所有单据初始状态均为“待处理”，尚未实施修复，也未完成真实宿主复现。

- **P1**：优先排查和修复的崩溃风险。
- **P2**：功能正确性、发布可靠性及关键验证问题，建议在下一轮发布前处理。
- **P3**：后续架构、资源占用和维护性改进。
- **静态确认**：当前代码存在相应行为或遗漏；不表示已在 Windows 宿主中复现。
- **风险待复现**：代码存在危险路径，实际触发条件及运行结果需要进一步验证。

建议按编号顺序逐单交给修复 agent。同优先级的先后顺序考虑了依赖关系；
后续 agent 应在前面单据的修复结果上继续工作，不能恢复审查基线中的旧实现。
这些单据有源码交叉，默认不适合在同一工作区同时修改。

## 每单共用的实施与交付要求

1. 先阅读仓库 `AGENTS.md`，检查 `git status`，保留已有改动。修改组件关系、渲染或
   宿主调用前阅读并同步更新 [architecture.md](architecture.md)；涉及兼容性时阅读
   [host-compatibility.md](host-compatibility.md)；涉及发布时阅读
   [releasing.md](releasing.md)。
2. 保持 Qt 5.15.2、MSVC v142、Windows x64 和 C++14 基线。`notepad--/` 仅作只读参考；
   不修改插件 ABI 字段顺序、字段类型或导出签名，不重新引入对宿主静态 QScintilla
   实现的直接调用。
3. 先核实本单问题，补足可重复的触发条件。风险项若被运行证据排除，应提供证据并标注
   “不成立”或“仅需防御性加固”，不能为了匹配单据而修改正确代码。
4. 每单集中处理一个主题；在实际修复链路上增加必要的行为回归验证。BUG-012 是统一
   整理测试入口和补齐覆盖，不代表此前修复可以不验证。
5. 影响 DLL 的修复需要 Release 构建证据。分别记录静态检查、自动化测试、Windows
   Release 编译、Artifact 校验、真实宿主测试；不能用其中一项通过替代其他项。
   审查环境曾因缺少 `Qt5Config.cmake` 无法完成本地配置，后续应重新核实可用环境。
6. 将修复交回验证前，提供单据编号、修改摘要、文件列表、提交号或补丁、复现方式、
   实际验证结果及未验证项。若尚未取得必需的运行证据，状态写“修复完成／待验证”，
   不直接写“已关闭”。

## 优先级与依赖总表

| 编号 | 优先级 | 名称 | 类型／证据 | 前置依赖 |
| --- | --- | --- | --- | --- |
| BUG-001 | P1 | Dock 析构期间的回调与对象生命周期风险 | 安全；风险待复现 | 无 |
| BUG-002 | P2 | 标签切换期间预览与编辑器身份不一致 | 功能；静态确认 | BUG-001 |
| BUG-003 | P2 | HTML 导出选错文档、导出提示页或旧内容 | 功能；静态确认 | BUG-002 |
| BUG-004 | P2 | 同进程多窗口只初始化一个插件控制器 | 功能；静态确认 | BUG-001 |
| BUG-005 | P2 | 文件路径变化后预览元数据与资源路径不刷新 | 功能；静态确认 | BUG-002、BUG-004 |
| BUG-006 | P2 | 关闭同步滚动后刷新丢失阅读位置 | 功能；静态确认 | BUG-002 |
| BUG-007 | P2 | 原生预览未接入链接与锚点处理 | 功能；静态确认 | BUG-002 |
| BUG-008 | P2 | HTML 跨目录导出未处理相对资源 | 功能；静态确认 | BUG-003、BUG-005 |
| BUG-009 | P2 | 文档样式应用与主题刷新不完整 | 功能；静态确认 | BUG-005、BUG-006 |
| BUG-010 | P2 | 发布重跑覆盖已公开 Release 附件 | 发布；静态确认 | 无，可提前独立处理 |
| BUG-011 | P2 | 全文同步渲染缺少大文档性能治理 | 性能；影响待测量 | BUG-002、BUG-006、BUG-009 |
| BUG-012 | P2 | 缺少统一的关键行为回归测试入口 | 验证建设；静态确认 | BUG-001～BUG-011，整合已有测试 |
| BUG-013 | P3 | 宿主适配散落在控制器和 Dock 中 | 架构改进 | BUG-012 |
| BUG-014 | P3 | 多文档原生预览缓存没有容量控制 | 资源治理；占用待测量 | BUG-001、BUG-013 |
| BUG-015 | P3 | 轮询依赖过重且滚动缓存忽略范围变化 | 架构与功能改进 | BUG-002、BUG-006、BUG-013 |
| BUG-016 | P3 | 诊断日志缺少容量限制和实例隔离 | 可维护性改进 | BUG-004 |

## BUG-001：Dock 析构期间的回调与对象生命周期风险

- **优先级／状态**：P1／修复完成／待验证。
- **证据性质**：风险待复现。不能把本单描述为已经复现的宿主崩溃。
- **代码位置**：
  [markdown_preview_dock.cpp](../src/markdown_preview_dock.cpp) 的
  `adoptNativePreview()`，约 136～173 行；
  [markdown_preview_dock.h](../src/markdown_preview_dock.h) 的成员和析构定义；
  [preview_controller.cpp](../src/preview_controller.cpp) 的
  `activateNativePreview()`，约 489～498 行。
- **问题影响**：原生预览的 `destroyed` 回调捕获 Dock 的 `this`，并访问成员、计时器
  和裸指针 `m_browser`。Dock 没有显式析构解绑。递归删除子控件时，该回调可能发生在
  派生类成员生命周期结束之后，或在备用浏览器已经释放之后，形成未定义行为与退出崩溃
  风险。需要按 Qt 5.15.2 的实际 QWidget 销毁顺序验证，不能仅凭通用 QPointer 说明下结论。
- **建议复现**：创建原生预览后销毁 Dock；依次切换多个已预览文档再关闭窗口；分别测试
  编辑器先销毁、Dock 先销毁、存在 `deleteLater` 和待执行布局回调的顺序。
- **修改计划**：
  1. 记录并管理全部原生预览连接，包括已经隐藏的预览对象对应连接。
  2. 显式定义析构／解绑阶段，在派生类成员销毁之前停止计时器并断开相关回调。
  3. 区分“单个预览被关闭”和“Dock 整体正在析构”，后者不执行恢复界面等操作。
  4. 检查跨父对象的所有权、动态属性清除和 `deleteLater` 协作；避免重复删除。
     单纯替换裸指针不足以作为整个问题的修复证明。
- **验收标准**：上述销毁顺序均不访问无效对象；普通关闭预览仍能正确恢复界面；
  已关闭编辑器的预览不会被重新激活；条件允许时提供 ASan 或 Windows 内存诊断证据，
  并补充真实宿主关闭测试。
- **范围／依赖**：仅收紧生命周期和连接管理，不改变渲染器或 ABI；无前置依赖。

## BUG-002：标签切换期间预览与编辑器身份不一致

- **优先级／状态**：P2／修复完成／待验证。
- **证据性质**：静态确认。
- **代码位置**：[preview_controller.cpp](../src/preview_controller.cpp) 的
  `pollEditor()`（211）、`renderNow()`（277）、`scrollEditorToRatio()`（354）、
  `attachEditor()`（389）、`updateSynchronizedScroll()`（522）；
  [markdown_preview_dock.h](../src/markdown_preview_dock.h) 的当前预览状态。
- **问题影响**：活动编辑器先切换，旧预览在防抖期间仍显示，滚动同步却已经使用新编辑器。
  可产生 A 的预览驱动 B 的编辑区、B 的滚动位置驱动 A 的预览等错误。手工刷新入口也未
  重新核实活动编辑器，存在轮询尚未更新时刷新旧标签的窗口。
- **建议复现**：打开可明显区分的长文档 A、B，从 A 切到 B 后立即滚动侧栏、点击刷新；
  再测试 A→B→A 快速切换及关闭当前标签。
- **修改计划**：
  1. 建立明确的状态记录，区分活动编辑器、预览所属编辑器、路径、内容版本和已渲染版本。
  2. 状态至少覆盖无文档、不支持、待刷新、就绪和失败；具体类型由实现者按现有代码决定。
  3. 标签切换时立即使旧的同步关系失效；只有身份匹配且状态有效才允许双向滚动。
  4. 手工刷新重新确认活动编辑器；延迟任务携带或核对文档身份／代次，过期任务直接丢弃。
  5. 给后续导出和元数据更新提供明确接口，避免继续用可见性推断内容归属。
- **验收标准**：A 的预览不能改变 B 的滚动位置；快速切换后最终只显示正确活动文档；
  手工刷新针对当前标签；销毁和过期回调不恢复旧内容；连续输入仍保持防抖。
- **范围／依赖**：依赖 BUG-001；本单建立最小状态模型，不同时完成导出服务和宿主适配重构。

## BUG-003：HTML 导出选错文档、导出提示页或旧内容

- **优先级／状态**：P2／修复完成／待验证。
- **证据性质**：静态确认；立即导出的旧内容行为需要用明确的产品语义收敛。
- **代码位置**：[markdown_preview_dock.cpp](../src/markdown_preview_dock.cpp) 的
  `showMessage()`、`htmlSnapshotFor()`、`saveHtmlSnapshot()`、`writeHtmlSnapshot()`；
  [preview_controller.cpp](../src/preview_controller.cpp) 的 `currentHtmlSnapshot()`、
  `exportCurrentHtml()` 和 `installMenu()` 导出动作。
- **问题影响**：`activeDocument()` 通过原生控件是否可见选择文档。隐藏 Dock 后会选择
  备用浏览器，可能静默失败或导出先前提示页。错误提示非空，也会被当成有效文档。
  导出入口没有确认预览与当前编辑器的身份、版本是否一致。
- **建议复现**：预览后隐藏 Dock 再导出；先打开非 Markdown 文件，再预览 Markdown、
  隐藏并导出；错误状态导出；编辑或切换标签后立即导出。
- **修改计划**：
  1. 推荐语义为导出当前活动 Markdown 文档的最新有效内容；隐藏侧栏不应改变导出对象。
     将该语义写入用户文档。
  2. 基于 BUG-002 的状态判断能否导出，提示页和错误页不能作为数据源。
  3. 需要刷新时先取得有效内容；在打开文件对话框前生成 HTML 字节快照或独立文档副本，
     后续写入不依赖仍可能变化的界面文档指针。
  4. 将快照生成、保存交互和文件写入分开，保留 `QSaveFile` 的原子保存行为。
  5. 同步导出动作的可用状态，并为确有必要的失败提供清晰反馈。
- **验收标准**：隐藏与显示时导出对象一致；提示页不能导出；立即导出包含最后一次编辑；
  快速切换不串文档；取消不写文件；写入失败不破坏已有文件。
- **范围／依赖**：依赖 BUG-002；本单不处理图片复制／嵌入，该工作属于 BUG-008。

## BUG-004：同进程多窗口只初始化一个插件控制器

- **优先级／状态**：P2／修复完成／待验证。
- **证据性质**：静态确认。
- **代码位置**：[plugin_exports.cpp](../src/plugin_exports.cpp) 的 `g_controller`（8）
  和 `NDD_PROC_MAIN()`（44～55）；
  [preview_controller.cpp](../src/preview_controller.cpp) 的快捷键注册（111～116）。
  宿主只读参考：`notepad--/src/cceditor/ccnotepad.cpp` 的 `openFileInNewWin()`、
  `quickshow()`、`sendParaToPlugin()`。
- **问题影响**：首个窗口占用全局控制器后，第二个窗口跳过菜单与 Dock 初始化但返回成功。
  全应用快捷键还可能作用于不符合用户预期的窗口。
- **建议复现**：在宿主中“在新窗口打开”文件，检查两个窗口的插件菜单和快捷键，
  关闭第一个窗口后继续操作第二个窗口。
- **修改计划**：
  1. 按宿主窗口建立控制器实例，通过窗口所有权或受控注册表管理并清理。
  2. 同一个窗口重复初始化应幂等，不重复菜单、计时器和事件过滤器。
  3. 根据活动宿主窗口确定快捷键作用范围，防止多个实例争抢同一全局快捷键。
  4. 验证事件过滤器只处理本窗口的编辑器菜单。
- **验收标准**：两个同进程窗口都可独立预览、刷新、导出；操作互不影响；关闭任一窗口
  不破坏另一窗口；重复入口调用不增加重复动作；没有快捷键歧义警告。
- **范围／依赖**：依赖 BUG-001；不改动导出 ABI，不将一个 Dock 跨窗口共享。

## BUG-005：文件路径变化后预览元数据与资源路径不刷新

- **优先级／状态**：P2／修复完成／待验证。
- **证据性质**：静态确认。
- **代码位置**：[preview_controller.cpp](../src/preview_controller.cpp) 的
  `pollEditor()`（211）、`attachEditor()`（389）、`currentFilePath()`（546）；
  [markdown_preview_dock.cpp](../src/markdown_preview_dock.cpp) 的
  `adoptNativePreview()`、`setDocumentInfo()`、`baseUrlForFile()`。
  宿主只读参考：`notepad--/src/cceditor/ccnotepad.cpp` 的 `filePath` 属性更新，约 5785、5886 行。
- **问题影响**：同一编辑器另存为或重命名时，仅动态属性改变，不一定产生文本变化。
  预览文件类型、标题、图片基准路径和导出默认位置可能一直保留旧值。
- **建议复现**：不修改内容，把 `.md` 改名为 `.txt`；另存到包含不同同名图片的目录；
  将未命名文档保存为 Markdown。
- **修改计划**：
  1. 监听编辑器的 `DynamicPropertyChange` 并仅处理 `filePath`，或采用等效的元数据检测。
  2. 使用 BUG-002 的状态模型统一更新路径、类型、有效性和导出信息。
  3. 路径变化后更新资源基准 URL，必要时刷新资源及内容，避免继续显示旧目录图片。
  4. 编辑器解绑时移除相应观察逻辑，防止旧窗口事件影响新窗口。
- **验收标准**：无需额外键入或切换标签即可更新；扩展名变化及时改变预览状态；
  新目录的图片正确；中文／空格路径正常；多窗口只更新自己的状态。
- **范围／依赖**：依赖 BUG-002、BUG-004；无需修改宿主保存逻辑。

## BUG-006：关闭同步滚动后刷新丢失阅读位置

- **优先级／状态**：P2／修复完成／待验证。
- **证据性质**：实际渲染路径缺少恢复逻辑已静态确认，跳动程度需运行验证。
- **代码位置**：[preview_controller.cpp](../src/preview_controller.cpp) 的
  `renderNow()`（277）、`activateNativePreview()`（515）；
  [markdown_preview_dock.cpp](../src/markdown_preview_dock.cpp) 的 `renderMarkdown()`（184）。
- **问题影响**：实际调用宿主 `QTextEdit::setMarkdown()` 会重建内容和重置光标，可能把
  预览滚回顶部。保存滚动比例的实现只存在于当前未调用的备用渲染函数中。
- **建议复现**：关闭同步滚动，将长文档预览滚到中后部，在编辑器修改内容并等待自动刷新，
  再测试手工刷新与图片导致的延迟布局。
- **修改计划**：
  1. 在真实原生渲染调用前保存当前文档的阅读位置，在渲染及必要的后续布局后恢复。
  2. 恢复操作核对文档身份；同步开启时以编辑器位置为准，关闭时保持预览阅读位置。
  3. 处理内容缩短后的边界，避免越界和不断相互修正。
  4. 删除无用途的备用恢复实现或明确其调用关系，避免两条路径表现不同。
- **验收标准**：同步关闭时刷新不无故跳顶；开启时保持原有同步功能；切换文档不继承
  其他文档位置；长文档完成布局后位置稳定；恢复不造成反向滚动反馈。
- **范围／依赖**：依赖 BUG-002；本单不引入精确源代码行到 Markdown 节点映射。

## BUG-007：原生预览未接入链接与锚点处理

- **优先级／状态**：P2／修复完成／待验证。
- **证据性质**：静态确认。
- **代码位置**：[markdown_preview_dock.cpp](../src/markdown_preview_dock.cpp) 的
  浏览器信号连接（89）、`adoptNativePreview()`（114）、`openLink()`（374）。
  宿主只读参考：`notepad--/src/markdownview.ui` 的 `textEdit`，约 34～38 行。
- **问题影响**：链接回调只连接到备用 `QTextBrowser`，实际显示的原生 `QTextEdit`
  未接入该逻辑；正常 Markdown 预览中的外部链接、相对链接和页内锚点无法按插件逻辑处理。
- **建议复现**：在正常原生预览中点击 HTTPS 链接、相对本地文档链接和存在的页内锚点。
- **修改计划**：
  1. 在实际显示控件上实现链接激活，优先采用局部适配；若需要统一展示控件，先在单据中
     说明具体方案和对宿主集成的影响。
  2. 页内锚点交给当前预览定位；相对链接按所属文档路径解析。
  3. 保持明确的协议允许范围和失败反馈，不让选中文字、拖动或滚动误触发打开。
  4. 外部打开行为封装为可替换入口，自动化测试记录请求，不实际启动外部应用。
- **验收标准**：支持的外部链接能打开；有效锚点跳转正确；相对路径与当前文档对应；
  不支持的协议不被打开；文字选择、复制、滚动保持正常。
- **范围／依赖**：依赖 BUG-002；本单不扩展到浏览器级渲染或脚本执行。

## BUG-008：HTML 跨目录导出未处理相对资源

- **优先级／状态**：P2／修复完成／待验证。
- **证据性质**：静态确认。
- **代码位置**：[markdown_preview_dock.cpp](../src/markdown_preview_dock.cpp) 的
  `htmlSnapshotFor()`、`saveHtmlSnapshot()`、`writeHtmlSnapshot()`、`baseUrlForFile()`；
  [preview_controller.cpp](../src/preview_controller.cpp) 的 `currentHtmlSnapshot()`。
- **问题影响**：直接 `toHtml()` 保留图片原始相对名称，导出到其他目录后浏览器从错误
  目录查找资源，导致图片丢失。预览文档的 `baseUrl` 本身不构成可迁移的资源包。
- **建议复现**：Markdown 引用 `images/a.png`，导出到其他目录后打开 HTML；再移动导出
  结果，检查图片是否仍可用。
- **修改计划**：
  1. 明确导出资源策略。建议优先评估将已成功加载的本地图片嵌入 HTML，保持单文件导出；
     如采用资源目录方案，需定义目录命名、重复资源、失败清理及覆盖行为并写入文档。
  2. 从固定快照及正确源路径解析资源，在导出副本中重写，不能修改实时预览文档。
  3. 处理中文／空格路径、同名资源、缺失图片以及嵌入后的文件体积。
  4. 外部网页链接保留原语义，不把任意本地被链接文件自动打包，也不默认下载远程资源。
- **验收标准**：跨目录导出正确显示本地图片；所选可迁移策略符合文档描述；原预览不变；
  资源缺失有明确结果；保存失败不破坏现有文件和无关资源。
- **范围／依赖**：依赖 BUG-003、BUG-005；不新增网络抓取服务。

## BUG-009：文档样式应用与主题刷新不完整

- **优先级／状态**：P2／修复完成／待验证。
- **证据性质**：样式应用路径和主题事件缺失已静态确认，具体视觉差异需截图验证。
- **代码位置**：[markdown_preview_dock.cpp](../src/markdown_preview_dock.cpp) 的
  `adoptNativePreview()`（128）、`loadStyleSheet()`（395）；
  [preview_controller.cpp](../src/preview_controller.cpp) 的首次渲染复用分支（508）；
  [markdown.css](../resources/markdown.css)。
- **问题影响**：`setDefaultStyleSheet()` 不能等同于浏览器 CSS 对普通 Markdown 的全面
  应用；首次设置又发生在宿主初次渲染之后。主题改变时未主动更新固化到文档的格式，
  外层配色变化不能证明正文、代码块、表格和嵌入 HTML 全部正确。
- **建议复现**：包含标题、代码、引用、表格、链接和嵌入 HTML 的文档，分别检查首次打开、
  手工刷新、明暗主题切换后的表现。
- **修改计划**：
  1. 列明 Qt 5.15.2 主渲染链路实际支持的样式，区分控件 palette、Markdown 文本格式和 HTML CSS。
  2. 将必要文档格式应用到真实渲染结果，或采用经验证的统一渲染方案；避免只扩充 CSS 文件。
  3. 监听适当的主题／配色事件，合并更新，确保首次与后续渲染一致。
  4. 保持阅读位置；主题更新不能恢复宿主逐键同步渲染，也不能无谓增加全文解析次数。
- **验收标准**：首次与刷新后样式一致；明暗主题下文字、链接和代码可读；相关块格式符合
  明确支持范围；提供前后截图；主题切换不串文档、不改变阅读位置。
- **范围／依赖**：依赖 BUG-005、BUG-006；不承诺完整浏览器 CSS 支持。

## BUG-010：发布重跑覆盖已公开 Release 附件

- **优先级／状态**：P2／待处理。
- **证据性质**：静态确认。
- **代码位置**：[windows-release.yml](../.github/workflows/windows-release.yml) 的
  `Create or update GitHub Release` 步骤，约 192～198 行；
  [releasing.md](releasing.md) 的发布失败处理约束。
- **问题影响**：Release 已存在时使用 `gh release upload --clobber`，允许同一公开版本的
  ZIP 和 SHA256 被重新构建的不同内容覆盖，破坏产物可追溯性并违背当前发布规则。
- **建议复现**：使用模拟 `gh` 返回结果或隔离测试仓库，分别模拟不存在、完整存在、附件
  缺失、内容相同和内容不同；不要覆盖生产 Release 来验证本单。
- **修改计划**：
  1. 取消默认覆盖公开附件，明确已发布版本的幂等判定和冲突处理。
  2. 完整且相同的发布可以跳过；不同内容应失败，不能把重新生成 ZIP 的差异当成覆盖授权。
  3. 区分 Release 不存在与网络／权限失败，不能把所有查询失败都当作不存在。
  4. 附件缺失时给出明确恢复策略，不覆盖已存在附件；同步更新发布手册。
- **验收标准**：不同附件不能覆盖同一公开版本；相同结果可按定义幂等执行；查询错误
  不错误进入创建分支；保留版本校验、权限范围和校验文件生成逻辑。
- **范围／依赖**：无前置依赖，可提前独立修复；本单不包含重打标签或修改现有生产 Release。

## BUG-011：全文同步渲染缺少大文档性能治理

- **优先级／状态**：P2／待处理。
- **证据性质**：同步全文路径已确认，卡顿阈值和影响需测量；属于性能治理单。
- **代码位置**：[preview_controller.cpp](../src/preview_controller.cpp) 的
  `scheduleRender()`（256）、`renderNow()`（305）、`activateNativePreview()`（449～519）。
  宿主只读参考：`notepad--/src/scintillaeditview.cpp` 的 `on_updataMarkdown()`，
  `notepad--/src/markdownview.cpp` 的 `viewMarkdown()`。
- **问题影响**：防抖减少调用次数，但实际读取、解析仍在 GUI 线程同步完成。
  同一大文档每次刷新仍可能阻塞输入；连续输入时预览延迟也没有独立的状态反馈。
- **建议复现**：使用有代表性的中小文档和较大文档，分别测量连续输入、首次打开、停顿刷新、
  手工刷新和表格／图片密集内容；记录文件规模、设备和耗时。
- **修改计划**：
  1. 建立可重复测量，区分宿主调用耗时、后续布局和输入响应；先确定可接受的性能目标。
  2. 利用内容版本跳过无变化重渲染；避免“显示、切换、刷新”引起重复解析。
  3. 根据测量结果采用大文档降频、明确的手工刷新模式等有界策略，并展示待刷新状态。
  4. 若确需后台解析，必须先取得安全的数据快照并明确独立解析边界；不能在工作线程调用
     宿主 QWidget、QTextEdit 或现有原生预览槽。更换渲染器需要单独说明设计和兼容性影响。
- **验收标准**：提交同条件前后性能数据及采用阈值；普通文档没有明显退化；大文档策略
  可预测且可手工刷新；连续输入期间不恢复逐键全文解析；最终预览版本正确。
- **范围／依赖**：依赖 BUG-002、BUG-006、BUG-009；未测量前不能宣称已解决所有大文档卡顿。

## BUG-012：缺少统一的关键行为回归测试入口

- **优先级／状态**：P2／待处理。
- **证据性质**：静态确认；属于验证建设单。
- **代码位置**：[CMakeLists.txt](../CMakeLists.txt)、
  [windows-release.yml](../.github/workflows/windows-release.yml)、
  [preview-demo.md](../examples/preview-demo.md)；后续单据已经添加的测试文件。
- **问题影响**：目前缺少自动化测试套件，现有示例也不足以覆盖生命周期、文档切换、导出
  与相对图片等边界。仅成功编译无法发现本轮审查中的行为缺陷。
- **修改计划**：
  1. 整合此前各单增加的回归测试，建立统一、可重复的执行入口；避免重复创建同类测试。
  2. 使用 Qt Test 或合适的行为测试方案，构造可替换的宿主窗口、编辑器和原生预览。
     模拟测试只证明插件行为，不冒充真实宿主 ABI 验证。
  3. 覆盖销毁顺序、多窗口、快速切换、路径变化、导出有效性及资源、滚动恢复和防抖。
  4. 为发布幂等行为提供隔离验证；为图片、锚点、主题及较长文本补充手工测试输入。
  5. 将适合自动化的检查接入 Windows CI，真实宿主测试保留独立记录。
- **验收标准**：提供可执行命令和测试结果；代表性回归测试能区分修复前后行为；测试不
  打开真实外部链接、不覆盖生产 Release、不依赖个人路径；Windows Release 构建与测试
  分别可见；真实宿主未运行时明确标注。
- **范围／依赖**：在 BUG-001～BUG-011 后整合；前面各单不以等待本单为由跳过必要验证。

## BUG-013：宿主适配散落在控制器和 Dock 中

- **优先级／状态**：P3／待处理。
- **证据性质**：架构改进，不单独声称已导致故障。
- **代码位置**：[preview_controller.cpp](../src/preview_controller.cpp) 的
  `resolveCurrentEditor()`、`bridgeEditorContextMenu()`、`nativePreviewForEditor()`、
  `disconnectHostImmediateRefresh()`、`activateNativePreview()`；
  [markdown_preview_dock.cpp](../src/markdown_preview_dock.cpp) 的宿主控件发现与嵌入逻辑。
- **问题影响**：对象名、动态属性、槽名称、连接处理和窗口结构假设混在交互与渲染调度中，
  宿主适配变化容易影响多个功能，也难以替换宿主做行为测试。
- **修改计划**：
  1. 提取最小 `HostAdapter`，集中宿主能力探测、编辑器识别、路径读取、原生预览调用及连接管理。
  2. 适配层返回结构化状态和错误信息，控制器负责调度，Dock 负责展示；复用 BUG-002 的
     状态模型和 BUG-003 的导出边界，避免再创建重复状态来源。
  3. 为不支持的宿主能力提供可解释的失败，控制对宿主信号连接的修改范围。
  4. 更新架构与兼容性文档，并使用 BUG-012 的回归测试证明重构保持行为。
- **验收标准**：宿主专用对象名和槽调用集中可查；控制器不再直接散布兼容性探测；
  单元测试可替换适配层；真实基线宿主功能不退化；ABI 和静态 QScintilla 边界保持不变。
- **范围／依赖**：依赖 BUG-012；不要求把每个辅助函数拆成单独的类。

## BUG-014：多文档原生预览缓存没有容量控制

- **优先级／状态**：P3／待处理。
- **证据性质**：保留策略已静态确认，内存规模待测量；不能直接标记为内存泄漏。
- **代码位置**：[markdown_preview_dock.cpp](../src/markdown_preview_dock.cpp) 的
  `adoptNativePreview()`（131～155）；
  [preview_controller.cpp](../src/preview_controller.cpp) 的原生预览属性和所有权回调，
  或 BUG-013 已提取的适配层。
- **问题影响**：切换时旧预览只隐藏并移出布局，每个浏览过的未关闭标签可保留一份原生
  窗口、富文本文档和图片缓存；多份大文档可能显著增加内存占用。
- **修改计划**：
  1. 测量预览多个文档前后及关闭标签后的对象数量、内存变化，区分缓存与未释放对象。
  2. 制定简单容量或淘汰策略，优先保留当前预览；参数应基于测量，不引入复杂缓存框架。
  3. 淘汰通过受控适配接口执行，清理属性、连接和回调，确认宿主能重新创建已淘汰预览。
  4. 保存必要的阅读状态；隐藏 Dock 和关闭标签的行为保持一致、可解释。
- **验收标准**：超出缓存容量后对象数量受控；返回已淘汰文档可以重新预览；关闭标签
  后资源释放；无悬挂指针、重复删除或逐键渲染连接恢复；提供前后测量。
- **范围／依赖**：依赖 BUG-001、BUG-013；不能直接批量删除宿主仍持有的对象而不验证协作。

## BUG-015：轮询依赖过重且滚动缓存忽略范围变化

- **优先级／状态**：P3／待处理。
- **证据性质**：当前实现已静态确认；事件驱动改造属于架构改进。
- **代码位置**：[preview_controller.cpp](../src/preview_controller.cpp) 的
  `m_pollTimer` 初始化、`pollEditor()`（211）、`updateSynchronizedScroll()`（522）；
  [markdown_preview_dock.cpp](../src/markdown_preview_dock.cpp) 的滚动连接（320～356）。
- **问题影响**：每 120 ms 查找活动标签，即使侧栏隐藏仍持续运行；状态响应受轮询延迟
  影响。滚动缓存只比较 `value`，滚动范围变化但数值不变时可能跳过本应更新的比例。
- **修改计划**：
  1. 经适配层连接标签切换及滚动条 `valueChanged`、`rangeChanged`，需要时保留低频轮询兜底。
  2. 事件触发后在合适时机统一读取完整状态，考虑宿主可能先切标签、后设置文件属性。
  3. 缓存包含文档身份及 `minimum/maximum/value`，或直接用明确的脏状态调度。
  4. 区分用户操作与程序同步，防止事件驱动后形成双向反馈；处理隐藏和对象销毁解绑。
- **验收标准**：切换响应不依赖固定轮询窗口；滚动范围变化时比例及时更新；两侧无振荡；
  隐藏期间不持续做无效高频查询；初始化和宿主更新顺序不导致漏绑。
- **范围／依赖**：依赖 BUG-002、BUG-006、BUG-013；不重新实现精确行级映射。

## BUG-016：诊断日志缺少容量限制和实例隔离

- **优先级／状态**：P3／待处理。
- **证据性质**：静态确认；属于可维护性改进。
- **代码位置**：[diagnostics.cpp](../src/diagnostics.cpp) 的 `logFilePath()`、
  `resetLog()`、`write()`；[plugin_exports.cpp](../src/plugin_exports.cpp) 的日志初始化（36）；
  [preview_controller.cpp](../src/preview_controller.cpp) 的路径和刷新日志调用。
- **问题影响**：所有实例共用临时目录固定文件，每次入口调用截断日志，多进程也可能
  混写；每条日志同步打开并写文件，且没有容量上限，影响诊断完整性和长期维护。
- **修改计划**：
  1. 初始化按进程幂等处理，窗口创建不清除其他窗口诊断；增加进程／窗口标识。
  2. 定义文件容量和轮转保留策略，保证多实例写入不会互相覆盖。
  3. 控制日志级别和高频写入，只在有测量依据时引入缓冲；关键错误仍要及时保存。
  4. 按诊断需要减少完整路径等敏感信息输出；日志位置变化时同步更新用户说明。
- **验收标准**：打开第二个窗口不丢失已有日志；两个进程可区分且不截断彼此记录；
  超出容量正确轮转；目录不可写不导致插件崩溃；关键失败和渲染耗时仍可诊断。
- **范围／依赖**：依赖 BUG-004；无需为小型插件引入独立日志服务。

## 交回验证的统一模板

修复 agent 在完成每单后填写以下内容，方便后续逐项复核：

```text
单据编号：BUG-xxx
状态：修复完成／待验证，或风险不成立（附证据）
基于的提交：
修复提交或补丁位置：
前置单据及对应提交：
修改文件：
问题复现与根因：
实际修改方案：
相较本单计划的偏差及原因：
验收标准逐项结果：
静态检查：passed / failed / not run
自动化测试：passed / failed / blocked / not run
Windows Release 编译：passed / failed / blocked / not run
Artifact 校验：passed / failed / not run
真实宿主测试：passed / failed / not verified
实际环境（Qt、MSVC、notepad--、OS）：
日志、截图或测试报告位置：
未验证项与已知限制：
```

单据只有在修改审查和适用验收项完成后才关闭。某项与本单无关时标记“不适用”并说明原因，
例如纯发布脚本修改不要求重复执行全部预览界面测试。

## BUG-002 交回验证

```text
单据编号：BUG-002
状态：修复完成／待验证
基于的提交：ba87695e48576e0711dadbcc2a97832b22efc5e4
修复提交或补丁位置：本记录所在提交；交回时使用 git rev-parse HEAD 核验
前置单据及对应提交：BUG-001，ba87695e48576e0711dadbcc2a97832b22efc5e4
修改文件：CMakeLists.txt；docs/architecture.md；docs/bug-backlog-2026-09-07.md；docs/bug-fix-handoffs-2026-09-07.md；src/markdown_preview_dock.cpp；src/markdown_preview_dock.h；src/preview_controller.cpp；src/preview_controller.h；tests/markdown_preview_dock_lifecycle_test.cpp；tests/preview_controller_document_identity_test.cpp
问题复现与根因：静态确认切换 A 到 B 后控制器先把活动编辑器改为 B，而 Dock 在防抖完成前仍显示 A；原双向滚动仅检查活动编辑器、Dock 可见性和同步开关，未核对预览归属。菜单与工具栏刷新也直接进入 renderNow，轮询尚未更新时可能刷新旧标签。
实际修改方案：建立活动编辑器、预览编辑器、内容版本、已渲染版本及五态模型；切换和编辑立即使旧同步关系失效；双向滚动、延迟刷新和渲染完成回调均核对身份与版本；手工刷新先重新解析当前标签；Dock 暴露明确的预览归属查询接口；新增快速切换、手工刷新、防抖和跨文档滚动测试。
相较本单计划的偏差及原因：未实现独立 HostAdapter 或导出服务，遵守本单最小状态模型边界；仍保留 120 ms 轮询作为宿主兼容兜底，事件驱动全面改造属于 BUG-015。
验收标准逐项结果：A 预览不能改变 B 滚动位置——测试源码覆盖，运行 blocked；快速切换最终只显示活动文档——版本门控和测试源码覆盖，运行 blocked；手工刷新针对当前标签——测试源码覆盖，运行 blocked；销毁和过期回调不恢复旧内容——状态门控完成，真实宿主 not verified；连续输入保持防抖——保留单次 trailing-edge 计时器并增加三次连续编辑只渲染一次的测试，运行 blocked。
静态检查：passed（git diff --check；宿主 v3.8.3 固定提交、槽和标签结构复核；ABI 与 notepad--/ 未修改）
自动化测试：blocked（新增 markdownview_document_identity_tests；CMake 在 find_package(Qt5 5.15) 因缺少 Qt5Config.cmake 失败）
Windows Release 编译：blocked（当前 Linux 环境无 Qt 5.15.2 与 MSVC v142）
Artifact 校验：not run（未生成 DLL 或 Artifact）
真实宿主测试：not verified（未在 notepad-- x64 执行 A/B 快速切换、关闭标签和滚动测试）
实际环境（Qt、MSVC、notepad--、OS）：Qt 5.15.2 开发包不可用；MSVC 不可用；notepad-- v3.8.3 / 91105f68b74382128f3313ac5af8accdc77de918；Linux x86_64
日志、截图或测试报告位置：CMake 配置错误见本次交回测试证据；测试源码 tests/preview_controller_document_identity_test.cpp；未生成截图或二进制报告
未验证项与已知限制：Qt Test 未实际编译运行；Windows Release、DLL Artifact、真实宿主 A→B→A、当前标签关闭、同步滚动及连续输入体验均待复核。后续 BUG-003 可使用 Dock 的预览身份接口，但本单未改变现有导出语义。
```

## BUG-003 交回验证

```text
单据编号：BUG-003
状态：修复完成／待验证
基于的提交：e71f14f4c067b271711c128259cf4f6e164025bb
修复提交或补丁位置：本记录所在提交；交回时使用 git rev-parse HEAD 核验
前置单据及对应提交：BUG-002，e71f14f4c067b271711c128259cf4f6e164025bb
修改文件：README.md；docs/architecture.md；docs/bug-backlog-2026-09-07.md；docs/bug-fix-handoffs-2026-09-07.md；src/markdown_preview_dock.cpp；src/markdown_preview_dock.h；src/preview_controller.cpp；src/preview_controller.h；tests/preview_controller_document_identity_test.cpp
问题复现与根因：静态确认原 activeDocument() 以原生控件可见性选择数据源，Dock 隐藏后会改取备用 QTextBrowser；showMessage() 生成的非空提示 HTML 可被误判为有效文档；菜单导出动作没有同步当前标签，也没有核对 BUG-002 建立的编辑器身份和内容版本，因此可能导出旧内容或其他标签。
实际修改方案：导出语义收敛为当前活动 Markdown 文档的最新有效内容；控制器同步当前标签并在内容未就绪时允许隐藏 Dock 下同步刷新；Dock 仅为身份和版本匹配的原生预览生成 HTML 字节快照，提示页会失效导出身份；快照和源路径在文件对话框前固定；保存交互与写入分离，取消不进入写入，QSaveFile 保持原子提交；导出动作随当前文档类型启停；补充隐藏导出、立即编辑、快速切换、提示页、动作状态和快照写入回归测试。
相较本单计划的偏差及原因：未建立独立导出服务类，而是在现有控制器和 Dock 边界内提供可测试的快照与写入接口，避免超出单据范围；未复制或嵌入相对资源，该工作仍属于 BUG-008。
验收标准逐项结果：隐藏与显示时导出对象一致——身份接口不依赖可见性并有隐藏 Dock 测试源码，运行 blocked；提示页不能导出——showMessage() 失效身份并有测试源码，运行 blocked；立即导出包含最后一次编辑——导出前同步刷新并有测试源码，运行 blocked；快速切换不串文档——导出前同步当前标签并有 A/B 测试源码，运行 blocked；取消不写文件——保存对话框空路径直接返回，写入接口空路径测试源码覆盖，运行 blocked；写入失败不破坏已有文件——继续使用 QSaveFile，测试覆盖无效快照不改已有文件及原子替换成功，实际提交失败场景未运行。
静态检查：passed（git diff --check；复核 BUG-002 身份模型、宿主 v3.8.3 同步渲染槽；ABI 与 notepad--/ 未修改）
自动化测试：blocked（扩展 markdownview_document_identity_tests；CMake 在 find_package(Qt5 5.15) 因缺少 Qt5Config.cmake 失败）
Windows Release 编译：blocked（当前 Linux 环境无 Qt 5.15.2 与 MSVC v142）
Artifact 校验：not run（未生成 DLL 或 Artifact）
真实宿主测试：not verified（未在 notepad-- x64 执行隐藏侧栏、立即编辑、快速切换、取消和写入失败测试）
实际环境（Qt、MSVC、notepad--、OS）：Qt 5.15.2 开发包不可用；MSVC 不可用；notepad-- v3.8.3 / 91105f68b74382128f3313ac5af8accdc77de918；Linux x86_64
日志、截图或测试报告位置：CMake 配置输出显示缺少 Qt5Config.cmake；测试源码 tests/preview_controller_document_identity_test.cpp；未生成截图或二进制报告
未验证项与已知限制：Qt Test 未实际编译运行；Windows Release DLL、Artifact 及真实宿主导出流程均待复核。跨目录相对图片资源仍未打包，按 BUG-008 处理；文件路径变化后的元数据刷新按 BUG-005 处理。
```

## BUG-004 交回验证

```text
单据编号：BUG-004
状态：修复完成／待验证
基于的提交：5c54499e5204376ac629ad6012f9564d888708ff
修复提交或补丁位置：9121b4b85b97b621b503ae1ba171d486eba051d0
前置单据及对应提交：BUG-001，ba87695e48576e0711dadbcc2a97832b22efc5e4
修改文件：CMakeLists.txt；docs/architecture.md；docs/bug-backlog-2026-09-07.md；docs/bug-fix-handoffs-2026-09-07.md；docs/host-compatibility.md；src/plugin_exports.cpp；src/preview_controller.cpp；src/preview_controller.h；tests/plugin_multi_window_test.cpp
问题复现与根因：静态确认原插件使用进程级 QPointer<PreviewController> g_controller；首个宿主窗口创建控制器后，同进程第二个窗口进入 NDD_PROC_MAIN 会跳过控制器和菜单初始化却返回成功。控制器的显示／隐藏快捷键使用 Qt::ApplicationShortcut，多个窗口各自初始化后也会争抢同一应用级快捷键。全局事件过滤器原先仅比较菜单父对象与当前编辑器，没有显式核对其所属宿主窗口。
实际修改方案：移除进程级全局控制器，改为从本次 notepad 宿主窗口的直接子对象中查找或创建 PreviewController；同一窗口重复入口复用已有控制器，installMenu 记录根菜单并保证幂等；每个控制器、Dock、计时器和动作继续由对应宿主窗口的 Qt 父子所有权管理；快捷键改为 Qt::WindowShortcut；右键菜单桥接增加菜单、当前编辑器和宿主窗口归属一致检查；新增独立 Qt Test 覆盖双窗口预览／导出互不影响、重复初始化、关闭一窗后另一窗继续工作及右键菜单隔离。
相较本单计划的偏差及原因：没有引入独立进程级注册表，而使用宿主窗口直接子对象作为受控注册表；该方案由 Qt 父子所有权自动清理，满足按窗口实例化与幂等要求且范围更小。未修改 getCurrentEditor 或 hostCallback 的使用方式，也未改变插件 ABI。
验收标准逐项结果：两个同进程窗口都可独立预览、刷新、导出——专用测试源码覆盖，运行 blocked；操作互不影响——预览／导出和右键菜单隔离测试源码覆盖，运行 blocked；关闭任一窗口不破坏另一窗口——QPointer 生命周期测试源码覆盖，运行 blocked；重复入口调用不增加重复动作——控制器、Dock、计时器及菜单动作计数测试源码覆盖，运行 blocked；没有快捷键歧义警告——快捷键已限定 Qt::WindowShortcut 并有断言，真实宿主警告检查 not verified。
静态检查：passed（git diff --check；复核宿主 v3.8.3 的 openFileInNewWin、quickshow、插件菜单加载与 sendParaToPlugin；ABI 与 notepad--/ 未修改）
自动化测试：blocked（新增 markdownview_multi_window_tests；CMake 在 find_package(Qt5 5.15) 因缺少 Qt5Config.cmake 失败）
Windows Release 编译：blocked（当前 Linux 环境无 Qt 5.15.2 与 MSVC v142）
Artifact 校验：not run（未生成 DLL 或 Artifact）
真实宿主测试：not verified（未在 notepad-- x64 执行在新窗口打开、双窗口快捷键、关闭任一窗口及重复插件入口测试）
实际环境（Qt、MSVC、notepad--、OS）：Qt 5.15.2 开发包不可用；MSVC 不可用；notepad-- v3.8.3 / 91105f68b74382128f3313ac5af8accdc77de918；Linux x86_64
日志、截图或测试报告位置：CMake 配置输出显示缺少 Qt5Config.cmake；测试源码 tests/plugin_multi_window_test.cpp；未生成截图或二进制报告
未验证项与已知限制：Qt Test 未实际编译运行；Windows Release DLL、Artifact 及真实宿主双窗口操作均待复核。Diagnostics::resetLog() 仍会在每次窗口入口截断共享日志，该独立问题按 BUG-016 处理；本单不能仅凭静态检查标记为已关闭。
```

## BUG-005 交回验证

```text
单据编号：BUG-005
状态：修复完成／待验证
基于的提交：ebdf8f0ab8d1c8838552d18a18e0e29ae0e53c7f
修复提交或补丁位置：本记录所在提交；交回时使用 git rev-parse HEAD 核验
前置单据及对应提交：BUG-002，e71f14f4c067b271711c128259cf4f6e164025bb；BUG-004，9121b4b85b97b621b503ae1ba171d486eba051d0
修改文件：docs/architecture.md；docs/bug-backlog-2026-09-07.md；docs/bug-fix-handoffs-2026-09-07.md；src/preview_controller.cpp；src/preview_controller.h；tests/plugin_multi_window_test.cpp；tests/preview_controller_document_identity_test.cpp
问题复现与根因：静态确认宿主 v3.8.3 在保存、另存为和重命名链路通过编辑器的 filePath 动态属性更新路径；原控制器只在标签切换、渲染或导出时临时读取该属性，同一编辑器仅改变路径而没有文本变化时不会推进内容版本或安排刷新，因此旧预览仍保持旧文件类型、标题、QTextDocument baseUrl 和导出源路径。
实际修改方案：控制器的应用事件过滤器增加仅针对当前活动编辑器和 filePath 的 DynamicPropertyChange 处理，并缓存已处理路径去重；路径变化推进 BUG-002 内容版本、失效旧预览身份、立即更新标题与导出动作，Dock 可见时沿用防抖刷新，隐藏时由后续显示或导出同步刷新；原生预览重新接入时沿用 adoptNativePreview 在宿主更新前写入新 baseUrl；编辑器切换或销毁时同步替换或清空路径缓存，其他窗口和旧编辑器事件因身份检查被忽略；补充中文／空格目录、md→txt、未命名→md、导出路径和多窗口隔离回归测试。
相较本单计划的偏差及原因：没有为每个编辑器再安装一层对象事件过滤器，而复用 BUG-004 已存在的应用级事件过滤器并以 watched == m_editor 严格限定当前窗口活动编辑器；这样不会引入重复事件分发，切换或销毁后旧编辑器自然不再匹配。保留 120 ms 轮询用于标签切换兼容兜底，但路径更新不依赖轮询。
验收标准逐项结果：无需额外键入或切换标签即可更新——DynamicPropertyChange 同步使旧状态失效并安排刷新，测试源码覆盖，运行 blocked；扩展名变化及时改变预览状态——md→txt 立即禁用导出并使旧预览失效，测试源码覆盖，运行 blocked；新目录图片正确——路径刷新前更新 QTextDocument baseUrl，中文／空格新目录断言已加入，运行 blocked；中文／空格路径正常——QTemporaryDir 下中文及空格目录测试源码覆盖，运行 blocked；多窗口只更新自己的状态——独立窗口回归测试验证首窗改为 txt 不影响第二窗导出与渲染，运行 blocked。
静态检查：passed（git diff --check；宿主 v3.8.3 / 91105f68 的 filePath 属性设置路径复核；仅处理当前编辑器的 DynamicPropertyChange；ABI 与 notepad--/ 未修改）
自动化测试：blocked（扩展 markdownview_document_identity_tests 和 markdownview_multi_window_tests；cmake -S . -B /tmp/markdownview-bug005-build -DBUILD_TESTING=ON 在 find_package(Qt5 5.15) 因缺少 Qt5Config.cmake 失败）
Windows Release 编译：blocked（当前 Linux 环境无 Qt 5.15.2 与 MSVC v142）
Artifact 校验：not run（未生成 DLL 或 Artifact）
真实宿主测试：not verified（未在 notepad-- x64 执行重命名、另存目录、未命名保存、中文／空格路径和双窗口路径变化测试）
实际环境（Qt、MSVC、notepad--、OS）：Qt 5.15.2 开发包不可用；MSVC 不可用；notepad-- v3.8.3 / 91105f68b74382128f3313ac5af8accdc77de918；Linux x86_64
日志、截图或测试报告位置：CMake 配置目录 /tmp/markdownview-bug005-build；测试源码 tests/preview_controller_document_identity_test.cpp、tests/plugin_multi_window_test.cpp；未生成截图或二进制报告
未验证项与已知限制：Qt Test 未实际编译运行；Windows Release DLL、Artifact 及真实宿主中的文件重命名／另存为、同名相对图片目录切换、中文／空格路径和多窗口隔离均待复核。跨目录 HTML 导出资源打包仍按 BUG-008 处理；本单不能仅凭静态检查标记为已关闭。
```

## BUG-006 交回验证

```text
单据编号：BUG-006
状态：修复完成／待验证
基于的提交：b1fd83a22e0f3c066c7f0f4f2206ea576cd3b1b3
修复提交或补丁位置：本记录所在提交；交回时使用 git rev-parse HEAD 核验
前置单据及对应提交：BUG-002，e71f14f4c067b271711c128259cf4f6e164025bb
修改文件：docs/architecture.md；docs/bug-backlog-2026-09-07.md；docs/bug-fix-handoffs-2026-09-07.md；src/markdown_preview_dock.cpp；src/markdown_preview_dock.h；src/preview_controller.cpp；tests/preview_controller_document_identity_test.cpp
问题复现与根因：静态确认实际预览通过宿主 on_updataMarkdown 调用 QTextEdit::setMarkdown，文档重建会重置预览滚动位置；原有 previousRatio 恢复仅存在于未被正常原生渲染链路调用的备用 renderMarkdown，因此关闭同步滚动后自动或手工刷新可能跳回顶部，富文本延迟布局还会再次改变滚动范围。
实际修改方案：控制器在原生渲染前按当前编辑器捕获预览滚动比例；渲染成功且身份和内容版本仍匹配时交给 Dock 保存恢复目标。Dock 在短计时器到期时恢复位置，并在 QTextEdit 滚动范围因延迟布局继续变化时重新应用；比例限制在 0～1。切换文档、预览销毁或重新开启同步会取消旧目标；恢复时屏蔽滚动条信号，避免形成反向反馈。新增测试覆盖自动刷新、手工刷新、两阶段延迟布局、文档切换隔离及重新开启同步后的编辑器主导行为。
相较本单计划的偏差及原因：保留备用 renderMarkdown 的现有恢复逻辑，因为它仍是错误提示之外的独立备用渲染能力；本次明确把等价且带身份门控的恢复接入真实原生路径，没有引入精确源行到 Markdown 节点映射。
验收标准逐项结果：同步关闭刷新不跳顶——自动与手工刷新测试源码覆盖，运行 blocked；同步开启保持原功能——重新开启后编辑器 80% 位置驱动预览的测试源码覆盖，运行 blocked；切换文档不继承位置——A 的 50% 目标不会应用到 B，测试源码覆盖，运行 blocked；长文档布局后稳定——模拟两次延迟 rangeChanged 后仍恢复 75%，测试源码覆盖，运行 blocked；恢复不造成反向反馈——恢复使用 QSignalBlocker 且预览到编辑器仍只响应 actionTriggered，静态 passed，真实交互 not verified。
静态检查：passed（git diff --check；真实原生 on_updataMarkdown 路径已接入；身份／版本门控、范围限制和旧目标取消已复核；ABI 与 notepad--/ 未修改）
自动化测试：blocked（扩展 markdownview_document_identity_tests；CMake 在 find_package(Qt5 5.15) 因缺少 Qt5Config.cmake 配置失败）
Windows Release 编译：blocked（当前 Linux 环境无 Qt 5.15.2 与 MSVC v142）
Artifact 校验：not run（未生成 DLL 或 Artifact）
真实宿主测试：not verified（未在 notepad-- x64 执行同步关闭后的自动刷新、手工刷新、图片延迟布局、内容缩短、标签切换和重新开启同步测试）
实际环境（Qt、MSVC、notepad--、OS）：Qt 5.15.2 开发包不可用；MSVC 不可用；notepad-- v3.8.3 / 91105f68b74382128f3313ac5af8accdc77de918；Linux x86_64
日志、截图或测试报告位置：CMake 配置目录 /tmp/markdownview-bug006-build；测试源码 tests/preview_controller_document_identity_test.cpp；未生成截图或二进制报告
未验证项与已知限制：Qt Test 未实际编译运行；Windows Release DLL、Artifact 及真实宿主中的阅读位置保持和图片延迟布局均待复核。阅读位置按滚动比例保持，不提供精确源行或 Markdown 节点映射；后续 BUG-015 仍需处理更广泛的滚动缓存与范围变化架构问题。
```

## BUG-007 交回验证

```text
单据编号：BUG-007
状态：修复完成／待验证
基于的提交：1868f3e6a117a794297cd538215df7d8b0dda635
修复提交或补丁位置：本记录所在提交；交回时使用 git rev-parse HEAD 核验
前置单据及对应提交：BUG-002，e71f14f4c067b271711c128259cf4f6e164025bb
修改文件：docs/architecture.md；docs/bug-backlog-2026-09-07.md；docs/bug-fix-handoffs-2026-09-07.md；src/markdown_preview_dock.cpp；src/markdown_preview_dock.h；tests/markdown_preview_dock_lifecycle_test.cpp
问题复现与根因：静态确认备用 QTextBrowser 的 anchorClicked 已连接 openLink()，但正常预览实际显示宿主 MarkdownView 内只读 QTextEdit；adoptNativePreview() 只接入样式和滚动条，没有给该控件安装链接激活入口，所以外部链接、相对链接和页内锚点均绕过插件逻辑。
实际修改方案：在原生 QTextEdit viewport 上安装 Dock 局部事件过滤器，仅把同一链接上的无修饰左键短点击且未形成选区识别为激活；纯片段链接扫描当前文档命名锚点并调整原生预览滚动条；相对链接按当前 Markdown 文件目录解析；外部协议限定为 http、https、mailto、file；不支持协议和打开失败写诊断日志并在文档栏反馈；QDesktopServices 调用封装为可注入 UrlOpener，测试仅记录 URL。扩展 lifecycle Qt Test 覆盖原生点击、相对解析、页内锚点、协议拒绝和拖选不误开。
相较本单计划的偏差及原因：未替换宿主原生 QTextEdit，也未引入浏览器级控件；采用 viewport 事件过滤器作为最小局部适配。页内锚点通过 QTextDocument 的命名锚点定位，不增加源代码行到节点映射。
验收标准逐项结果：支持的外部链接能打开——注入打开器记录 HTTPS 请求，测试源码覆盖，运行 blocked；有效锚点跳转正确——原生文档命名锚点定位测试源码覆盖，运行 blocked；相对路径与当前文档对应——/tmp/docs/current.md 下 guide/next.md 解析断言覆盖，运行 blocked；不支持协议不被打开——javascript 协议拒绝且显示反馈的测试源码覆盖，运行 blocked；文字选择、复制、滚动保持正常——拖选形成选区且打开次数为零的测试源码覆盖，复制和滚轮因事件过滤器不拦截对应事件而静态 passed，真实交互 not verified。
静态检查：passed（git diff --check；核对原生 QTextEdit、事件范围、协议白名单、相对 baseUrl、ABI 与 notepad--/ 未修改）
自动化测试：blocked（扩展 markdownview_lifecycle_tests；cmake -S . -B /tmp/markdownview-bug007-build -DBUILD_TESTING=ON 在 find_package(Qt5 5.15) 因缺少 Qt5Config.cmake 失败）
Windows Release 编译：blocked（当前 Linux 环境无 Qt 5.15.2 与 MSVC v142）
Artifact 校验：not run（未生成 DLL 或 Artifact）
真实宿主测试：not verified（未在 notepad-- x64 点击 HTTPS、mailto、file、相对文档、页内锚点及执行拖选／复制／滚轮测试）
实际环境（Qt、MSVC、notepad--、OS）：Qt 5.15.2 开发包不可用；MSVC 不可用；notepad-- v3.8.3 / 91105f68b74382128f3313ac5af8accdc77de918；Linux x86_64
日志、截图或测试报告位置：CMake 配置目录 /tmp/markdownview-bug007-build；测试源码 tests/markdown_preview_dock_lifecycle_test.cpp；未生成截图或二进制报告
未验证项与已知限制：Qt Test 未实际编译运行；Windows Release DLL、Artifact 和真实宿主链接交互均待复核。只处理 QTextDocument 可识别的命名锚点和明确允许协议，不支持脚本执行、浏览器导航历史或源行级定位。
```

## BUG-008 交回验证

```text
单据编号：BUG-008
状态：修复完成／待验证
基于的提交：3e2b00848101fb2f9074761e446962f593897efd
修复提交或补丁位置：本记录所在提交；交回时使用 git rev-parse HEAD 核验
前置单据及对应提交：BUG-003，5c54499e5204376ac629ad6012f9564d888708ff；BUG-005，b1fd83a22e0f3c066c7f0f4f2206ea576cd3b1b3
修改文件：README.md；docs/architecture.md；docs/bug-backlog-2026-09-07.md；docs/bug-fix-handoffs-2026-09-07.md；src/markdown_preview_dock.cpp；tests/markdown_preview_dock_lifecycle_test.cpp
问题复现与根因：静态确认 BUG-003 固定的 QTextDocument::toHtml() 快照保留相对图片 src；即使预览依靠 BUG-005 更新后的 baseUrl 正确显示，导出到其他目录或移动 HTML 后，浏览器仍会从导出目录解析该相对路径并丢失图片。
实际修改方案：采用单文件导出策略，只在固定 HTML 字符串副本中扫描 img src；相对路径和 file: 本地图片按快照文档 baseUrl 解析，读取成功且 MIME 为 image/* 时嵌入 Base64 data URL。远程图片、data URL、其他协议和普通超链接保持原样，不下载或打包。缺失、不可读或非图片资源保留原引用，在 HTML 注释和诊断日志中列出。实时 QTextDocument 不修改，保存仍复用 QSaveFile 原子写入。
相较本单计划的偏差及原因：采用建议的内嵌策略，没有建立伴随资源目录，因此不存在目录命名、同名覆盖、部分复制清理或无关资源损坏问题。未设置图片体积上限，以保证单文件迁移语义；README 和架构文档明确 Base64 会增加文件体积。
验收标准逐项结果：跨目录导出正确显示本地图片——中文及空格路径 PNG 被转为 data:image/png;base64 且写入其他目录的测试源码覆盖，运行 blocked；可迁移策略符合文档——README 与架构文档已说明单文件内嵌、远程资源和体积语义，静态 passed；原预览不变——测试保存转换前后 document()->toHtml() 并比较相等，运行 blocked；资源缺失有明确结果——保留原 src、插入 markdownview-export 注释并写诊断日志，测试源码覆盖，运行 blocked；保存失败不破坏已有文件和无关资源——继续使用 BUG-003 的 QSaveFile，既有原子写入测试源码覆盖，运行 blocked，本单不创建资源目录。
静态检查：passed（git diff --check；核对只重写固定快照中的 img src；远程图片和普通链接不打包；ABI 与 notepad--/ 未修改；宿主参考基线 v3.8.3 / 91105f68）
自动化测试：blocked（扩展 markdownview_lifecycle_tests；cmake -S . -B /tmp/markdownview-bug008-build -DBUILD_TESTING=ON 在 find_package(Qt5 5.15) 因缺少 Qt5Config.cmake 失败）
Windows Release 编译：blocked（当前 Linux 环境无 Qt 5.15.2 与 MSVC v142）
Artifact 校验：not run（未生成 DLL 或 Artifact）
真实宿主测试：not verified（未在 notepad-- x64 执行跨目录导出、移动 HTML、中文／空格图片、缺失图片、远程图片及大图片体积测试）
实际环境（Qt、MSVC、notepad--、OS）：Qt 5.15.2 开发包不可用；MSVC 不可用；notepad-- v3.8.3 / 91105f68b74382128f3313ac5af8accdc77de918；Linux x86_64
日志、截图或测试报告位置：CMake 配置目录 /tmp/markdownview-bug008-build；测试源码 tests/markdown_preview_dock_lifecycle_test.cpp；未生成截图或二进制报告
未验证项与已知限制：Qt Test 未实际编译运行；Windows Release DLL、Artifact 和真实宿主跨目录导出均待复核。远程图片仍依赖查看 HTML 时的网络可达性；缺失本地图片不会使整个导出失败，而是保留引用并明确记录；大型图片会按 Base64 增大 HTML 文件。
```

## BUG-009 交回验证

```text
单据编号：BUG-009
状态：修复完成／待验证
基于的提交：6d47180c3a10fae8ae18720d63e4af53e8851625
修复提交或补丁位置：本记录所在提交；交回时使用 git rev-parse HEAD 核验
前置单据及对应提交：BUG-005，b1fd83a22e0f3c066c7f0f4f2206ea576cd3b1b3；BUG-006，1868f3e6a117a794297cd538215df7d8b0dda635
修改文件：docs/architecture.md；docs/bug-backlog-2026-09-07.md；docs/bug-fix-handoffs-2026-09-07.md；src/markdown_preview_dock.cpp；src/markdown_preview_dock.h；src/preview_controller.cpp；tests/markdown_preview_dock_lifecycle_test.cpp
问题复现与根因：静态确认主路径由宿主 QTextEdit::setMarkdown() 先生成文档，adoptNativePreview() 随后才调用 setDefaultStyleSheet()；该 API 不能追溯性地把浏览器 CSS 完整应用到已经生成的 Markdown 字符与块格式。复用预览时样式又发生在 on_updataMarkdown() 之前，随后文档重建会覆盖格式。代码未处理 palette 或 style 变化，因此主题切换只可能改变外层控件，正文链接、代码、引用和表格没有一致刷新保证。
实际修改方案：保留宿主 Qt 5.15.2 Markdown 解析器，在其渲染结果上增加不重解析源文本的文档格式后处理。按 QTextFormat 的标题级别、引用级别、代码围栏／语言属性、等宽片段和锚点设置标题、引用、代码与链接；递归处理 QTextTable 的边框、留白和首行背景；控件 palette 统一正文、背景及明暗主题颜色。首次创建在 adopt 时处理，已有预览在 on_updataMarkdown 返回后处理。Dock 合并 PaletteChange、ApplicationPaletteChange、StyleChange，在零延迟计时器中仅刷新当前身份／版本匹配的文档；刷新前后保存并恢复滚动比例。新增 Qt Test 输入标题、引用、行内／围栏代码、链接和表格，并验证暗色 palette 更新及阅读位置不变。
相较本单计划的偏差及原因：没有替换宿主渲染器，也不承诺完整浏览器 CSS；采用 Qt 富文本文档公开属性可表达的明确支持范围。未生成前后截图，因为当前环境缺少 Qt 运行与 Windows 宿主条件，截图和实际视觉差异保留为真实宿主待验证项。
验收标准逐项结果：首次与刷新后样式一致——初次 adopt 与 on_updataMarkdown 后共用 applyDocumentStyle，静态 passed，自动化运行 blocked；明暗主题下文字、链接和代码可读——palette 驱动 Base/Text/AlternateBase/Link 并有暗色测试源码，运行 blocked、真实视觉 not verified；相关块格式符合支持范围——标题、引用、围栏／行内代码、表格和链接后处理已实现，测试源码覆盖代表性文档，运行 blocked；提供前后截图——not verified，当前环境无法生成真实宿主截图；主题切换不串文档、不改变阅读位置——身份／版本门控且按比例恢复，测试源码覆盖位置保持，运行 blocked，真实多标签主题切换 not verified。
静态检查：passed（git diff --check；首次／后续渲染时序、主题事件合并、身份／版本门控和不调用宿主解析路径已复核；ABI 与 notepad--/ 未修改）
自动化测试：blocked（扩展 markdownview_lifecycle_tests；cmake -S . -B /tmp/markdownview-bug009-build -DBUILD_TESTING=ON 在 find_package(Qt5 5.15) 因缺少 Qt5Config.cmake 失败）
Windows Release 编译：blocked（当前 Linux 环境无 Qt 5.15.2 与 MSVC v142）
Artifact 校验：not run（未生成 DLL 或 Artifact）
真实宿主测试：not verified（未在 notepad-- x64 检查首次打开、手工刷新、明暗主题、多标签和阅读位置，也未生成前后截图）
实际环境（Qt、MSVC、notepad--、OS）：Qt 5.15.2 开发包不可用；MSVC 不可用；notepad-- v3.8.3 / 91105f68b74382128f3313ac5af8accdc77de918；Linux x86_64
日志、截图或测试报告位置：CMake 配置目录 /tmp/markdownview-bug009-build；测试源码 tests/markdown_preview_dock_lifecycle_test.cpp；未生成截图或二进制报告
未验证项与已知限制：Qt Test 未实际编译运行；Windows Release DLL、Artifact、真实宿主明暗主题视觉、嵌入 HTML 细节及前后截图均待复核。支持范围限于 Qt QTextDocument 可表达的标题、引用、代码、表格、链接和控件 palette，不等价于完整浏览器 CSS；后续 BUG-011 仍需单独测量大文档格式遍历与总体渲染耗时。
```
