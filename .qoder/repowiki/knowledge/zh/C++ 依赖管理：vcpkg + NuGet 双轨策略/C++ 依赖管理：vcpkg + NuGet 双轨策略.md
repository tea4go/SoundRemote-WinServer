---
kind: dependency_management
name: C++ 依赖管理：vcpkg + NuGet 双轨策略
category: dependency_management
scope:
    - '**'
source_files:
    - vcpkg.json
    - vcpkg-configuration.json
    - SoundRemote/SoundRemote.vcxproj
    - Tests/Tests.vcxproj
    - Tests/packages.config
---

本仓库采用 vcpkg（Manifest 模式）+ NuGet 双轨依赖管理，分别覆盖运行时 C/C++ 第三方库与测试框架两类依赖。

## 1. 运行时代码依赖 — vcpkg Manifest
- 声明文件 vcpkg.json：以清单形式声明四个运行时依赖：boost-asio、boost-json、simpleini、opus。
- 版本锁定 vcpkg-configuration.json：通过 default-registry.baseline 固定到 vcpkg 官方注册表的一个具体 git commit（0cf34c18...），并额外配置了 Microsoft 的 artifact registry，确保可重现构建。
- MSBuild 集成：在 SoundRemote/SoundRemote.vcxproj 中启用 VcpkgEnableManifest=true，并通过 VcpkgUseStatic=true 强制静态链接；同时 VcpkgUseMD=true 指定使用动态 CRT（MultiThreadedDLL）。
- 本地源码嵌入：include/opus/ 下直接包含 opus 头文件，配合 vcpkg.json 中的 opus 包，形成“头文件 + 预编译静态库”的组合方式。

## 2. 测试依赖 — NuGet（packages.config）
- 声明文件 Tests/packages.config：仅声明一个测试依赖 gmock@1.11.0（targetFramework=native）。
- 手动引入：Tests/Tests.vcxproj 通过显式 Import Project="..\packages\gmock.1.11.0\build\native\gmock.targets" 和 ClCompile Include="..\packages\gmock.1.11.0\lib\native\src\gtest\src\gtest_main.cc" 将 gtest/gmock 源码纳入编译，而非通过项目引用链接。
- 恢复钩子：工程末尾定义了 EnsureNuGetPackageBuildImports Target，在未找到 .targets 时给出提示，依赖 NuGet Package Restore 自动下载。

## 3. 架构与约定
- 运行时 C/C++ 库：统一走 vcpkg manifest，禁止在 vcxproj 中手写 AdditionalDependencies 以外的路径。
- 测试框架：通过 NuGet packages.config 拉取，且以源码形式编译进 Tests 工程。
- 版本控制：vcpkg 通过 baseline 锁定注册表快照；NuGet 通过版本号硬编码。
- 链接模型：所有配置均 VcpkgUseStatic=true + VcpkgUseMD=true，即“静态链接第三方库 + 动态 CRT”。
- 平台工具集：固定为 v145（VS2022），Windows SDK 10.0。

## 4. 开发者应遵循的规则
1. 新增运行时依赖：在 vcpkg.json 的 dependencies 数组中添加条目，不要修改任何 vcxproj 的 Include/Lib 路径。
2. 升级依赖版本：优先调整 vcpkg-configuration.json 的 baseline 指向新提交；如需特定端口版本，可在 vcpkg.json 中使用特性或 port 版本约束。
3. 新增测试依赖：更新 Tests/packages.config 的版本号，并确保 vcxproj 中的 Import 与 ClCompile Include="..\packages\..." 同步更新。
4. 保持链接一致性：新增模块必须遵循 VcpkgUseStatic=true + VcpkgUseMD=true 的链接约定，避免 CRT 混用导致 LNK4098 等错误。
5. CI 环境：GitHub Actions build.yml 应在安装 MSBuild 后执行 vcpkg install（由 Manifest 驱动），并对 NuGet restore 做幂等处理。