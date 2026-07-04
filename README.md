# SoundRemote 服务端

桌面应用，通过 [SoundRemote 客户端](https://github.com/SoundRemote/client-android) 与 Android 设备配对，实现以下功能：
* 捕获音频并发送到客户端设备。
* 模拟从客户端接收的键盘快捷键。部分快捷键（如 `Ctrl + Alt + Delete` 或 `Win + L`）目前尚不支持。

![主窗口](https://github.com/user-attachments/assets/1b86c980-132d-4661-87ed-dbe3dd670a8a "主窗口")

## 构建

前置要求：
 - Visual Studio 2022
 - vcpkg 已与 Visual Studio 中的 MSBuild 集成
