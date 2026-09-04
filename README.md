# SoundRemote 服务端（tea4go fork）

**[EN](README.en.md) · [简体中文](README.md)**

桌面应用，通过 [SoundRemote 客户端](https://github.com/SoundRemote/client-android) 与 Android 设备配对，实现以下功能：
* 捕获音频并发送到客户端设备。
* 模拟从客户端接收的键盘快捷键。部分快捷键（如 `Ctrl + Alt + Delete` 或 `Win + L`）目前尚不支持。

![主窗口](https://github.com/user-attachments/assets/1b86c980-132d-4661-87ed-dbe3dd670a8a "主窗口")

## Fork 说明

本项目 fork 自 [SoundRemote/server-windows](https://github.com/SoundRemote/server-windows)（作者 **Aleksandr Shipovskii**，Copyright © 2025），继续使用 GPL-3.0 协议开源。

### 主要改动（tea4go, 2026-07）
- 中文/英文国际化，通过 `settings.ini` 的 `Language` 键切换
- 字体可配置（`Font` / `Font.Size` / `Font.Bold`）
- 客户端与快捷键区域改为页签布局，文本框可用高度更大
- 客户端列表改用 `ListBox`，鼠标悬停高亮
- IP 列表过滤 0.0.0.0 / 回环地址，附带网卡名称
- 更新检查器：加入 HTTP 超时、线程生命周期保护、24 小时节流
- 双击"快捷键"页签的文本框可清空历史
- 交互元素统一使用手型光标
- 新增 `run_win.ps1` 构建脚本：`-Check` / `-InstallDeps` / `-Build` / `-Run` / `-Test` / `-Publish`

## 构建

前置要求：
 - Visual Studio 2022 及以上（含 C++ 桌面开发工作负载）
 - vcpkg 已与 Visual Studio 中的 MSBuild 集成

推荐使用 PowerShell 构建脚本：

```powershell
.\run_win.ps1 -Check          # 检测环境是否就绪
.\run_win.ps1 -InstallDeps    # 安装 NuGet + vcpkg 依赖
.\run_win.ps1 -Build          # 编译（Release/x64）
.\run_win.ps1 -Run            # 运行已编译的可执行文件
.\run_win.ps1 -Test           # 运行单元测试
.\run_win.ps1 -Publish                    # 发布到 GitHub Release
.\run_win.ps1 -Publish -Target gitee      # 发布到 Gitee Release
```

## 许可证

本项目采用 **GPL-3.0** 协议，完整文本见 `COPYING` 文件。第三方依赖 Opus 采用 BSD 风格许可，见 `opus_license.txt`。

根据 GPL-3.0，二进制发布必须附带完整源码或提供获取源码的书面途径。本项目源码托管于 https://github.com/tea4go/SoundRemote-WinServer。
