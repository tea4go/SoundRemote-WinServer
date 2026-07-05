## SoundRemote 更新说明

> **License**: 本软件基于 GPL-3.0 协议发布。zip 内含 `COPYING`（完整许可证）与 `NOTICE.txt`（合规声明）。
> - 源代码：https://github.com/tea4go/SoundRemote-WinServer
> - 上游项目：https://github.com/SoundRemote/server-windows（作者 Aleksandr Shipovskii）
> - Fork 说明：本项目在原版基础上做了中文化、字体配置、UI 重构等修改，详见 README.md

### 新功能
- 界面国际化支持（中文/英文）
- 字体可配置（名称、大小、粗体）
- 客户端与快捷键改为页签式布局
- 手型光标反馈
- 客户端列表改用 ListBox 控件

### 修复
- 更新检查器：HTTP 超时、线程生命周期、数据读取长度
- 启动检查节流至 24 小时
- IP 列表过滤无效/回环地址，追加网卡名称

### 使用说明
1. 解压 `SoundRemote-x.y.z-x64.zip` 到任意目录
2. 双击 `SoundRemote.exe` 运行
3. 编辑同目录 `settings.ini` 可修改语言、字体等设置
