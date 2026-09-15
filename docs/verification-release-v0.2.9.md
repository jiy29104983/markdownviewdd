# v0.2.9 发布验证记录

## 验证结果

| 验证项 | 状态 | 证据与范围 |
| --- | --- | --- |
| 静态检查 | passed | 发布准备的版本一致性、文档本地链接与 diff 空白检查通过 |
| 自动化测试 | passed | 本地八组 Qt 回归、Windows 标签流水线八组 Qt 回归与发布脚本隔离测试通过 |
| Windows Release 编译 | passed | Windows 2022、Qt 5.15.2、VS 2022／MSVC v142，x64 |
| Artifact 校验 | passed | 公开 ZIP 与 SHA256 下载成功，校验和、三项文件清单、DLL PE x64 与 README 内容通过 |
| 真实宿主测试 | passed | 维护者明确确认：发布前已手动测试，使用无问题；未逐项提供 DPI、多窗口等专项测试记录，不扩大结论范围 |
| Release 发布 | passed | 正式标签流水线构建与发布 job 均成功 |

[正式流水线](https://github.com/jiy29104983/markdownviewdd/actions/runs/34950791883)；
[公开 Release](https://github.com/jiy29104983/markdownviewdd/releases/tag/v0.2.9)。

公开 ZIP SHA256：

```text
2c12e00b711a1f96634d73828a302f86b713fe58243679b5fb67f274d9da0738
```

## 发布说明更正

此前将缺少代理执行记录误写成真实宿主尚未验证。维护者于本次发布后的会话中澄清，
其在要求发布前已完成手动确认。现按该确认更正公开说明，仅修改文案，标签及发布附件保持不变。

本次仅修改文档，检查 Markdown 本地链接及 `git diff --check`；不重跑 DLL 编译或宿主测试。
