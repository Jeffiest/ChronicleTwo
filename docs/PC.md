# PC port

The PC target builds the PAL release (`SCES_511.90`) as a native x64 application. It runs the game's original C++ code with host replacements for platform services and PS2 hardware paths. SDL3 provides the window and input layer; Vulkan provides rendering. The current work is a native port with compatibility checks, not a completed gameplay release.

## Windows build

The Windows scripts expect an external dependency root containing the toolchain and libraries under `deps/`. Set it to the Windows dependency directory used by your checkout:

```powershell
$depsRoot = 'C:\path\to\externaldepsRoot\windows'
$buildDir = (Resolve-Path .).Path + '\build\windows'

./tools/windows/configure.ps1 -Root $depsRoot -BuildDirectory $buildDir
./tools/windows/build.ps1 -Root $depsRoot -BuildDirectory $buildDir
```

The scripts use PowerShell 7, CMake, Python 3, Ninja, llvm-mingw/clang, SDL3, Vulkan headers and loader, and glslang from that dependency tree. The configure script defaults to Debug and `CT_BUILD_TESTS=OFF`. Pass `-BuildType Release` or `-Tests` as needed. `build.ps1` builds `darkchronicle`; `-Tests` also builds `darkchronicle_tests`. Runtime DLLs are copied next to the executables.

To configure with the GoogleTest target enabled and build it:

```powershell
./tools/windows/configure.ps1 -Root $depsRoot -BuildDirectory $buildDir -Tests
./tools/windows/build.ps1 -Root $depsRoot -BuildDirectory $buildDir -Tests
ctest --test-dir $buildDir --output-on-failure
```

`darkchronicle_tests` contains the game's GoogleTest cases and shared Kazarov tests. The custom targets `dc_compatibility_tests` and `dc_compatibility_tests_headless` run focused host-compatibility checks. The headless target skips color-capture cases that need a Vulkan device; it covers 25 cases. The full target covers 26 cases. These targets are available after configuring the PC build:

```powershell
cmake --build $buildDir --target dc_compatibility_tests_headless
cmake --build $buildDir --target dc_compatibility_tests
```

The core build targets are `ct_host` (shared host services), `ct_port` (ChronicleTwo's host implementation), and `ct_ps2` (the game sources compiled for the host). Build `darkchronicle` to link the executable. Windows CI and exact dependency versions are described by the scripts under `tools/windows/` and `.github/workflows/pc.yml`.

## Running

The executable needs extracted PAL game data. Supply its directory with `--data` or `DC_DATA`; the program reports a missing-data error if it cannot find the files. Example:

```powershell
& "$buildDir\darkchronicle.exe" --data 'C:\path\to\extracted-data' --save "$buildDir\save"
```

The current options are:

| Option | Purpose |
| --- | --- |
| `--data DIR` | Select the extracted game data directory. Defaults to `DC_DATA`, then local `data` directories and the platform data location. |
| `--save DIR` | Select saves, `config.json`, and the pipeline cache directory. Defaults to `DC_SAVE` and platform save locations. |
| `--headless` | Render without a visible window. Uses a Vulkan headless surface when available; otherwise uses offscreen rendering. |
| `--offscreen` | Render without a Vulkan surface, for image-only runs. |
| `--frames N` | Stop after N main-loop frames. |
| `--screenshot PATH` | Save the last rendered frame as a PNG on exit. |
| `--input FILE` | Drive pad 1 from an input script; defaults to `DC_INPUT`. |
| `--width W`, `--height H` | Set the window size; defaults come from `config.json` or the monitor. |
| `--jump MODE[:MAP]` | Start at `title`, `menu`, `edit[:map]`, or `dungeon[:map]`, skipping warm-up. |
| `--fast-load` | Requests shorter loading-screen holds and fades. The loading-screen seam is not implemented yet, so the option currently warns and has no effect. |

Environment variables include `DC_DATA`, `DC_SAVE`, `DC_INPUT`, `DC_AUDIO`, `DC_AUDIO_WAV`, and `DC_FAST_LOAD`. Set `DC_AUDIO=off` to disable audio output, or `DC_AUDIO_WAV=PATH` to write the mix to a WAV file. Headless runs disable audio unless an explicit audio setting is supplied. The game identity uses the `DC` settings prefix and the `chronicle-two` per-user directory.

`--headless --frames N --screenshot PATH` is useful for bounded rendering checks. Such a run exercises startup and the selected game loop; it does not by itself establish that every gameplay system works.

## Current gameplay status

The user has confirmed that the full prologue runs, Monica can be controlled, and the Options menu opens. These are useful end-to-end observations. Gameplay has not been re-proven after the upstream merge, so treat them as prior confirmed behavior pending a fresh launch on the merged branch.

Known unresolved issues include a crash involving shared model memory in Items and an attack failure in combat. They remain open gameplay issues; compatibility tests do not demonstrate that they are fixed. Do not assume that successful compilation or compatibility checks establish full gameplay correctness.

## Code and build structure

- `port/src` contains the host entry point, runtime replacements, and ChronicleTwo-specific host logic. `port/include` holds port headers and compatibility declarations.
- `ps2/src` and `ps2/include` contain the game's native sources and headers. The PC build compiles the game sources alongside the host code, using `PORT_OVERRIDE` definitions and generated host-compatible source transformations.
- `tools/kazarov/port` supplies shared SDL, Vulkan, audio, platform, and SDK replacement code. ChronicleTwo also has host overrides where its requirements differ.
- `port/CMakeLists.txt` defines `ct_host`, `ct_port`, `ct_ps2`, the executable, and the focused compatibility targets. `CT_BUILD_TESTS` enables `darkchronicle_tests`.
- `scripts/port/compatibility_audit.py` and related checks verify the host-specific source transformations and compatibility inventory.
- `docs/port/UPSTREAM_SYNC.md` records the upstream merge audit and its validation status.

## Platform and validation limits

The documented build and test workflow in this guide is Windows. Linux and macOS build routes inherited from upstream have not been verified on this merged branch. Retail matching was not run for this integration. A successful Windows build and passing compatibility checks are evidence for those checks only; they do not establish Linux/macOS support, byte-matching, or complete gameplay.
