# Windows 构建与 Release 发布

本文档是维护者使用的详细发布手册。`.github/workflows/windows-release.yml` 是云端
构建和发布的唯一入口，固定使用 GitHub `windows-2022`、Qt 5.15.2
`win64_msvc2019_64`、Visual Studio 2022 和 MSVC v142，不要求本机安装 Windows
编译环境。

第三方 Action 应继续固定到完整提交 SHA；升级 Action 前先核对其官方 release、运行时
要求和 GitHub runner 支持情况。

## 触发普通构建

以下操作会运行 `Build plugin` job，并在独立步骤执行回归测试，但不会创建 GitHub Release：

- 向 `main` 分支推送提交。
- 创建或更新目标分支为 `main` 的 Pull Request。
- 在 GitHub 仓库的 `Actions` 页面选择 `Windows build and release`，点击
  `Run workflow`，并选择 `main`。
- 已安装并登录 GitHub CLI 时，执行：

  ```bash
  gh workflow run windows-release.yml --ref main
  ```

只有 Windows Release 构建与回归测试步骤均成功后，工作流才会打包。Actions 运行页面会提供保存 14 天的
`markdownviewdd-v<版本>-windows-x64-<短提交号>` Artifact。产物中包含 ZIP 和
SHA256 文件；ZIP 内应只有：

```text
plugin/markdownviewdd.dll
README.md
LICENSE
```

发布前应下载候选 Artifact，验证校验和，并在兼容性基线指定的 notepad-- x64 宿主中
完成手工测试。云端编译成功不能替代真实宿主验证。本地统一测试命令和各测试的覆盖边界见
[testing.md](testing.md)。

## 发布新版本

版本标签必须严格使用 `vMAJOR.MINOR.PATCH`，例如 `v0.2.8`。推送任何 `v*` 标签都会
触发工作流，但格式不正确或标签版本与源码不一致时，版本校验会主动失败。不要先打标签
再修改源码。

### 1. 准备主分支

确认位于最新的 `main`，且没有未提交改动：

```bash
git switch main
git pull --ff-only origin main
git status --short --branch
```

### 2. 同步版本号

设置本次版本号，以下示例发布 `0.2.8`：

```bash
VERSION=0.2.8
```

同步修改三个版本来源：

- `CMakeLists.txt`：`project(ndd-markdown-view VERSION ...)`
- `markdownview.pro`：`NDD_MARKDOWN_VIEW_VERSION=...`
- `src/ndd_plugin_api.h`：后备 `NDD_MARKDOWN_VIEW_VERSION "..."`

如果 README 或其他用户文档写有当前版本，也要同步更新。用以下命令检查遗漏：

```bash
rg -n "project\(ndd-markdown-view VERSION|NDD_MARKDOWN_VIEW_VERSION|当前版本" \
  CMakeLists.txt markdownview.pro src README.md
```

### 3. 编写中文 Release Notes

每个正式版本必须在打标签前新增一份中文说明，路径固定为：

```text
docs/releases/v<版本>.md
```

例如 `0.2.8` 对应 `docs/releases/v0.2.8.md`。文件名必须包含标签使用的前缀 `v`，并与
最终标签完全一致。Release Notes 至少应包含：

- 版本标题和本次发布的重点。
- “相比上一版本的改进”，按用户可感知的功能、性能、稳定性和修复内容编写。
- 下载、校验和安装方法。
- 支持的 notepad--、Windows 架构及 Qt 运行库版本。
- 简洁的发布验证结论，以云端结果和维护者确认的真实宿主测试结果为依据。

发布前必须先向维护者确认真实宿主手动测试结果；当前会话已经明确确认的，不重复询问。
不能把“代理未执行／没有本地记录”当成“维护者未测试”。未获确认时，先完成文档与自动化
验证，再向维护者确认后发布，不得擅自将未确认状态写入对外 Release 并直接发布。

静态检查、自动化测试、Windows 编译、Artifact 和真实宿主测试的分层状态记入仓库验证文档；
未完成项如实使用 `not run`／`not verified`。对外 Release 面向使用者，简述已确认的结果，
不复制内部候选进度表。维护者的手动确认是有效证据，但不能扩写成未明确确认的 DPI、
多窗口或其他专项测试均已通过。

说明应面向插件使用者，不要直接复制提交列表，也不要写入本机路径、账号、Token、内部
排查记录或无法由当前版本证据支持的结论。可参考已发布的
[`v0.2.8` 中文说明](releases/v0.2.8.md)。打标签前检查文件存在且非空：

```bash
test -s "docs/releases/v${VERSION}.md"
```

发布脚本会优先把该文件原样用作 GitHub Release 正文。脚本保留缺少版本说明时使用 GitHub
自动生成说明的兼容回退，但正常正式发布不得依赖该回退；发现说明文件缺失时应停止发布准备，
补齐中文内容并先提交到 `main`。

### 4. 生成并验证候选 Artifact

提交版本变更并先推送 `main`，让普通构建生成候选 Artifact：

```bash
git diff --check
git add CMakeLists.txt markdownview.pro src/ndd_plugin_api.h README.md \
  "docs/releases/v${VERSION}.md"
git commit -m "chore(release): prepare v${VERSION}"
git push origin main
```

