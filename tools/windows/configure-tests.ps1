param([string]$Root, [string]$BuildDirectory, [string[]]$CMakeArgs = @())
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/common.ps1"
$root = Resolve-WindowsRoot $Root
if (!$BuildDirectory) { $BuildDirectory = Join-Path $root 'build-tests' }
& "$PSScriptRoot/configure.ps1" -Root $root -BuildDirectory $BuildDirectory -Tests -CMakeArgs $CMakeArgs
exit $LASTEXITCODE
