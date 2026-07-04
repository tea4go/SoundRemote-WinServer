# CLAUDE.md

本文件为 Claude Code (claude.ai/code) 在此代码库中工作时提供指导。

## 项目概述

SoundRemote 是一个 Windows 桌面应用程序，通过 UDP 将系统音频捕获并流式传输到 Android 客户端，同时接收来自 Android 设备的键盘快捷键命令并在 Windows 上模拟执行。

## 构建系统

**Visual Studio 2022 / MSBuild** — 解决方案文件 `SoundRemote.sln`。

前置条件：
- Visual Studio 2022（含 C++ 工作负载，工具集 `v143`，Windows 10 SDK）
- vcpkg 集成 MSBuild（`vcpkg integrate install`）
- NuGet（用于测试依赖 `gmock 1.11.0`）

命令行构建：
```bat
nuget restore SoundRemote.sln
msbuild SoundRemote.sln -m -p:Configuration=Release -p:Platform=x64
```

运行测试：
```bat
x64\Release\Tests.exe
```

## 架构

数据流为线性管道：

```
WASAPI (AudioCapture)
  → AudioResampler    重采样到目标采样率/格式
  → CapturePipe       编排整个管道流程
  → EncoderOpus       可选的 Opus 压缩
  → Server            通过 boost::asio 协程异步 UDP 发送
  → Clients           per-client 注册表，记录音频格式偏好
```

**SoundRemoteApp** 是 Win32 应用程序宿主，负责创建并连接 `Server`、`Clients`、`CapturePipe`、`Settings` 和 `UpdateChecker`。

## 网络协议

定义在 `NetDefines.h`，UDP 自定义二进制协议：
- 5 字节头部：2 字节签名（`0xA571`）、1 字节类别、2 字节数据长度
- 服务端监听端口 `15711`，向客户端发送端口 `15712`
- 协议版本：`1`
- 音频模式：未压缩 PCM 或 Opus

数据包类别：Connect、Disconnect、SetFormat、Keystroke、AudioDataUncompressed、AudioDataOpus、ClientKeepAlive、ServerKeepAlive、Ack。

## 关键设计说明

- **Server**：使用 C++20 协程（`co_await`）配合 `boost::asio` 进行异步 UDP 接收；收到 Keystroke 包时触发 `KeystrokeCallback`。
- **Clients**：基于 `std::shared_mutex` 的线程安全注册表；客户端无活动约 5 秒后超时；通过观察者模式（`addClientsListener`）通知 UI 变更。
- **Settings**：通过 vcpkg 依赖 `simpleini` 将配置持久化到 INI 文件。
- `Tests` 项目直接链接 `SoundRemote` 的 `.obj` 文件（非静态库），不存在单独的库目标。

## 测试

`Tests/` 下分两类：
1. 功能单元测试（`ClientsTest.cpp`、`KeystrokeTest.cpp` 等），使用 Google Test/Mock。
2. 仅编译的头文件自包含测试，位于 `Tests/header_tests/`，每个源文件头部对应一个，验证头文件可独立引入。

新增源码模块时，需在 `header_tests/` 中添加对应的 `*HTest.cpp`。
