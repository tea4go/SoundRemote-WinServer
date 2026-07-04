param(
    [switch]$Build,
    [switch]$Run,
    [switch]$Test,
    [switch]$InstallDeps,
    [switch]$Check,
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [string]$Output = ""
)

if (-not $Build -and -not $Run -and -not $Test -and -not $InstallDeps -and -not $Check) {
    Write-Host "用法: .\run_win.ps1 [-Check] [-InstallDeps] [-Build] [-Run] [-Test] [-Configuration <Release|Debug>] [-Platform <x64|Win32>] [-Output <目录>]"
    Write-Host ""
    Write-Host "  -Check           检测系统环境，确认所有构建依赖均已就绪"
    Write-Host "  -InstallDeps     安装构建依赖（nuget restore + vcpkg install）"
    Write-Host "  -Build           执行构建"
    Write-Host "  -Run             退出旧版本并运行已构建的最新版本"
    Write-Host "  -Test            运行单元测试"
    Write-Host "  -Configuration   构建配置（默认: Release）"
    Write-Host "  -Platform        目标平台（默认: x64）"
    Write-Host "  -Output          输出目录（默认: <Platform>\<Configuration>\）"
    exit 0
}

$exeName = "SoundRemote"
$sln = "SoundRemote.sln"

$defaultOutput = "$Platform\$Configuration"
$outputDir = if ($Output) { $Output } else { $defaultOutput }

function Initialize-VsEnvironment {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        Write-Error "未找到 vswhere.exe，请安装 Visual Studio 2022 或更高版本"
        return $null
    }

    # 优先找包含 C++ 工作负载的 VS 安装
    $vsPath = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    if (-not $vsPath) {
        Write-Warning "未找到含 C++ 工作负载的 VS，尝试任意最新版本..."
        $vsPath = & $vswhere -latest -property installationPath 2>$null
    }
    if (-not $vsPath) {
        Write-Error "未找到任何 Visual Studio 安装"
        return $null
    }

    Write-Host "Visual Studio: $vsPath"

    # 通过 DevShell 模块初始化 C++ 编译环境（正确设置 VCTargetsPath 等变量）
    $devShellDll = Join-Path $vsPath "Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
    if (Test-Path $devShellDll) {
        Import-Module $devShellDll -ErrorAction SilentlyContinue
        Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -no_logo" 2>$null
        Write-Host "已初始化 VS 开发环境"
    } else {
        Write-Warning "未找到 DevShell 模块：$devShellDll"
    }

    return Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe"
}

function Find-NuGet {
    $nuget = Get-Command nuget -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
    if ($nuget) { return $nuget }
    $local = "$env:USERPROFILE\nuget.exe"
    if (Test-Path $local) { return $local }
    return $null
}

function Find-Vcpkg {
    $vcpkg = Get-Command vcpkg -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
    if ($vcpkg) { return $vcpkg }
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
        if (-not $vsPath) { $vsPath = & $vswhere -latest -property installationPath 2>$null }
        if ($vsPath) {
            $embedded = Join-Path $vsPath "VC\vcpkg\vcpkg.exe"
            if (Test-Path $embedded) { return $embedded }
        }
    }
    return $null
}

function Write-CheckItem($label, $ok, $fix = "") {
    if ($ok) {
        Write-Host "  [OK]  $label" -ForegroundColor Green
    } else {
        Write-Host "  [!!]  $label" -ForegroundColor Red
        if ($fix) { Write-Host "        修复: $fix" -ForegroundColor Yellow }
    }
    return $ok
}

