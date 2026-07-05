param(
    [switch]$Build,
    [switch]$Run,
    [switch]$Test,
    [switch]$InstallDeps,
    [switch]$Check,
    [switch]$Publish,
    [ValidateSet('github', 'gitee')]
    [string]$Target = 'github',
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [string]$Output = ""
)

if (-not $Build -and -not $Run -and -not $Test -and -not $InstallDeps -and -not $Check -and -not $Publish) {
    Write-Host "用法: .\run_win.ps1 [-Check] [-InstallDeps] [-Build] [-Run] [-Test] [-Publish] [-Target <github|gitee>] [-Configuration <Release|Debug>]"
    Write-Host ""
    Write-Host "  -Check           检测系统环境，确认所有构建依赖均已就绪"
    Write-Host "  -InstallDeps     安装构建依赖（nuget restore + vcpkg install）"
    Write-Host "  -Build           执行构建"
    Write-Host "  -Run             退出旧版本并运行已构建的最新版本"
    Write-Host "  -Test            运行单元测试"
    Write-Host "  -Publish         发布已有构建产物到 Release（不重新构建）"
    Write-Host "  -Target          发布平台（与 -Publish 搭配），可选：github（默认）、gitee"
    Write-Host "                   - github：需已安装并登录 gh CLI"
    Write-Host "                   - gitee：需设置环境变量 GITEE_TOKEN"
    Write-Host "  -Configuration   构建配置（默认: Release）"
    Write-Host "  -Platform        目标平台（仅 x64 受支持，默认: x64）"
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

function Read-RcVersion {
    param([string]$RcPath)
    if (-not (Test-Path $RcPath)) { return $null }
    # .rc 文件是 UTF-16LE 编码
    $content = Get-Content -LiteralPath $RcPath -Raw -Encoding Unicode
    if ($content -match 'FILEVERSION\s+(\d+),\s*(\d+),\s*(\d+),\s*(\d+)') {
        return "$($Matches[1]).$($Matches[2]).$($Matches[3])"
    }
    return $null
}

if ($Publish) {
    $rcPath = Join-Path $PSScriptRoot "SoundRemote\SoundRemote.rc"
    $version = Read-RcVersion -RcPath $rcPath
    if (-not $version) { Write-Error "无法从 $rcPath 读取版本号"; exit 1 }

    $exePath = Join-Path $outputDir "$exeName.exe"
    if (-not (Test-Path $exePath)) { Write-Error "未找到 $exePath，请先执行 -Build"; exit 1 }

    $notesFile = Join-Path $PSScriptRoot "RELEASE_NOTES.md"
    if (-not (Test-Path $notesFile)) {
        Write-Error "未找到 RELEASE_NOTES.md，请先创建该文件作为发布说明"
        exit 1
    }

    # 打包 exe + 许可证 + NOTICE 为 zip
    $artifactName = "$exeName-$version-$Platform"
    $zipPath = Join-Path $outputDir "$artifactName.zip"
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }

    # 生成合规 NOTICE.txt（源码链接、原作者、修改说明）
    $noticePath = Join-Path $outputDir "NOTICE.txt"
    $noticeContent = @"
SoundRemote Server (fork by tea4go) v$version
============================================

This software is licensed under GPL-3.0.
See COPYING for the full license text.

Origin
------
Original project: https://github.com/SoundRemote/server-windows
Original author:  Aleksandr Shipovskii (Copyright (C) 2025)

Fork
----
Fork repository:  https://github.com/tea4go/SoundRemote-WinServer
Fork maintainer:  tea4go (Copyright (C) 2026)
Fork changes:     see README.md for the list of modifications

Third-party components
----------------------
Opus codec (BSD-style) — see opus_license.txt
Boost, SimpleIni, and others — see respective LICENSE files in the source tree.

Source code
-----------
As required by GPL-3.0 section 6, the complete corresponding source code
for this binary is available at the fork repository above. If the repository
becomes unavailable, contact the fork maintainer at the URL listed.
"@
    Set-Content -LiteralPath $noticePath -Value $noticeContent -Encoding UTF8

    # 收集要打包的文件
    $filesToZip = @($exePath, $noticePath)
    $copyingPath = Join-Path $PSScriptRoot "COPYING"
    $opusLicensePath = Join-Path $PSScriptRoot "opus_license.txt"
    if (Test-Path $copyingPath)     { $filesToZip += $copyingPath }
    if (Test-Path $opusLicensePath) { $filesToZip += $opusLicensePath }
    Compress-Archive -Path $filesToZip -DestinationPath $zipPath -Force
    Remove-Item $noticePath -Force  # 打包完删掉临时 NOTICE

    $tag = "v$version"
    $title = "$exeName v$version"
    Write-Host "  版本: $version" -ForegroundColor Cyan
    Write-Host "  产物: $zipPath (含 SoundRemote.exe, NOTICE.txt, COPYING, opus_license.txt)" -ForegroundColor Cyan
    Write-Host "  Tag:  $tag" -ForegroundColor Cyan
    Write-Host "  平台: $Target" -ForegroundColor Cyan

    if ($Target -eq 'gitee') {
        # Gitee Release（API v5）
        $giteeOwner = 'tea4go'
        $giteeRepo  = 'SoundRemote-WinServer'
        $giteeToken = $env:GITEE_TOKEN
        if ([string]::IsNullOrWhiteSpace($giteeToken)) {
            Write-Error "未设置 GITEE_TOKEN 环境变量。请先设置："
            Write-Host '  $env:GITEE_TOKEN = "你的 Gitee 私人令牌"' -ForegroundColor Yellow
            Write-Host '  获取令牌: https://gitee.com/personal_access_tokens' -ForegroundColor Yellow
            exit 1
        }

        $notes = Get-Content -LiteralPath $notesFile -Raw -Encoding UTF8
        $createUri = "https://gitee.com/api/v5/repos/$giteeOwner/$giteeRepo/releases"
        $createBody = @{
            access_token     = $giteeToken
            tag_name         = $tag
            name             = $title
            body             = $notes
            target_commitish = 'main'
        }
        Write-Host "  创建 Gitee Release..." -ForegroundColor Cyan
        try {
            $release = Invoke-RestMethod -Method Post -Uri $createUri -Body $createBody -ErrorAction Stop
        } catch {
            Write-Error "Gitee Release 创建失败：$($_.Exception.Message)"
            exit 1
        }
        $releaseId = $release.id
        Write-Host "  Release 已创建（ID: $releaseId）" -ForegroundColor Green

        # 上传附件用 curl.exe（PowerShell multipart 易出错）
        $curlExe = (Get-Command curl.exe -ErrorAction SilentlyContinue).Source
        if (-not $curlExe) { Write-Error "未找到 curl.exe"; exit 1 }

        $uploadUri = "https://gitee.com/api/v5/repos/$giteeOwner/$giteeRepo/releases/$releaseId/attach_files"
        Write-Host "  上传 $(Split-Path $zipPath -Leaf) ..." -ForegroundColor Cyan
        & $curlExe -s -S -X POST -F "file=@$zipPath" -F "access_token=$giteeToken" $uploadUri
        if ($LASTEXITCODE -ne 0) { Write-Error "附件上传失败（curl 退出码 $LASTEXITCODE）"; exit 1 }
        Write-Host "已发布：https://gitee.com/$giteeOwner/$giteeRepo/releases/tag/$tag" -ForegroundColor Green
    } else {
        # GitHub Release（gh CLI）
        if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
            Write-Error "未找到 gh CLI，请先安装：https://cli.github.com/ 并 gh auth login"
            exit 1
        }
        & gh auth status 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { Write-Error "gh 未登录，请先执行：gh auth login"; exit 1 }

        $repo = 'tea4go/SoundRemote-WinServer'
        & gh release create $tag $zipPath --title $title --notes-file $notesFile --repo $repo
        if ($LASTEXITCODE -ne 0) { Write-Error "GitHub Release 发布失败（tag 可能已存在，请提升版本号后重试）"; exit 1 }
        Write-Host "已发布：https://github.com/$repo/releases/tag/$tag" -ForegroundColor Green
    }
    exit 0
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
