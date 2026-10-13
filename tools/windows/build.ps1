param([string]$Root, [string]$BuildDirectory, [switch]$Tests, [int]$Jobs = 12)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/common.ps1"
$root = Resolve-WindowsRoot $Root
$build = Resolve-WindowsBuild $root $BuildDirectory
$cmake = Find-WindowsTool cmake 'C:/Program Files/CMake/bin/cmake.exe'
$targets = @('darkchronicle', 'dc2data')
if ($Tests) { $targets += 'darkchronicle_tests' }
& $cmake --build $build --target $targets -j $Jobs *> "$build/build.log"
$code = $LASTEXITCODE
Get-Content "$build/build.log" -Tail 65
if ($code) { exit $code }
$runtime = "$root/deps/llvm/llvm-mingw-20260922-ucrt-x86_64/x86_64-w64-mingw32/bin"
# BtbN's avcodec-61.dll imports swresample-5.dll; the process will not start without it.
$ffmpeg = "$root/deps/ffmpeg/ffmpeg-n7.1.1-5-g276bd388f3-win64-lgpl-shared-7.1/bin"
Copy-Item -LiteralPath "$runtime/libc++.dll", "$runtime/libunwind.dll", "$runtime/libwinpthread-1.dll", "$root/deps/sdl/SDL3-3.4.18/x86_64-w64-mingw32/bin/SDL3.dll", "$ffmpeg/avcodec-61.dll", "$ffmpeg/avutil-59.dll", "$ffmpeg/swresample-5.dll" -Destination $build
exit 0