if ($Check) {
    Write-Host "=== 构建环境检测 ===" -ForegroundColor Cyan
    $allOk = $true

    # 1. vswhere.exe
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $ok = Test-Path $vswhere
    $allOk = (Write-CheckItem "vswhere.exe" $ok "安装 Visual Studio Installer") -and $allOk

    # 2. VS 含 C++ 工作负载
    $vsPath = $null
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    }
    $ok = [bool]$vsPath
    $allOk = (Write-CheckItem "Visual Studio (含 C++ 工作负载)$(if($vsPath){" — $vsPath"})" $ok "在 VS Installer 中勾选「使用 C++ 的桌面开发」") -and $allOk

    # 3. MSBuild
    $msbuild = if ($vsPath) { Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe" } else { $null }
    $ok = $msbuild -and (Test-Path $msbuild)
    $allOk = (Write-CheckItem "MSBuild.exe" $ok "需要 Visual Studio 含 C++ 工作负载") -and $allOk

    # 4. 平台工具集 v145
    $toolsetPath = if ($vsPath) { Join-Path $vsPath "MSBuild\Microsoft\VC\v180\Platforms\x64\PlatformToolsets\v145" } else { $null }
    $ok = $toolsetPath -and (Test-Path $toolsetPath)
    $allOk = (Write-CheckItem "平台工具集 v145" $ok "在 VS Installer 中安装最新 MSVC 生成工具") -and $allOk

    # 5. Windows 10 SDK
    $sdkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10\Include"
    $ok = (Test-Path $sdkRoot) -and ((Get-ChildItem $sdkRoot -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } | Measure-Object).Count -gt 0)
    $allOk = (Write-CheckItem "Windows 10 SDK" $ok "在 VS Installer 中勾选 Windows 10/11 SDK") -and $allOk

    # 6. vcpkg（VS 内置）
    $vcpkgExe = Find-Vcpkg
    $ok = [bool]$vcpkgExe
    $allOk = (Write-CheckItem "vcpkg$(if($vcpkgExe){" — $vcpkgExe"})" $ok "安装 Visual Studio 含 C++ 工作负载（内置 vcpkg）") -and $allOk

    # 7. vcpkg 包已安装（检查关键头文件）
    $vcpkgInc = "$PSScriptRoot\vcpkg_installed\x64-windows-static-md\x64-windows-static-md\include"
    $boostOk   = Test-Path "$vcpkgInc\boost\asio.hpp"
    $opusOk    = Test-Path "$vcpkgInc\opus\opus.h"
    $simpleOk  = Test-Path "$vcpkgInc\SimpleIni.h"
    $ok = $boostOk -and $opusOk -and $simpleOk
    $detail = @()
    if (-not $boostOk)  { $detail += "boost-asio" }
    if (-not $opusOk)   { $detail += "opus" }
    if (-not $simpleOk) { $detail += "simpleini" }
    $label = if ($ok) { "vcpkg 依赖包 (boost-asio, opus, simpleini)" } else { "vcpkg 依赖包缺少: $($detail -join ', ')" }
    $allOk = (Write-CheckItem $label $ok "运行 .\run_win.ps1 -InstallDeps") -and $allOk

    # 8. nuget.exe
    $nugetExe = Find-NuGet
    $ok = [bool]$nugetExe
    $allOk = (Write-CheckItem "nuget.exe$(if($nugetExe){" — $nugetExe"})" $ok "运行 .\run_win.ps1 -InstallDeps") -and $allOk

    # 9. NuGet 包（gmock）
    $gmock = "$PSScriptRoot\packages\gmock.1.11.0\build\native\gmock.targets"
    $ok = Test-Path $gmock
    $allOk = (Write-CheckItem "NuGet 包 gmock.1.11.0" $ok "运行 .\run_win.ps1 -InstallDeps") -and $allOk

    Write-Host ""
    if ($allOk) {
        Write-Host "所有组件就绪，可以执行 -Build" -ForegroundColor Green
    } else {
        Write-Host "存在缺失组件，请先运行 .\run_win.ps1 -InstallDeps 或按提示手动安装" -ForegroundColor Red
        exit 1
    }
}

