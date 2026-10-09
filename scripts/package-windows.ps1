#requires -Version 5.1
<#
Build and verify a portable Windows x64 preview package. Run from a trusted
checkout. Native development dependencies must already be configured by CMake.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidatePattern('^v[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?$')]
    [string]$Version,
    [Parameter(Mandatory = $true)][string]$ExpectedCommit,
    [Parameter(Mandatory = $true)][string]$RuntimeDirectory,
    [Parameter(Mandatory = $true)][string]$AdditionalNoticesDirectory,
    [string]$BuildPreset = 'local',
    [string]$BuildDirectory = 'build/local',
    [int]$Parallel = 4
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output = Join-Path $repo 'outputs/releases'
$packageName = "Multi-focus-Image-Fusion-$Version-windows-x64"
$archive = Join-Path $output "$packageName.zip"
$checksum = "$archive.sha256"
$utf8 = New-Object System.Text.UTF8Encoding($false)
$work = $null
$originalLocation = Get-Location

function Write-Utf8([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text, $utf8)
}

function Assert-Submodules([string]$Checkout) {
    # Use native Git plumbing so this also works outside Git Bash/MSYS PATH.
    $entries = @(& git -C $Checkout -c core.quotePath=false ls-files --stage)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect submodule entries.' }
    foreach ($entry in $entries) {
        if ($entry -match '^160000 ([0-9a-f]{40}) 0\t(.+)$') {
            $recorded = $Matches[1]
            $submodule = Join-Path $Checkout $Matches[2]
            $actual = (& git -C $submodule rev-parse HEAD).Trim()
            if ($LASTEXITCODE -ne 0 -or $actual -ne $recorded) { throw "Submodule must match the recorded commit: $submodule" }
            $dirty = @(& git -C $submodule status --porcelain --untracked-files=all)
            if ($LASTEXITCODE -ne 0 -or $dirty.Count) { throw "Submodule must be clean: $submodule" }
            Assert-Submodules $submodule
        }
    }
}

function Assert-Checkout {
    $head = (& git -C $repo rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $head -ne $ExpectedCommit) {
        throw "HEAD must equal the full ExpectedCommit ($ExpectedCommit); found $head."
    }
    # Publishing documentation may be prepared alongside an existing binary tag.
    # All build inputs, including submodules and untracked source, must be clean.
    $changes = @(& git -C $repo -c core.safecrlf=false diff --name-only HEAD --)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect tracked changes.' }
    $changes += @(& git -C $repo ls-files --others --exclude-standard)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect untracked changes.' }
    $allowed = @('README.md', 'docs/build.md', 'scripts/package-windows.ps1')
    $unexpected = @($changes | Where-Object { $_ -and $_ -notin $allowed })
    if ($unexpected.Count) { throw "Commit or remove build input changes before packaging: $($unexpected -join ', ')" }
    Assert-Submodules $repo
    return @($changes | Where-Object { $_ } | Sort-Object -Unique)
}

