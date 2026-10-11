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
# KZ_WINDOWS_ROOT points the dependencies at an existing acquire.ps1 layout (deps\, archives\) instead of downloading one.
$root = Resolve-WindowsRoot $(if ($env:KZ_WINDOWS_ROOT) { $env:KZ_WINDOWS_ROOT } else { Join-Path $state 'windows' })
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
    'deps/vulkan/Vulkan-Headers-vulkan-sdk-1.4.363.0/include'
)
if (@($markers | Where-Object { -not (Test-Path -LiteralPath (Join-Path $root $_)) }).Count -gt 0) {
    & "$PSScriptRoot/acquire.ps1" -Root $root
    if ($LASTEXITCODE) { $deps.Dispose(); exit $LASTEXITCODE }
}
$deps.Dispose()

# configure.ps1 runs when the directory has no build.ninja, or its cache was made for another path.
# A stale cache is deleted first, so CMake does not reuse paths that no longer exist.
function Read-CacheValue([string]$File, [string]$Key) {
    if (-not (Test-Path -LiteralPath $File)) { return $null }
    $match = Select-String -LiteralPath $File -Pattern "^$([regex]::Escape($Key)):INTERNAL=(.*)$" | Select-Object -First 1
    if ($match) { return $match.Matches[0].Groups[1].Value.Trim() }
    return $null
}
$source = $checkout.Replace('\', '/')
$buildPath = $build.Replace('\', '/')
$cache = Join-Path $build 'CMakeCache.txt'
$cachedSource = Read-CacheValue $cache 'CMAKE_HOME_DIRECTORY'
$cachedBuild = Read-CacheValue $cache 'CMAKE_CACHEFILE_DIR'
$stale = ($null -ne $cachedSource -and $cachedSource -ne $source) -or ($null -ne $cachedBuild -and $cachedBuild -ne $buildPath)
if ($stale) {
    foreach ($file in 'CMakeCache.txt', 'CMakeFiles', 'build.ninja', '.ninja_log', '.ninja_deps') {
        Remove-Item -LiteralPath (Join-Path $build $file) -Recurse -Force -ErrorAction SilentlyContinue
    }
}
if ($stale -or -not (Test-Path -LiteralPath (Join-Path $build 'build.ninja'))) {
    # KZ_WINDOWS_CMAKE_ARGS (semicolon separated) adds -D options, such as FETCHCONTENT_SOURCE_DIR_* to avoid CMake's downloads.
    $extra = @(([string]$env:KZ_WINDOWS_CMAKE_ARGS) -split ';' | Where-Object { $_ })
    & "$PSScriptRoot/configure.ps1" -Root $root -BuildDirectory $build -BuildType $buildType -CMakeArgs $extra
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}

# build.ps1 runs ninja, which builds only what changed.
$jobs = @()
if ($env:JOBS) { $jobs = @('-Jobs', $env:JOBS) }
& "$PSScriptRoot/build.ps1" -Root $root -BuildDirectory $build @jobs
if ($LASTEXITCODE) { exit $LASTEXITCODE }

if ($command -eq 'build') {
    $lock.Dispose()
    Write-Host "kazarov: built $build"
    exit 0
}

# The game reads the extracted disc, rom\pal\extracted\iso, the tree the ps2-pal configuration also uses.
# kz-extract writes it from the PAL disc and records the disc's checksum, so an unchanged disc is not extracted again.
$iso = Join-Path $checkout 'rom/pal/Dark Chronicle (PAL).iso'
$extractDir = Join-Path $checkout 'rom/pal/extracted'
$data = Join-Path $extractDir 'iso'
$idFile = Join-Path $extractDir '.kazarov-extract.id'
# Every configuration extracts into the same tree, so they take one lock for it. The game holds it shared while it runs.
$dataLockFile = Join-Path $locks 'port-data.lock'
$haveIso = Test-Path -LiteralPath $iso -PathType Leaf
if (-not $haveIso -and -not (Test-Path -LiteralPath (Join-Path $data 'DATA.HD4') -PathType Leaf)) {
    [Console]::Error.WriteLine('kazarov: put the PAL disc image at rom/pal/Dark Chronicle (PAL).iso, or extract it to rom/pal/extracted/iso.')
    exit 1
}
if ($haveIso) {
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
    $python = Find-WindowsPython
    & $python (Join-Path $checkout 'tools/kazarov/bin/kz-extract') --iso $iso --checksum (Join-Path $checkout 'rom/pal/checksum.sha256') --out $extractDir --id-file $idFile
    if ($LASTEXITCODE) { $dataLock.Dispose(); exit $LASTEXITCODE }
    $dataLock.Dispose()
}
$want = if (Test-Path -LiteralPath $idFile) { (Get-Content -LiteralPath $idFile -Raw).Trim() } else { '' }

$lock.Dispose()

# The game reads the tree while it runs, so it holds the data lock shared; another configuration's extraction waits for it.
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
$have = if (Test-Path -LiteralPath $idFile) { (Get-Content -LiteralPath $idFile -Raw).Trim() } else { '' }
if ($have -ne $want) {
    $dataShared.Dispose()
    [Console]::Error.WriteLine("kazarov: $idFile changed while starting; run again")
    exit 1
}

# Keep launch data and saves in the checkout unless explicit paths are provided.
$gameArgs = @($rest)
if (-not $env:DC2_DATA -and -not @($rest | Where-Object { $_ -like '--data*' })) { $gameArgs += @('--data', $data) }
if (-not $env:DC2_SAVE -and -not @($rest | Where-Object { $_ -like '--save*' })) { $gameArgs += @('--save', (Join-Path $state 'save')) }

Push-Location $checkout
& (Join-Path $build 'darkchronicle.exe') @gameArgs
$code = $LASTEXITCODE
Pop-Location
$dataShared.Dispose()
exit $code
