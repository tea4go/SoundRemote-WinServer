---
kind: frontend_style
name: 前端样式系统：不适用（纯 C++ Win32 桌面应用）
category: frontend_style
scope:
    - '**'
---

本仓库是 SoundRemote Windows 服务端，使用原生 C++ 与 Win32 API 构建桌面 UI，不包含任何前端样式相关代码。经检查：
- 无 CSS/SCSS/Less/Sass/Stylus 等样式文件
- 无 HTML、JavaScript、TypeScript 或任何 Web 前端资源
- UI 完全通过 Win32 `CreateWindowW`、`DialogBox`、`LoadStringW` 等原生 API 在 `SoundRemoteApp.cpp`、`Controls.cpp` 中硬编码实现
- 仅包含 `.ico` 图标资源（`resources/sound_off.ico`、`sound_on.ico`），无主题、设计令牌或样式框架

因此 `frontend_style` 类别不适用于此仓库。