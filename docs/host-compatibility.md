# notepad-- 宿主兼容性

## 兼容性基线

截至 2026 年 9 月 4 日，本插件兼容性基线为 notepad-- `v3.8.3`，固定提交为
`91105f68b74382128f3313ac5af8accdc77de918`。该版本于 2026 年 8 月 5 日作为预览
release 发布；截至同一日期，`v3.8.2` 是 notepad-- 的最新稳定版。

宿主源码以自主发布仓库 Gitee 的标签和提交为准。GitHub 镜像的 3.8.x 标签未同步到
相同提交，不应作为本插件的兼容性基线。上述记录是带日期的已验证基线，不代表未来
一直是 notepad-- 的最新版本。

## 本地宿主源码

`notepad--/` 是已忽略的本地宿主源码参考目录，不属于本插件的提交内容。除非任务明确
要求修改宿主，否则不要编辑或提交其中内容。

重点参考文件：

- `notepad--/src/scintillaeditview.cpp`：编辑器、右键菜单以及
  `on_viewMarkdown()`、`on_updataMarkdown()` 的实现。
- `notepad--/src/markdownview.*`：宿主原生 Markdown 预览窗口。
- `notepad--/src/include/pluginGl.h` 与 `notepad--/src/plugin.h`：插件 ABI 和加载入口
  定义。

使用以下命令确认本地参考源码和预览调用：

```bash
git -C notepad-- describe --tags --exact-match HEAD
git -C notepad-- rev-parse HEAD
rg -n "on_viewMarkdown|on_updataMarkdown" notepad--/src
```

前两条命令应分别返回 `v3.8.3` 和
`91105f68b74382128f3313ac5af8accdc77de918`。

## ABI 安全边界

`src/ndd_plugin_api.h`、`src/plugin_exports.cpp` 中的导出函数签名，以及对应的宿主
结构体和回调类型都属于 ABI 高风险区域。除非任务明确要求适配新的宿主版本，否则不要
改变字段顺序、字段类型、调用约定或导出签名。

检查兼容性时，应逐项对照 Gitee 基线中的 `src/include/pluginGl.h` 和 `src/plugin.h`，
不能只比较名称或依赖宽泛的文本差异。组件关系、宿主调用方式和当前 ABI 设计说明见
[`architecture.md`](architecture.md)。

发布 DLL 还必须匹配宿主的 CPU 架构、MSVC 工具链和运行库、Qt 主次版本，以及插件
依赖的窗口对象名和元对象槽。当前目标为 x64、Qt 5.15.2 和 MSVC v142。

## 验证与基线更新

兼容性结论应分别记录以下证据，不得互相替代：

- 宿主源码、结构体字段、回调签名和元对象槽的静态检查。
- Windows Release DLL 编译结果。
- 打包文件、CPU 架构和依赖检查。
- DLL 在目标 notepad-- x64 宿主中的实际加载和功能测试。

升级兼容性基线时，应重新核对官方 Gitee release、标签和提交，检查上述 ABI 与宿主调用
点，并同步更新本文件、README 中的运行环境说明及受影响的构建或发布文档。不能把仅有
静态检查或编译成功的版本记录为已经完成真实宿主兼容性验证。