function Assert-LocalDirectory([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if (-not (Test-Path -LiteralPath $full -PathType Container)) { throw "Missing directory: $full" }
    if ((Get-Item -LiteralPath $full).Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw "Directory must not be a link: $full"
    }
    if (@(Get-ChildItem -LiteralPath $full -Recurse -Force | Where-Object {
        $_.Attributes -band [IO.FileAttributes]::ReparsePoint
    }).Count) { throw "Directory contains a link: $full" }
    return $full
}

function Relative-Name([string]$Root, [string]$File) {
    return $File.Substring($Root.Length + 1).Replace('\', '/')
}

function Test-IsolatedStartup([string]$LaunchDirectory) {
    $saved = @{}
    $running = $null
    foreach ($name in @('PATH', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_QPA_PLATFORM', 'PYTHONPATH', 'QML2_IMPORT_PATH')) {
        $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
        [Environment]::SetEnvironmentVariable($name, $null, 'Process')
    }
    try {
        $env:PATH = "$LaunchDirectory;$env:SystemRoot/System32;$env:SystemRoot"
        $running = Start-Process -FilePath (Join-Path $LaunchDirectory 'mif_desktop.exe') -WorkingDirectory $LaunchDirectory -WindowStyle Hidden -PassThru
        Start-Sleep -Seconds 4
        $running.Refresh()
        if ($running.HasExited) { throw "Isolated startup exited early with code $($running.ExitCode)." }
        if (-not $running.Responding) { throw 'App did not respond during isolated startup.' }
        $modules = @($running.Modules | ForEach-Object { $_.FileName })
        $platform = @($modules | Where-Object { $_ -ieq (Join-Path $LaunchDirectory 'platforms/qwindows.dll') })
        if ($platform.Count -ne 1) { throw 'The app-local Qt Windows platform plugin was not loaded.' }
        $unexpectedModules = @($modules | Where-Object {
            -not $_.StartsWith($LaunchDirectory + '\', [StringComparison]::OrdinalIgnoreCase) -and
            -not $_.StartsWith($env:SystemRoot + '\', [StringComparison]::OrdinalIgnoreCase)
        })
        if ($unexpectedModules.Count) { throw "Startup loaded external modules: $($unexpectedModules -join ', ')" }
        $closedGracefully = $running.CloseMainWindow()
        if (-not $closedGracefully -or -not $running.WaitForExit(5000)) {
            Stop-Process -Id $running.Id
            $running.WaitForExit()
        }
        return [pscustomobject]@{ ClosedGracefully = $closedGracefully; ExitCode = $running.ExitCode; LoadedModules = $modules.Count }
    } finally {
        if ($null -ne $running -and -not $running.HasExited) { Stop-Process -Id $running.Id }
        foreach ($name in $saved.Keys) {
            [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process')
        }
    }
}

try {
    Set-Location -LiteralPath $repo
    $excludedChanges = @(Assert-Checkout)
    if ($env:OS -ne 'Windows_NT' -or -not [Environment]::Is64BitProcess) {
        throw 'Run this script with 64-bit PowerShell on Windows.'
    }
    if (Test-Path -LiteralPath $archive) { throw "Refusing to overwrite $archive" }
    if (Test-Path -LiteralPath $checksum) { throw "Refusing to overwrite $checksum" }
    $runtime = Assert-LocalDirectory $RuntimeDirectory
    $notices = Assert-LocalDirectory $AdditionalNoticesDirectory
    if (-not @(Get-ChildItem -LiteralPath $notices -Recurse -File).Count) { throw 'Additional notices are empty.' }
    foreach ($required in @('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')) {
        if (-not (Test-Path -LiteralPath (Join-Path $runtime $required) -PathType Leaf)) {
            throw "Missing redistributable runtime: $required"
        }
    }

    $build = Assert-LocalDirectory ([IO.Path]::GetFullPath((Join-Path $repo $BuildDirectory)))
    $cache = Get-Content -LiteralPath (Join-Path $build 'CMakeCache.txt')
    if (-not ($cache -match '^CMAKE_GENERATOR_PLATFORM:INTERNAL=x64$')) { throw 'Configured build must target x64.' }
    $configuredSource = @($cache | Where-Object { $_ -match '^CMAKE_HOME_DIRECTORY:INTERNAL=' })
    if ($configuredSource.Count -ne 1 -or
        [IO.Path]::GetFullPath($configuredSource[0].Substring('CMAKE_HOME_DIRECTORY:INTERNAL='.Length)) -ne $repo) {
        throw 'Build directory must belong to this source checkout.'
    }
    foreach ($flag in @('MIF_BUILD_GUI', 'MIF_BUILD_TESTS', 'MIF_STAGE_OUTPUTS')) {
        if (-not ($cache -match "^${flag}:BOOL=ON$")) { throw "$flag must be enabled in the configured build." }
    }
    $dumpbinEntry = @($cache | Where-Object { $_ -match '^MIF_DUMPBIN:FILEPATH=' })
    if ($dumpbinEntry.Count -ne 1) { throw 'Configured MSVC dumpbin was not found.' }
    $dumpbin = $dumpbinEntry[0].Substring('MIF_DUMPBIN:FILEPATH='.Length)
    if (-not (Test-Path -LiteralPath $dumpbin -PathType Leaf)) { throw "Missing dumpbin: $dumpbin" }

    New-Item -ItemType Directory -Path $output -Force | Out-Null
    $null = Assert-LocalDirectory $output
    $work = Join-Path $output ('.work-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $work | Out-Null
    & cmake --build --preset $BuildPreset --config Release --parallel $Parallel *> (Join-Path $work 'build.log')
    if ($LASTEXITCODE -ne 0) {
        Get-Content -LiteralPath (Join-Path $work 'build.log') -Tail 80
        throw 'Release build failed.'
    }
    & ctest --preset $BuildPreset -C Release --output-on-failure *> (Join-Path $work 'tests.log')
    if ($LASTEXITCODE -ne 0) {
        Get-Content -LiteralPath (Join-Path $work 'tests.log') -Tail 100
        throw 'Tests failed.'
    }
    $testSummary = (Get-Content -LiteralPath (Join-Path $work 'tests.log') | Where-Object { $_ -match 'tests passed|Total Test time' }) -join "`n"
    if ($testSummary -notmatch '100% tests passed') { throw 'CTest did not report a complete passing test suite.' }
    $excludedChanges = @(Assert-Checkout)

    $source = Assert-LocalDirectory (Join-Path $repo 'outputs/Release/app')
    foreach ($required in @('mif_desktop.exe', 'mif_core.dll', 'platforms/qwindows.dll', 'licenses/THIRD_PARTY_NOTICES.md')) {
        if (-not (Test-Path -LiteralPath (Join-Path $source $required) -PathType Leaf)) { throw "App deployment is incomplete: $required" }
    }
    foreach ($binary in @('mif_desktop.exe', 'mif_core.dll')) {
        $builtHash = (Get-FileHash -LiteralPath (Join-Path $build "bin/Release/$binary") -Algorithm SHA256).Hash
        $stagedHash = (Get-FileHash -LiteralPath (Join-Path $source $binary) -Algorithm SHA256).Hash
        if ($builtHash -ne $stagedHash) { throw "Staged $binary differs from the Release build." }
    }
    $package = Join-Path $work $packageName
    New-Item -ItemType Directory -Path $package | Out-Null
    # Windows supplies D3Dcompiler_47.dll. The current raster Widgets UI does not
    # need the optional Mesa software OpenGL fallback copied by windeployqt.
    Get-ChildItem -LiteralPath $source -Force | Where-Object { $_.Name -notin @('D3Dcompiler_47.dll', 'opengl32sw.dll') } |
        Copy-Item -Destination $package -Recurse
    foreach ($dll in Get-ChildItem -LiteralPath $runtime -Filter '*.dll' -File) {
        $target = Join-Path $package $dll.Name
        if (Test-Path -LiteralPath $target) {
            $oldVersion = [Version](Get-Item -LiteralPath $target).VersionInfo.FileVersion
            if ([Version]$dll.VersionInfo.FileVersion -lt $oldVersion) { throw "Runtime would be downgraded: $($dll.Name)" }
        }
        Copy-Item -LiteralPath $dll.FullName -Destination $target -Force
    }
    Copy-Item -LiteralPath $notices -Destination (Join-Path $package 'licenses/release-notices') -Recurse
    New-Item -ItemType Directory -Path (Join-Path $package 'examples') | Out-Null
    foreach ($sample in @('focus_01.png', 'focus_02.png', 'reference.png')) {
        Copy-Item -LiteralPath (Join-Path $repo "outputs/demo/$sample") -Destination (Join-Path $package 'examples')
    }

    # Check every PE import against app-local DLLs and Windows system libraries.
    # API sets and the Universal CRT are provided by supported Windows versions.
    $imports = @{}
    foreach ($binary in Get-ChildItem -LiteralPath $package -Recurse -File | Where-Object { $_.Extension -in @('.exe', '.dll') }) {
        $dependencies = & $dumpbin /DEPENDENTS $binary.FullName
        if ($LASTEXITCODE -ne 0) { throw "Cannot inspect $($binary.FullName)" }
        foreach ($line in $dependencies) {
            if ($line -match '^\s+([A-Za-z0-9_.-]+\.dll)\s*$') {
                $dependency = $Matches[1]
                $imports[$dependency] = $true
                if ($dependency -match '^(api-ms-|ext-ms-)') { continue }
                if (-not (Test-Path -LiteralPath (Join-Path $package $dependency)) -and
                    -not (Test-Path -LiteralPath (Join-Path $env:SystemRoot "System32/$dependency"))) {
                    throw "Missing runtime dependency $dependency, required by $($binary.Name)"
                }
            }
        }
    }

    Write-Utf8 (Join-Path $package 'README.txt') @"
多聚焦图像融合 — Windows x64 测试版 $Version

使用方法
1. 将整个 ZIP 解压到一个文件夹，双击 mif_desktop.exe。
2. 导入同一场景、不同焦点的图片；存在位移时先进行配准。
3. 选择融合算法，调整参数后执行融合，最后保存结果。
4. 快速测试可只导入 examples/focus_01.png 和 focus_02.png。
   examples/reference.png 是全清晰参考图，请勿加入输入图像栈。
5. 可选择“GFG-FGF 梯度融合”；其采用项目自定义的 G 优先决策，其他算法也可用于对比。

保留程序旁边的 DLL、platforms、iconengines 和 licenses 文件夹。
面向 Windows 10/11 x64；包内附带 MSVC x64 运行库，不需要安装 Python 或 OpenCV。
本次仅在开发机器上隔离开发 PATH 做了启动检查，尚未在独立干净机器验证。
这是测试版本。反馈时请提供 Windows 版本、算法和参数、复现步骤及可公开的样图。

源码提交：$ExpectedCommit
项目与反馈：https://github.com/hzweifei/Multi-focus-Image-Fusion
第三方许可证及依赖源码说明见 licenses/。
VC 运行库来自 Visual Studio 的 Microsoft.VC143.CRT 可再分发目录。
文件校验记录见 FILES.sha256，版本与本次检查结果见 BUILD_INFO.json。
"@

    $null = Assert-Checkout
    $metadata = [ordered]@{
        version = $Version
        source_commit = $ExpectedCommit
        source_tree = 'Build inputs match the commit; publishing documentation changes listed below are excluded.'
        excluded_documentation_changes = $excludedChanges
        platform = 'windows-x64'
        configuration = 'Release'
        built_at_utc = [DateTime]::UtcNow.ToString('o')
        build_preset = $BuildPreset
        ctest = $testSummary
        vc_runtime = (Get-Item -LiteralPath (Join-Path $package 'vcruntime140.dll')).VersionInfo.FileVersion
        excluded_runtime_files = @('D3Dcompiler_47.dll: provided by Windows 10/11', 'opengl32sw.dll: optional software OpenGL fallback; current raster Widgets UI does not use it')
        startup_check = 'Packaging is gated on launching the extracted ZIP on this development machine with PATH limited to package and Windows, loading app-local qwindows and no external modules.'
        clean_machine_verified = $false
        imported_dlls = @($imports.Keys | Sort-Object)
    }
    Write-Utf8 (Join-Path $package 'BUILD_INFO.json') (($metadata | ConvertTo-Json -Depth 5) + "`n")
    $manifest = @(Get-ChildItem -LiteralPath $package -Recurse -File | Sort-Object FullName | ForEach-Object {
        '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), (Relative-Name $package $_.FullName)
    })
    Write-Utf8 (Join-Path $package 'FILES.sha256') (($manifest -join "`n") + "`n")

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $temporaryZip = Join-Path $work "$packageName.zip"
    [IO.Compression.ZipFile]::CreateFromDirectory($package, $temporaryZip, [IO.Compression.CompressionLevel]::Optimal, $true)
    $verify = Join-Path $work 'verify'
    [IO.Compression.ZipFile]::ExtractToDirectory($temporaryZip, $verify)
    $unpacked = Join-Path $verify $packageName
    $packagedFiles = @(Get-ChildItem -LiteralPath $package -Recurse -File)
    $extractedFiles = @(Get-ChildItem -LiteralPath $unpacked -Recurse -File)
    if ($packagedFiles.Count -ne $extractedFiles.Count) { throw 'ZIP file count mismatch.' }
    foreach ($file in $packagedFiles) {
        $copy = Join-Path $unpacked (Relative-Name $package $file.FullName)
        if ((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash) {
            throw "ZIP hash mismatch: $($file.Name)"
        }
    }
    $startup = Test-IsolatedStartup $unpacked
    $null = Assert-Checkout
    # File.Move fails if an output appeared meanwhile; never replace a release.
    [IO.File]::Move($temporaryZip, $archive)
    $hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    $checksumText = "$hash  $packageName.zip`n"
    $checksumBytes = $utf8.GetBytes($checksumText)
    $stream = [IO.File]::Open($checksum, [IO.FileMode]::CreateNew)
    try { $stream.Write($checksumBytes, 0, $checksumBytes.Length) } finally { $stream.Dispose() }
    [pscustomobject]@{
        Archive = $archive; Bytes = (Get-Item -LiteralPath $archive).Length
        SHA256 = $hash; Files = $packagedFiles.Count; Tests = $testSummary
        IsolatedStartup = 'Passed'; ExtractedHashes = 'All matched'
        StartupExitCode = $startup.ExitCode; StartupClosedGracefully = $startup.ClosedGracefully
    } | Format-List
} finally {
    Set-Location -LiteralPath $originalLocation
    if ($null -ne $work -and (Test-Path -LiteralPath $work)) {
        # Delete only this invocation's generated workspace, after resolving it.
        $resolvedWork = (Resolve-Path -LiteralPath $work).Path
        $resolvedOutput = (Resolve-Path -LiteralPath $output).Path
        if ([IO.Path]::GetDirectoryName($resolvedWork) -ne $resolvedOutput -or
            [IO.Path]::GetFileName($resolvedWork) -notmatch '^\.work-[0-9a-f]{32}$') {
            throw "Refusing cleanup outside the release staging directory: $resolvedWork"
        }
        Remove-Item -LiteralPath $resolvedWork -Recurse -Force
    }
}
