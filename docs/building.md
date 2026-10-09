# Windows 构建指南

[返回项目首页](../README.md)

本项目面向 notepad-- v3.8.3 x64，使用 C++14 和 Qt。只需使用插件时，可直接下载 [Release 发布包](https://github.com/jiy29104983/markdownviewdd/releases)，无需安装开发工具。

## 准备环境

- Visual Studio 2019 或 Visual Studio Build Tools 2019，安装 MSVC v142 x64 工具集。
- Qt 5.15.2 `msvc2019_64`，包含 Core、Gui、Widgets 和 Test 模块。
- CMake 3.16 或更高版本。

Visual Studio 2022 也可使用，但需额外安装 v142 工具集，并使用 CMake 3.21 或更高版本。VS Code 可以作为编辑器，编译仍依赖上述工具和 Qt。

## 使用构建脚本

在仓库根目录运行 `build-windows.bat`，脚本默认使用：

| 配置 | 默认值 |
| --- | --- |
| Qt 路径 | `C:\Qt\5.15.2\msvc2019_64`；若已设置 `QTDIR`，优先使用该变量 |
| 生成器 | Visual Studio 2019，x64 |
| 构建类型 | Release |

也可从 PowerShell 指定 Qt 路径：

```powershell
.\scripts\build-windows.ps1 -QtRoot "D:\Qt\5.15.2\msvc2019_64"
```

需要删除旧构建目录并重新构建时，加上 `-Clean`：

```powershell
.\scripts\build-windows.ps1 -QtRoot "D:\Qt\5.15.2\msvc2019_64" -Clean
```

使用 Visual Studio 2022 与 v142 工具集：

```powershell
.\scripts\build-windows.ps1 `
  -QtRoot "D:\Qt\5.15.2\msvc2019_64" `
  -Generator "Visual Studio 17 2022" `
  -Toolset "v142"
```

构建成功后，产物位于 `build\plugin\markdownviewdd.dll`。脚本同时构建测试程序；运行测试的方法见[测试指南](testing.md)。

## 手动使用 CMake

在仓库根目录打开“x64 Native Tools Command Prompt for VS 2019”，按实际安装位置设置 Qt 路径：

```bat
set "QTDIR=C:\Qt\5.15.2\msvc2019_64"
set "PATH=%QTDIR%\bin;%PATH%"

cmake -S . -B build ^
  -G "Visual Studio 16 2019" -A x64 ^
  -DCMAKE_PREFIX_PATH="%QTDIR%"

cmake --build build --config Release
```

默认启用测试构建，因此需要 Qt Test 模块。产物同样位于 `build\plugin\markdownviewdd.dll`。

## 使用 qmake

在已加载 MSVC v142 x64 环境的命令提示符中执行：

```bat
set "QTDIR=C:\Qt\5.15.2\msvc2019_64"
set "PATH=%QTDIR%\bin;%PATH%"

qmake markdownview.pro
nmake release
```

产物位于 `build\plugin\markdownviewdd.dll`。qmake 配置仅构建插件，统一回归测试使用 CMake，见[测试指南](testing.md)。

## 安装与验证

关闭 notepad--，将 DLL 复制到 `Notepad--.exe` 同级的 `plugin` 目录，然后重启编辑器。完整安装步骤见[项目首页](../README.md#安装与开始使用)。

构建成功后，还需在目标 notepad-- 版本中验证加载和实际功能。接口基线见[宿主兼容性说明](host-compatibility.md)。

## GitHub Actions

仓库的 [Windows build and release](../.github/workflows/windows-release.yml) 工作流会构建 Windows x64 插件、运行测试并生成带 SHA256 校验文件的压缩包。

向 `main` 推送、向 `main` 创建 Pull Request 或手动运行工作流可生成构建 Artifact；推送 `vMAJOR.MINOR.PATCH` 格式的版本标签可触发 Release 发布。发布流程与版本检查要求见[发布指南](releasing.md)。