if ($InstallDeps) {
    # 1. 确保 nuget.exe 可用
    $nugetExe = Find-NuGet
    if (-not $nugetExe) {
        $nugetLocal = "$env:USERPROFILE\nuget.exe"
        Write-Host "正在下载 nuget.exe..."
        curl.exe -L "https://dist.nuget.org/win-x86-commandline/latest/nuget.exe" -o $nugetLocal
        if ($LASTEXITCODE -ne 0) { Write-Error "下载 nuget.exe 失败"; exit 1 }
        $nugetExe = $nugetLocal
    }
    Write-Host "nuget: $nugetExe"

    # 2. 还原 NuGet 包（gmock 等测试依赖）
    Write-Host "正在还原 NuGet 包..."
    & $nugetExe restore SoundRemote.sln
    if ($LASTEXITCODE -ne 0) { Write-Error "NuGet 还原失败"; exit 1 }

    # 3. vcpkg 依赖（已安装则跳过，避免触发 cmake 等工具链下载）
    $vcpkgInc = "$PSScriptRoot\vcpkg_installed\x64-windows-static-md\x64-windows-static-md\include"
    $vcpkgReady = (Test-Path "$vcpkgInc\boost\asio.hpp") -and (Test-Path "$vcpkgInc\opus\opus.h") -and (Test-Path "$vcpkgInc\SimpleIni.h")
    if ($vcpkgReady) {
        Write-Host "vcpkg 依赖已就绪，跳过安装"
    } else {
        $vcpkgExe = Find-Vcpkg
        if (-not $vcpkgExe) { Write-Error "未找到 vcpkg，请安装 Visual Studio 并包含 C++ 工作负载"; exit 1 }
        Write-Host "vcpkg: $vcpkgExe"
        Write-Host "正在安装 vcpkg 依赖 (triplet: x64-windows-static-md)..."
        & $vcpkgExe install `
            --triplet x64-windows-static-md `
            "--x-manifest-root=$PSScriptRoot" `
            "--x-install-root=$PSScriptRoot\vcpkg_installed\x64-windows-static-md"
        if ($LASTEXITCODE -ne 0) { Write-Error "vcpkg 安装失败"; exit 1 }
    }

    Write-Host "依赖安装完成"
}

if ($Build) {
    $msbuild = Initialize-VsEnvironment
    if (-not $msbuild) { exit 1 }
    if (-not (Test-Path $msbuild)) {
        Write-Error "MSBuild 不存在：$msbuild"
        exit 1
    }
    Write-Host "MSBuild: $msbuild"

    # 构建前退出旧版本，避免文件被锁
    Get-Process -Name $exeName -ErrorAction SilentlyContinue | Stop-Process -Force

    if (Get-Command nuget -ErrorAction SilentlyContinue) {
        Write-Host "正在还原 NuGet 包..."
        nuget restore $sln
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    } else {
        Write-Warning "未找到 nuget 命令，跳过 NuGet 还原（如构建失败请手动执行 nuget restore）"
    }

    if (Get-Command vcpkg -ErrorAction SilentlyContinue) {
        Write-Host "正在集成 vcpkg..."
        vcpkg integrate install | Out-Null
    } else {
        Write-Warning "未找到 vcpkg 命令，跳过集成（如已通过 VS 集成则可忽略此警告）"
    }

    $msbuildArgs = @(
        $sln,
        "-m",
        "-p:Configuration=$Configuration",
        "-p:Platform=$Platform"
    )
    if ($Output) {
        $resolvedOutput = (New-Item -ItemType Directory -Force -Path $Output).FullName
        $msbuildArgs += "-p:OutDir=$resolvedOutput\"
    }

    Write-Host "正在构建 ($Configuration|$Platform)..."
    & $msbuild @msbuildArgs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    $exePath = Join-Path $outputDir "$exeName.exe"
    Write-Host "完成 -> $outputDir"
    Write-Host "可执行文件: $exePath"
}

if ($Test) {
    $testsExe = Join-Path $outputDir "Tests.exe"
    if (-not (Test-Path $testsExe)) {
        Write-Error "未找到 Tests.exe：$testsExe，请先执行 -Build"
        exit 1
    }
    Write-Host "正在运行单元测试..."
    & $testsExe
    if ($LASTEXITCODE -ne 0) {
        Write-Error "测试失败（退出码 $LASTEXITCODE）"
        exit $LASTEXITCODE
    }
    Write-Host "所有测试通过"
}

if ($Run) {
    $exePath = Join-Path $outputDir "$exeName.exe"
    if (-not (Test-Path $exePath)) {
        Write-Error "未找到可执行文件：$exePath，请先执行 -Build"
        exit 1
    }

    $running = Get-Process -Name $exeName -ErrorAction SilentlyContinue
    if ($running) {
        Write-Host "正在退出旧版本..."
        $running | Stop-Process -Force
        Start-Sleep -Seconds 1
    }

    Write-Host "正在启动 $exeName..."
    Start-Process -FilePath (Resolve-Path $exePath).Path
}
