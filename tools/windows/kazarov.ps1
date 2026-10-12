#requires -Version 7.0
# Builds or runs a Windows configuration: kazarov.ps1 build|run <config> [args...].
# Kazarov's entry points call it from the project root with the configuration resolved.
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/common.ps1"

if ($args.Count -lt 2) { throw 'usage: kazarov.ps1 build|run <config> [args...]' }
$command = ([string]$args[0]).ToLower()
$config = [string]$args[1]
$rest = @($args | Select-Object -Skip 2)
if ($command -notin 'build', 'run') { throw "kazarov: unknown command $command" }

switch ($config.ToLower()) {
    { $_ -in 'windows', 'windows-x64-release' } { $name = 'windows-x64-release'; $buildType = 'RelWithDebInfo' }
    'windows-x64' { $name = 'windows-x64'; $buildType = 'Debug' }
    default { throw "kazarov: unknown Windows configuration $config" }
}

$checkout = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$state = Join-Path $checkout '.kazarov'
$root = Resolve-WindowsRoot (Join-Path $state 'windows')
$build = Resolve-WindowsBuild $root (Join-Path $checkout "port/build/$name")

# One run per configuration at a time. The lock is released when the process exits, or before the game starts.
$locks = Join-Path $state 'locks'
New-Item -ItemType Directory -Force $locks | Out-Null
$lockFile = Join-Path $locks "$name.lock"
$lock = $null
$announced = $false
while ($null -eq $lock) {
    try {
        $lock = [IO.File]::Open($lockFile, 'OpenOrCreate', 'ReadWrite', 'None')
    } catch [IO.IOException], [UnauthorizedAccessException] {
        if (-not $announced) { Write-Host "kazarov: waiting for $lockFile"; $announced = $true }
        Start-Sleep -Milliseconds 500
    }
}

if ($env:CLEAN -eq '1' -and (Test-Path -LiteralPath $build)) {
    Remove-Item -LiteralPath $build -Recurse -Force
}

# Both configurations share the dependencies, so their check and acquisition are serialised by a second lock.
$depsLock = Join-Path $locks 'windows-deps.lock'
$deps = $null
$depsAnnounced = $false
while ($null -eq $deps) {
    try {
        $deps = [IO.File]::Open($depsLock, 'OpenOrCreate', 'ReadWrite', 'None')
    } catch [IO.IOException], [UnauthorizedAccessException] {
        if (-not $depsAnnounced) { Write-Host "kazarov: waiting for $depsLock"; $depsAnnounced = $true }
        Start-Sleep -Milliseconds 500
    }
}

# acquire.ps1 verifies its archives. It runs only when one of the toolchain's files is missing.
$markers = @(
    'deps/llvm/llvm-mingw-20260922-ucrt-x86_64/bin/clang++.exe',
    'deps/ninja/ninja.exe',
    'deps/glslang/bin/glslang.exe',
    'deps/sdl/SDL3-3.4.18/x86_64-w64-mingw32/lib/cmake/SDL3',
    'deps/ffmpeg/ffmpeg-n7.1.1-5-g276bd388f3-win64-lgpl-shared-7.1/bin/avcodec-61.dll',
    'deps/vulkan/Vulkan-Headers-vulkan-sdk-1.4.363.0/include'
)
if (@($markers | Where-Object { -not (Test-Path -LiteralPath (Join-Path $root $_)) }).Count -gt 0) {
    & "$PSScriptRoot/acquire.ps1" -Root $root
    if ($LASTEXITCODE) { $deps.Dispose(); exit $LASTEXITCODE }
}
$deps.Dispose()

