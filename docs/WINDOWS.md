# Windows on x64

The PC port builds natively on x64 Windows with llvm-mingw clang, SDL3 and
Vulkan. It needs your own PAL disc image, as on Linux and macOS.

**Status.** The Windows scripts and the Kazarov configurations exist and run as
far as the port's sources allow. At the time of writing the port does not yet
compile on Windows, so nothing here has produced a `darkchronicle.exe`; the
section "What is verified and what is not" says what has been run.

## Dependencies

Install PowerShell 7, Python 3 (with `py` or `python` on PATH) and 7-Zip. CMake
3.28 or later is needed too; the scripts find `cmake` and `7z` on PATH, then try
their standard installations under `C:/Program Files`. A Vulkan driver with the
features listed in `port/src/gfx/README.md`, "Device", is required.

The build system is Kazarov, a submodule at `tools/kazarov`. A clone needs it:

```bat
git clone --recurse-submodules <repository>
```

or `git submodule update --init tools/kazarov` in an existing checkout. `build.bat`
and `run.bat` run that command themselves when the directory is empty.

## Building and running

From the checkout, in a Command Prompt:

```bat
build.bat                  & rem windows-x64-release: RelWithDebInfo, port\build\windows-x64-release
build.bat windows-x64      & rem Debug, port\build\windows-x64
run.bat                    & rem builds, extracts the game data once, then starts darkchronicle.exe
```

On a Windows host the default configuration is `windows-x64-release`
(`KZ_DEFAULT_BUILD_WINDOWS` in `kazarov.conf`). The PS2 build is still
`build.bat ps2-pal`; it runs in a container (docker or podman), as before.

`run.bat` takes the arguments for `darkchronicle.exe` after the configuration name, and
`set CLEAN=1` before a build deletes the configuration's build directory first.
`set JOBS=N` sets the number of parallel jobs.

`tools/windows/kazarov.ps1` does the work. It runs `acquire.ps1`, `configure.ps1`
and `build.ps1` as needed, and keeps the downloads and locks in `<checkout>\.kazarov`,
which both configurations share:

- `acquire.ps1` runs only when a dependency is missing. It downloads each pinned
  archive and verifies its SHA-256 before extracting it.
- `configure.ps1` runs only when the build directory has no `build.ninja`, or its
  `CMakeCache.txt` was made for another checkout path. Then the cache files are deleted first.
- `build.ps1` builds `darkchronicle` with Ninja, so an unchanged tree rebuilds nothing,
  and copies the runtime DLLs (`SDL3.dll`, `libc++.dll`, `libunwind.dll`,
  `libwinpthread-1.dll`) beside the executable.
- A lock in `.kazarov\locks` serialises runs of one configuration; other configurations run in parallel.

Two environment variables exist for machines that must not download:

- `KZ_WINDOWS_ROOT` names a directory holding `deps\` in `acquire.ps1`'s layout
  (`llvm`, `ninja`, `sdl`, `glslang`, `vulkan`) instead of `<checkout>\.kazarov\windows`.
  `acquire.ps1` does not run when every dependency is there.
- `KZ_WINDOWS_CMAKE_ARGS` is a semicolon-separated list of extra `-D` options for
  `configure.ps1`. CMake itself fetches nlohmann/json 3.12.0 and `stb_image.h` while
  configuring (`port/CMakeLists.txt`) and GoogleTest v1.18.0 with tests on; pass
  `-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=<dir>` to use a local copy, and put
  `stb_image.h` in `<build>/stb/` beforehand.

## Game data

`run.bat` extracts the PAL disc, `rom\pal\Dark Chronicle (PAL).iso`, into
`rom\pal\extracted\iso` with Kazarov's `kz-extract`. It is the tree the `ps2-pal`
configuration uses, and `kz-extract` checks the image against
`rom\pal\checksum.sha256` first, so only that disc is accepted. It records
what it extracted, so a later run with the same disc extracts nothing.
Without the disc image, an existing `rom\pal\extracted\iso` that holds `DATA.HD4` is used;
with neither, the run stops and says where to put the image.

The game is started with `--data rom\pal\extracted\iso --save .kazarov\save`
unless the arguments already name `--data` or `--save`, or `DC_DATA` or
`DC_SAVE` is set. Without those, the game would read and write
`%LOCALAPPDATA%\chronicle`, the directory the first game's port also uses.

To extract by hand:

```powershell
python tools/kazarov/bin/kz-extract --iso "rom/pal/Dark Chronicle (PAL).iso" --checksum rom/pal/checksum.sha256 --out rom/pal/extracted --id-file rom/pal/extracted/.kazarov-extract.id
```

Use only an owned PAL disc image. Keep the image and the extracted files outside
git; `rom/` is already ignored.

## Running

```powershell
& port/build/windows-x64-release/darkchronicle.exe --data rom/pal/extracted/iso --save .kazarov/save
```

## Scripts for CI and manual builds

The scripts can be called directly. They default `Root` to the checkout's
parent directory; pass `-Root` to use another. Sources always come from the
checkout containing the scripts. `kazarov.ps1` passes `Root` as
`<checkout>\.kazarov\windows`, or `KZ_WINDOWS_ROOT`.

```powershell
./tools/windows/acquire.ps1 -Root $root [-VulkanLoader]
./tools/windows/configure.ps1 -Root $root -BuildDirectory $build [-Tests] [-BuildType RelWithDebInfo] [-VulkanLibrary <dll>] [-CMakeArgs <options>]
./tools/windows/build.ps1 -Root $root -BuildDirectory $build [-Tests] [-Jobs N]
```

Builds default to `<root>/build`, or `<root>/build-tests` for
`configure-tests.ps1`; `-BuildDirectory` overrides that location.

`acquire.ps1` downloads llvm-mingw 20260922 (Clang/lld 23.1.2, x64 UCRT), Ninja
1.13.2, SDL3 3.4.18, glslang 16.6.0 and Vulkan headers 1.4.363 from their
upstream release archives into `<root>/archives` and `<root>/deps`; no toolchain
installer is run. It verifies every pinned archive against its recorded SHA-256
before extraction. Vulkan links to the system's `vulkan-1.dll`. The scripts do
not install validation layers.

On a machine without a Vulkan loader (CI), `-VulkanLoader` also fetches
LunarG's 1.4.363 loader. Pass it to `configure.ps1` with `-VulkanLibrary` and
copy it beside the built executables.

The build needs llvm-mingw's clang. MSVC and other architectures are not
supported. `CMakePresets.json` still carries a `windows-x64` preset with another
build directory (`port/build/windows`); the scripts do not use presets.

## Tests

```powershell
$build = Join-Path $root 'build-tests'
./tools/windows/configure-tests.ps1 -Root $root -BuildDirectory $build
./tools/windows/build.ps1 -Root $root -BuildDirectory $build -Tests
ctest --test-dir $build -j 1 --output-on-failure
```

`build.ps1 -Tests` builds `darkchronicle_tests` when the test executable has that
name, and `darkcloud_tests` otherwise (`port/src/tests/CMakeLists.txt` still uses the second).

## What is verified and what is not

Verified on Windows 11 with PowerShell 7, CMake 4.4, Python 3.13, llvm-mingw
20260922 and Ninja 1.13.2:

- Every script parses, and `build.bat`, `build.bat windows-x64` and `kazarov.conf`'s
  Windows routing reach `configure.ps1` and `build.ps1`.
- CMake configures `windows-x64` and `windows-x64-release` with those dependencies.
- The host layer (`platform`, `gfx`, `audio`, `dc_host`) compiles.
- The disc extraction and the argument handling of `run.bat`, against a stand-in executable.
- The dependency archives that were used match the SHA-256 values in `acquire.ps1`.

Not verified:

- That the game compiles or links on Windows. It does not, yet; `docs/PC_PORT_PLAN.md`
  and the port's own sources are where that work is tracked. In particular
  `u_long` is 32 bits on Windows and `llvm-objcopy --weaken` and `ld.lld -r` do not
  support COFF.
- `acquire.ps1` on this checkout: it was not run, because it downloads.
- `configure-tests.ps1` and `build.ps1 -Tests` (GoogleTest is fetched with git).
- Everything that needs a running game: windowed or offscreen output, input, audio,
  saves, and the first-run flow.
- The `-VulkanLoader` path and CI use.
