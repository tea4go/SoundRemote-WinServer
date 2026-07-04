---
kind: build_system
name: Windows MSBuild + vcpkg 构建体系
category: build_system
scope:
    - '**'
source_files:
    - SoundRemote.sln
    - build_win.ps1
    - run_win.ps1
    - run_win.bat
    - vcpkg.json
    - vcpkg-configuration.json
    - .github/workflows/build.yml
    - Tests/packages.config
---

## 构建系统概览

本项目采用 **Visual Studio / MSBuild** 作为核心构建引擎，配合 **vcpkg** 管理 C++ 第三方依赖，通过 PowerShell 脚本封装本地开发体验，并由 GitHub Actions 驱动 CI 流水线完成自动化构建与测试。

## 关键组件

### 1. 解决方案与工程
- `SoundRemote.sln`：VS2022（版本 17）解决方案，包含两个项目：
  - `SoundRemote\SoundRemote.vcxproj` — 主服务端可执行文件
  - `Tests\Tests.vcxproj` — Google Mock 单元测试工程
- 统一支持四种配置组合：`Debug|Win32`、`Release|Win32`、`Debug|x64`、`Release|x64`
- 输出目录约定为 `<Platform>\<Configuration>`（如 `x64\Release`），可通过 `-p:OutDir` 覆盖

### 2. 依赖管理
- **vcpkg**（C++ 包管理器）：声明式依赖清单位于根目录
  - `vcpkg.json`：声明 `boost-asio`、`boost-json`、`simpleini`、`opus` 四个依赖
  - `vcpkg-configuration.json`：锁定 vcpkg 注册表 baseline，并启用 vcpkg CE artifact registry 加速下载
- **NuGet**：测试工程使用 `Tests\packages.config` 引入 `gmock@1.11.0`，构建前自动执行 `nuget restore`
- 源码中附带 `include/opus/` 头文件，但实际编译链接由 vcpkg 提供

### 3. 本地构建脚本
- `build_win.ps1` / `run_win.ps1`：功能相同的 PowerShell 入口（`run_win.bat` 仅转发到后者），提供三个开关：
  - `-Build`：检测 VS 安装 → 初始化 DevShell 环境 → 可选 nuget/vcpkg 集成 → 调用 MSBuild 并行构建（`-m`）→ 输出到 `x64\Release` 或自定义 `-Output`
  - `-Test`：运行已构建的 `Tests.exe`，非零退出码即失败
  - `-Run`：先强制终止同名进程，再启动最新构建产物
- 自动探测 Visual Studio 2022+，优先选择含 C++ 工作负载的安装；若未找到则回退到任意最新版本
- 构建前自动 `Stop-Process SoundRemote` 避免文件被占用锁住

### 4. CI 流水线（GitHub Actions）
- `.github/workflows/build.yml`：在 `windows-latest` 上触发（push/pull_request to main）
- 步骤顺序：checkout → `microsoft/setup-msbuild@v2` → `nuget restore` → `TAServers/vcpkg-cache@v3` 缓存恢复 → `vcpkg integrate install` → `msbuild` 构建 → 运行 `Tests.exe` → 上传 `bin\SoundRemote.exe` 作为 artifact
- 通过环境变量集中定义路径与配置：`SOLUTION_FILE_PATH=.`, `BINARY_FILE_PATH=bin`, `BUILD_CONFIGURATION=Release`, `PLATFORM=x64`
- 利用 `VCPKG_BINARY_SOURCES` 将 vcpkg 二进制缓存写入 GHA cache，显著缩短冷启动时间

## 架构与约定
- **单仓库多工程**：主程序与测试同仓，共享同一套 vcpkg 工具链和 MSBuild 环境
- **平台分离**：Win32 与 x64 输出互不干扰，默认以 x64 Release 为主流目标
- **幂等构建**：脚本对缺失工具（nuget、vcpkg）做降级处理，仅警告不中断流程
- **CI 与本地一致**：CI 直接调用 msbuild，与本地脚本行为对齐，减少环境差异

## 开发者注意事项
- 首次构建需安装 Visual Studio 2022+ 并勾选 **Desktop development with C++** 工作负载
- 建议预先执行 `vcpkg integrate install` 使 VS 能直接识别 vcpkg 包，否则本地调试可能找不到库
- 修改依赖后需在 CI 中更新 `vcpkg-configuration.json` 的 baseline 以固定版本
- 新增 NuGet 包时同步更新 `Tests/packages.config`，确保 `nuget restore` 能拉取