# A stale cache is deleted before configuring: CMake would otherwise reuse paths that no longer exist.
function Read-CacheValue([string]$File, [string]$Key) {
    if (-not (Test-Path -LiteralPath $File)) { return $null }
    $match = Select-String -LiteralPath $File -Pattern "^$([regex]::Escape($Key)):[A-Z]+=(.*)$" | Select-Object -First 1
    if ($match) { return $match.Matches[0].Groups[1].Value.Trim() }
    return $null
}
$source = $checkout.Replace('\', '/')
$buildPath = $build.Replace('\', '/')
$ffmpegDir = "$($root.Replace('\', '/'))/deps/ffmpeg/ffmpeg-n7.1.1-5-g276bd388f3-win64-lgpl-shared-7.1"
$cache = Join-Path $build 'CMakeCache.txt'
$cachedSource = Read-CacheValue $cache 'CMAKE_HOME_DIRECTORY'
$cachedBuild = Read-CacheValue $cache 'CMAKE_CACHEFILE_DIR'
$cachedFfmpeg = Read-CacheValue $cache 'KZ_FFMPEG_DIR'
$stale = ($null -ne $cachedSource -and $cachedSource -ne $source) -or ($null -ne $cachedBuild -and $cachedBuild -ne $buildPath)
if ($stale) {
    foreach ($file in 'CMakeCache.txt', 'CMakeFiles', 'build.ninja', '.ninja_log', '.ninja_deps') {
        Remove-Item -LiteralPath (Join-Path $build $file) -Recurse -Force -ErrorAction SilentlyContinue
    }
}
# ninja re-runs CMake from the existing cache on its own, so a cache configured without KZ_FFMPEG_DIR must be reconfigured here.
$reconfigure = $cachedFfmpeg -ne $ffmpegDir
if ($stale -or $reconfigure -or -not (Test-Path -LiteralPath (Join-Path $build 'build.ninja'))) {
    & "$PSScriptRoot/configure.ps1" -Root $root -BuildDirectory $build -BuildType $buildType
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}

# build.ps1 runs ninja, which builds only what changed.
$jobs = @{}
if ($env:JOBS) { $jobs = @{ Jobs = [int]$env:JOBS } }
& "$PSScriptRoot/build.ps1" -Root $root -BuildDirectory $build @jobs
if ($LASTEXITCODE) { exit $LASTEXITCODE }

if ($command -eq 'build') {
    $lock.Dispose()
    Write-Host "kazarov: built $build"
    exit 0
}

# The game data is extracted from the PAL disc into data\, once per source and dc2data.exe. data\.kazarov-data
# records the source's and dc2data's sizes and write times; a run with the same ones skips the extraction.
# A directory source only reports its own write time, so edits to files inside it do not trigger re-extraction.
$source = Join-Path $checkout 'rom/pal/Dark Chronicle (PAL).iso'
if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
    $source = Join-Path $checkout 'rom/pal/extracted/iso'
    if (-not (Test-Path -LiteralPath $source -PathType Container)) { $source = $null }
}

$data = Join-Path $checkout 'data'
# Every port configuration extracts into the same data\, so they take one lock for it. The game holds it shared while it runs.
$dataLockFile = Join-Path $locks 'port-data.lock'
if (-not $source) {
    # Without a disc an existing data\ is used; otherwise the game starts without its data.
    if (-not (Test-Path -LiteralPath $data -PathType Container)) {
        [Console]::Error.WriteLine('kazarov: no PAL disc found; the game starts without data and its file loads fail')
    }
} else {
    $dataLock = $null
    $dataAnnounced = $false
    while ($null -eq $dataLock) {
        try {
            $dataLock = [IO.File]::Open($dataLockFile, 'OpenOrCreate', 'ReadWrite', 'None')
        } catch [IO.IOException], [UnauthorizedAccessException] {
            if (-not $dataAnnounced) { Write-Host "kazarov: waiting for $dataLockFile"; $dataAnnounced = $true }
            Start-Sleep -Milliseconds 500
        }
    }
    $dc2data = Join-Path $build 'dc2data.exe'
    $sourceInfo = Get-Item -LiteralPath $source
    $sourceLength = if ($sourceInfo.PSIsContainer) { 0 } else { $sourceInfo.Length }
    $dc2dataInfo = Get-Item -LiteralPath $dc2data
    $want = "iso $sourceLength $($sourceInfo.LastWriteTimeUtc.Ticks)`ndc2data $($dc2dataInfo.Length) $($dc2dataInfo.LastWriteTimeUtc.Ticks)"
    $stamp = Join-Path $data '.kazarov-data'
    $have = if (Test-Path -LiteralPath $stamp) { (Get-Content -LiteralPath $stamp -Raw) -replace "`r", '' } else { '' }
    if ($have.Trim() -ne $want) {
        New-Item -ItemType Directory -Force $data | Out-Null
        & $dc2data extract $source $data
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        Set-Content -LiteralPath $stamp -Value $want
    }
    $dataLock.Dispose()
}

$lock.Dispose()

# The game reads data\ while it runs, so it holds the data lock shared; another configuration's extraction waits for it.
$dataShared = $null
$dataSharedAnnounced = $false
while ($null -eq $dataShared) {
    try {
        $dataShared = [IO.File]::Open($dataLockFile, 'OpenOrCreate', 'Read', 'Read')
    } catch [IO.IOException], [UnauthorizedAccessException] {
        if (-not $dataSharedAnnounced) { Write-Host "kazarov: waiting for $dataLockFile"; $dataSharedAnnounced = $true }
        Start-Sleep -Milliseconds 500
    }
}

# Another configuration can re-extract between the two locks; the game then refuses to start.
if ($source) {
    $have = if (Test-Path -LiteralPath $stamp) { (Get-Content -LiteralPath $stamp -Raw) -replace "`r", '' } else { '' }
    if ($have.Trim() -ne $want) {
        $dataShared.Dispose()
        [Console]::Error.WriteLine("kazarov: $stamp no longer matches this build's dc2data; run again")
        exit 1
    }
}

Push-Location $checkout
& (Join-Path $build 'darkchronicle.exe') @rest
$code = $LASTEXITCODE
Pop-Location
$dataShared.Dispose()
exit $code
