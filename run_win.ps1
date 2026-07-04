param(
    [switch]$Build,
    [switch]$Run,
    [switch]$Test,
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [string]$Output = ""
)

if (-not $Build -and -not $Run -and -not $Test) {
    Write-Host "用法: .\build_win.ps1 [-Build] [-Run] [-Test] [-Configuration <Release|Debug>] [-Platform <x64|Win32>] [-Output <目录>]"
    Write-Host ""
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
