---
kind: configuration_system
name: INI 配置文件系统（Settings 单例 + CSimpleIni）
category: configuration_system
scope:
    - '**'
source_files:
    - SoundRemote/Settings.h
    - SoundRemote/Settings.cpp
    - SoundRemote/SoundRemoteApp.cpp
    - Tests/SettingsTest.cpp
    - .gitignore
---

## 1. 使用的系统与工具
- 持久化格式：Windows INI 文件，固定文件名 `settings.ini`。
- 解析库：vcpkg 管理的第三方头文件库 `SimpleIni.h`，通过大小写不敏感版本 `CSimpleIniCaseW` 读写 Unicode 键值。
- 依赖声明：`vcpkg.json` / `vcpkg-configuration.json` 中引入 SimpleIni；构建产物 `x64/Release` 与测试工程均链接该库。

## 2. 核心文件与包
- `SoundRemote/Settings.h` / `SoundRemote/Settings.cpp`：配置系统的唯一实现，封装所有 INI 读/写、默认值填充与迁移逻辑。
- `SoundRemote/SoundRemoteApp.cpp`：应用启动时创建 `Settings("settings.ini")`，并在菜单交互处调用 setter 触发持久化。
- `Tests/SettingsTest.cpp`：覆盖读取、写入、旧版无 section 迁移等场景。
- `.gitignore`：显式忽略 `settings.ini`，避免用户本地配置入库。

## 3. 架构与约定
- **单一入口类**：`Settings` 由 `SoundRemoteApp::initSettings()` 构造一次并持有，全局通过指针访问，未使用 DI 容器。
- **Section + Key 命名空间**：用 `Section::{network, general}` 与 `Setting::{server_port, client_port, check_updates, capture_device}` 两个 `constexpr` 命名空间集中管理字段名，避免魔法字符串散落。
- **默认值集中定义**：`DefaultValue` 命名空间提供端口、更新开关、默认设备 ID 等常量，构造函数在首次运行或文件缺失时写入并保存。
- **懒持久化策略**：setter 仅在值发生变化时才调用 `SaveFile`，减少磁盘 I/O。
- **向后兼容迁移**：`checkMissingSettings()` 检测旧版（0.5.2 之前）根级 key，将其迁移到 `[network]` section，并清理空 section。
- **运行时配置来源**：除 INI 外，程序还从 Windows API 获取音频设备列表，但“当前选中设备”仍落盘到 `capture_device` 字段。

## 4. 开发者应遵循的规则
- **新增配置项**：
  1. 在 `Setting` 命名空间追加 `constexpr` 键名；
  2. 在 `DefaultValue` 命名空间给出默认值；
  3. 在 `Settings` 中添加 getter/setter，并在 `setDefaultValues` 与 `checkMissingSettings` 中处理；
  4. 如需迁移旧结构，参考现有 `server_port`/`client_port` 的迁移写法。
- **禁止绕过 Settings**：所有对 `settings.ini` 的直接读写必须经过 `Settings` 类，不得自行调用 `CSimpleIniCaseW`。
- **保持大小写不敏感**：继续使用 `CSimpleIniCaseW(true)` 初始化，确保跨平台/用户输入差异下的兼容性。
- **不要将 `settings.ini` 提交到仓库**：`.gitignore` 已排除，部署时应提供示例或引导用户生成首份配置。
- **测试覆盖**：新增字段应在 `Tests/SettingsTest.cpp` 中补充读写与迁移用例。