`README.md` 没有版本文字变化时不要强行加入提交。等待 `Build plugin` job 成功，下载
Artifact，验证 SHA256，并按仓库测试要求在 notepad-- 中完成加载和功能测试。

至少分别记录以下结果：

- 云端 Release 编译。
- Artifact 下载与 SHA256 校验。
- ZIP 内容检查。
- Windows/notepad-- 真实加载与功能测试。

### 5. 确认标签未被占用

确认同名标签在本地和远端都不存在：

```bash
git tag --list "v${VERSION}"
git ls-remote --tags origin "refs/tags/v${VERSION}" "refs/tags/v${VERSION}^{}"
```

两条命令都不应返回标签。已存在的发布标签不得移动、覆盖或复用。

### 6. 创建并推送标签

只在候选构建和手工测试均通过的提交上创建注释标签：

```bash
git status --short --branch
test -s "docs/releases/v${VERSION}.md"
git tag -a "v${VERSION}" -m "Release v${VERSION}"
git push origin "v${VERSION}"
```

### 7. 自动发布

标签工作流会自动执行以下操作，无需手工创建 Release，也无需配置 PAT 或 Secret：

- 校验标签版本与三个源码版本一致。
- 重新构建 Release x64 DLL。
- 生成 `markdownviewdd-v<版本>-windows-x64.zip`。
- 生成同名 `.zip.sha256` 校验文件。
- 使用仓库 `GITHUB_TOKEN` 创建非草稿、非预发布的 GitHub Release。
- 如果存在 `docs/releases/v<版本>.md`，使用其中的中文内容作为 Release 正文；只有旧标签或
  非标准兼容场景缺少该文件时，才回退到 GitHub 自动生成说明。

发布附件一旦公开即视为不可变。同一标签的工作流重跑遵循以下规则：

- ZIP 和 SHA256 均已存在且内容逐字节相同：视为幂等成功，跳过上传。
- 仅缺少部分附件：先验证已有同名附件内容一致，再只上传缺失附件，不覆盖已有附件。
- 任一同名附件内容不同：发布失败；保留原标签和附件，修复后发布新的补丁版本。
- Release 查询明确返回 HTTP 404：创建 Release；网络、权限、限流等其他查询错误直接失败，
  不得按“Release 不存在”处理。

不要使用 `gh release upload --clobber` 恢复公开发布。若附件缺失，可在确认现有附件与该标签
构建结果一致后重跑标签工作流；若内容不一致，必须人工调查并发布新版本。

### 8. 验证公开 Release

在 Actions 页面确认 `Build plugin` 和 `Publish GitHub Release` 两个 job 均为
`success`，再在 Releases 页面确认中文正文完整显示，ZIP 与 SHA256 两个附件均可下载。
正文应明确列出相较上一版本的改进和实际验证边界。安装 GitHub CLI 后也可执行：

```bash
gh run list --workflow windows-release.yml --limit 5
gh release view "v${VERSION}"
gh release view "v${VERSION}" --json body --jq .body
gh release download "v${VERSION}" --pattern "*.zip" --pattern "*.sha256"
sha256sum -c "markdownviewdd-v${VERSION}-windows-x64.zip.sha256"
unzip -l "markdownviewdd-v${VERSION}-windows-x64.zip"
```

## 发布失败处理

- 网络、缓存或 runner 临时故障：在 Actions 页面重跑失败的 job，不要新建或移动标签。
- 标签版本校验失败：不要强推标签。修正 `main` 后使用新的补丁版本发布；只有标签尚未
  公开、没有 Release，且用户明确同意时，才能删除并重建错误标签。
- Release 已经公开后发现问题：保留原标签和附件，修复代码并发布下一个补丁版本。
- Release Notes 缺失、不是中文或与实际改动不符：不要创建标签；补充
  `docs/releases/v<版本>.md`，提交并等待候选流水线重新成功。Release 已公开后需要修正文案时，
  只编辑 Release 正文，不替换或覆盖已发布附件。
- 必须分别记录云端编译、Artifact 校验、Windows/notepad-- 手工测试和 Release 发布
  的结果；不能用其中一项成功代替其他项。

## 验证记录

2026 年 9 月 4 日，`v0.2.7` 完成首次发布流程端到端验证，包括云端 Windows 编译、
版本校验、Release 创建、公开附件重新下载、SHA256 校验和 DLL x86-64 格式检查。
该记录不替代后续版本各自的候选构建和真实宿主测试。

2026 年 9 月 7 日，`v0.2.8` 首次使用 `docs/releases/v0.2.8.md` 作为中文 Release
正文完成发布，并验证公开正文、ZIP、SHA256、压缩包内容和 DLL PE32+ x86-64 格式。
该版本的 GitHub Actions Windows 构建和自动化回归测试通过；真实 notepad-- 宿主测试仍为
`not verified`。


2026 年 9 月 15 日，`v0.2.9` 正式发布后，维护者明确确认发布前已完成真实宿主手动测试，
使用无问题；据此更正此前错误标注的未验证状态。分层结果见
[v0.2.9 发布验证记录](verification-release-v0.2.9.md)。
