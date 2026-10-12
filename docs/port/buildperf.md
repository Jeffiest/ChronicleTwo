# Build and test loop timings

Measured on the development machine (Windows 11, 32 threads, RTX 4090, Docker Desktop for the Linux container), 12-13 Oct 2026, JOBS=8, with four other streams building concurrently and six unrelated long-running `find` processes on the host. Process creation was slow (Git Bash fork 0.2-0.9 s, Python start 0.16-1.4 s), so absolute numbers are noisy. Each figure is the median of 3 runs unless marked (1 run). "Edit" means appending a comment line to the file, then building.

## Before and after

| Scenario | Before | After | Note |
|---|---|---|---|
| Windows debug, `build.bat windows-x64`, edit one ps2/src unit (map.cpp) | 12.3 s | 4.8 s | One Python process per copy instead of three; the check reparses only the changed unit |
| Windows, edit a widely included ps2/include header (mg_frame.hpp, ~34 dependents) | 6.5 s but WRONG: dependents were recompiled only by the next build (32 s) | 24.7 s, correct in one build | Mirror headers are now declared outputs of the mirror step |
| Windows, add or remove one PORT_OVERRIDE (copy step only) | 82 s (all 149 units recopied) | 4.1 s (check + the one affected unit) | Per-unit overrides lists |
| Windows, no-op | 2.8 s | 2.3 s | |
| Windows, edit port/src/mglib.cpp | 10.1 s | not re-measured | Unchanged path |
| Windows, edit tools/kazarov/port/gfx/draw.cpp | 6.4 s | not re-measured | Unchanged path |
| Windows headless run to the title (2500 frames, title.input) | 19.1 s | 18.1 s (1 run); 15.2 s with SDL joystick backends disabled by environment | Screenshot byte-identical |
| Linux tests, `ctest -j4` | 53.1 s (778 cases) | 92.2 s (787 cases, 1 run, loaded machine) | No change intended |
| Windows tests, `ctest -j4` | 99.5 s (783 cases) | 119.1 s (792 cases, 1 run, loaded) | No change intended |

## Baseline only

Before figures, not changed by this stream.

| Scenario | Measurement |
|---|---|
| Windows debug clean build (configure + build) | 225.5 s; ninja wall 175 s; job-seconds: per-unit copies 580, ct_ps2 compile 485, ct_port 176, ct_host 89; game link 1.4-2.6 s |
| Windows release clean build | 147.0 s (1 run) |
| Windows tests configure, build (clean) | 47.9 s configure, 173.9 s build |
| Linux debug clean build (`./build.sh linux-x64`, game + tests + dc2data) | 195.9 s: configure 23.0 s + generate 34.2 s, ninja wall 65.6 s; job-seconds: copies 75, ct_ps2 55, ct_port 65, ct_host 51, tests 183; links: game 2.8 s, tests 4.0 s |
| Linux no-op `./build.sh linux-x64` | 61.0 s |
| `kazarov shell true` | 14.5 s (8.7-20.5) |
| `kazarov shell ninja -C port/build/pc` no-op | 30.5 s |
| Linux edits through `./build.sh linux-x64` | port/src 20.4 s; ps2/src 22.8 s; Kazarov gfx 19.8 s; header 19.4 s (stale, as on Windows; the second build 45.4 s) |
| Linux headless run to the title (lavapipe, data bind-mounted by hand) | 46.4 s |
| Windows startup | 3-4 s before the first frame, of which ~2 s is SDL joystick/gamepad enumeration. With SDL_JOYSTICK_HIDAPI/RAWINPUT/WGI/DIRECTINPUT=0 the device is up at 0.8-1.8 s instead of 2.8-4.0 s. 840 pipelines ready in 0.30 s (pipeline cache already persisted); data index 0.2 s |
| Release vs debug at runtime | The 2500-frame title run takes 4.6-4.9 s in windows-x64-release and 16-18 s in debug (-O0); screenshot byte-identical |
| Slowest tests, Linux | Truncations.Check 35.5 s; Records.Check 20.5 s; then GPU cases at 1.6-2.7 s (GfxMeshDc2 22 cases 31.8 s, Vu1Prog0Shader 8 cases 12.9 s, MgVisual 12.9 s) |
| Slowest tests, Windows | PresentIdentity 5.9 s; GfxMeshDc2 cases 2.2-3.4 s each (53 s total); Smoke 3.0 s; GfxMisc.PipelineCache 2.8 s |
| Test time totals | Sum of case times is 362 s for 783 cases; 132 cases of at least 1 s account for 231 s |
| Per-case overhead | A trivial case costs 0.14-0.6 s under ctest but 0.05 s when the test binary is run directly. `ctest -R '^MgMath\.'` takes 16.7 s at -j1 and 3.2 s at -j16 |

## Per-stage costs

- Python start is 0.16-1.4 s per process on this host (imports 24 ms; the rest is process creation). The old per-unit copy ran three processes (strip 0.9 s, header_fixups 0.8 s, long64 0.85 s for map.cpp); it now runs one.
- `port_override.py check` over all 149 units: 3.5 s, on the path of every ps2/src edit. With the new per-unit cache it is 0.4 s of work plus process start when one unit changed. `list`: 0.9 s.
- Container: `docker run --rm` 0.7-5 s; `docker exec` into a running container 0.2 s. The Git Bash driver adds 6-15 s of fork overhead per `kazarov` command.
- Inside the container every ninja run pays ~2,229 stats of bind-mounted files at ~2 ms each (4.6 s), and the CONFIGURE_DEPENDS glob check (2.0 s; 0.3 s on the Windows host).
- No compiler cache (ccache/sccache) is installed on the host or in the image.
