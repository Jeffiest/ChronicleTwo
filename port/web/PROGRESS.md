# Web build progress log

Work package C of `DC2_PLAN.md`: ChronicleTwo in the browser, on the layer DC_Web built for Dark Cloud 1. This log is
for whoever continues it: what was done, what failed and why, and what was changed in Kazarov.

## Done

- `PLATFORM=WEB` in the top-level `CMakeLists.txt`; `web` configure and build presets in `CMakePresets.json`
  (`port/build/web`, Release, tests, Tracy, LTO and split symbols off). The preset's toolchain file is
  `port/web/toolchain.cmake`, which finds Emscripten from `EMSCRIPTEN_ROOT`, then `EMSDK/upstream/emscripten`, then the
  `emcc` on PATH, and includes Emscripten's own `Emscripten.cmake`. `emcmake cmake --preset web` works too.
- `port/web/web.cmake` (from DC_Web's, names `ct_web_*`): SDL3 3.4.18 and Vulkan-Headers 1.4.366 from git, the Vulkan
  stubs, the WGSL embed, `ct_vk_webgpu` and `ct_vk_gl` object libraries, the link options (preloads `glyphs/`, `fonts/`
  and the Luau `scripts/`), the page copy, the WebGPU probe and Kazarov's renderer tests (`kazarov_gfx_tests`,
  `kazarov_gfx_tests_gl`, not built by default). It includes `tools/kazarov/cmake/KazarovRust.cmake` itself, since
  `port/cmake/data.cmake` (dc2data) is skipped on the web, as are `crash.cmake` (cpptrace) and `movies.cmake` (FFmpeg).
- `port/CMakeLists.txt` hooks mirror DC_Web's: PIC off, `ct_web_host` (arena memory on the heap), `ct_web_vulkan`,
  `ct_web_port` (replacement units), `CT_WEB_LINK_OPTIONS`, `darkchronicle_gl` (`CT_WEB_GL_IN_ALL`, off by default),
  the `SDL3::Headers` alias fix.
- `port/web/src`: `memory.cpp`, `storage.cpp`, `exit.cpp` from DC_Web; `touch.cpp` (exports `ct_web_touch_screen`,
  `ct_web_touch_state`; `WebTouchOverlayPoll` called from `main.cpp`'s pump); `crash.cpp` (the Switch stub's shape,
  exits through `WebQuickExit`); `movie.cpp` (every `MoviePlayer` method a no-op: no FFmpeg); `firstrun.cpp` (DC2's:
  `data.zip`+`data.json` or `data/`, else a `disc.iso`/`disc/` extracted by `kazarov::extract_tracked_with_script_policy`
  on a thread with progress on stdout as `extract: <percent> <stage>`, else `kExitNoData`).
- `port/web/vulkan` and `port/web/gl` copied from DC_Web, with the one shader change between Kazarov `0dcfcdaa` and
  `fd72338a` ported: `draw_body.glsl`'s `kAemMask` (the palette slot as an AEM mask, flag 2048) and alpha function 8
  (RGB-only source-over). The newer renderer calls one more Vulkan command, `vkGetFenceStatus`, which the WebGPU layer
  already had.
- `port/web/site`: DC2's page. The data card takes a PAL disc image (`disc.iso`), `data.zip` with `data.json`, or a
  folder (the disc's `DATA.HD4`/`DATA.DAT`/`SOUND.HD3`/`SOUND.DAT`, a PC extraction's folder, or `data/` with
  `data.json`); the game starts with `--run-dir /opfs/chronicletwo`. The mods, PC downloads and animation editor cards of
  DC_Web's page are left out.
- Kazarov (`tools/kazarov`), all under `__EMSCRIPTEN__` or `if(EMSCRIPTEN)`:
  - `gfx/renderer.cpp`: `vkCreateCanvasSurfaceWEB` for the surface, `VK_KHR_surface` in place of SDL's instance
    extensions, `vkDestroySurfaceKHR` at shutdown.
  - `gfx/memory.cpp`: `vkWebGpuMarkWritten` on transient spans.
  - `sce/eekernel_threads.cpp`: fibers on `emscripten_fiber_*` (Asyncify), each with a 256 KiB Asyncify stack.
  - `platform/files.cpp`: `RenameExclusive` as check-then-rename (WasmFS has no exclusive rename or hard links).
  - `platform/window.cpp`: no `SDL_WINDOW_VULKAN`.
  - `platform/input.hpp`/`.cpp`: `KZ_TOUCH_OVERLAY` on the web; `WebTouchScreen()` weak default.
  - `include/platform/arena.hpp`: `WidenNear` returns the low word on 32-bit pointers (the shift by 32 is ill-formed).
  - `cmake/KazarovRust.cmake`: `Rust_CARGO_TARGET=wasm32-unknown-emscripten`, no default features (no eframe/rfd), no
    FFmpeg, and the standard library built by cargo (`-Zbuild-std=std,panic_abort` with `RUSTC_BOOTSTRAP=1`,
    `-Ctarget-feature=+atomics,+bulk-memory,+mutable-globals -Cpanic=abort`, `CFLAGS_wasm32_unknown_emscripten=-pthread`),
    because the prebuilt std has no atomics and wasm-ld refuses it in a `-pthread` link (verified with a test crate:
    the error is "--shared-memory is disallowed by ... because it was not compiled with 'atomics' or 'bulk-memory'").
    Rust's unwinding on Emscripten is WebAssembly exceptions (`__cpp_exception`), which Asyncify does not take, hence
    `panic=abort`. Needs the `rust-src` rustup component.
  - `src/c/movie_mux.c`: the coordinator's `write_packet_buffer` typedef for FFmpeg 6.1 (not compiled on the web).
- `port/src`: `main.cpp` (web `main`: mounts OPFS, `--env NAME=VALUE`, touch pad poll, render thread off, "the web" in
  the session header), `runtime.cpp` (`LIBC_NOEXCEPT` for musl), `platform/gamedata.cpp` (an iterator, not a pointer,
  from `std::ranges::find_if` over a `std::array`: libc++'s hardened mode wraps it), `include/mg_visual.hpp`
  (`sizeof(mgCFace)` is 20 on wasm32).
- `port/web/src/threads.cpp`: `pthread_setname_np`/`pthread_getname_np` (a thread-local name) and
  `sched_setaffinity` (`ENOSYS`) for quill's backend, which Emscripten's libc leaves out.
- `tools/web/build.sh` (configure and build with `CT_DEPS_DIR`), `tools/web/serve.py` (a static server with COOP/COEP
  and `application/wasm`), `tools/web/smoke.sh` (the Node and Chromium smoke runs without game data, below).
- `port/web/include/x86gprintrin.h`: shadows clang's for quill 13's `Rdtsc.h`, which includes it on every non-Arm
  platform and clang ships for every target; `__rdtsc` is the steady clock. The port's logger uses `ClockSourceType::User`.

## Building here

```
export EM_CONFIG=/root/.emscripten EMSCRIPTEN_ROOT=/opt/emsdk-dl/install/emscripten CT_DEPS_DIR=/opt/deps
export PATH=/opt/clang20/bin:/root/.cargo/bin:$PATH
tools/web/build.sh configure
tools/web/build.sh build darkchronicle
```

`CT_DEPS_DIR` holds pre-cloned sources (`corrosion`, `simdjson`, `fastgltf`, `luau`, `quill`, `googletest`, `SDL3`,
`vulkanheaders`) passed as `FETCHCONTENT_SOURCE_DIR_<NAME>`, because archive downloads from GitHub are blocked here.

## Failed and fixed

- `quill/core/Rdtsc.h`: `__rdtsc` undeclared on wasm32 (x86 intrinsics headers) -> the include shim above.
- `platform/arena.hpp`: `std::uintptr_t{1} << 32` not a constant expression on wasm32 -> `WidenNear` guard.
- `mg_visual.hpp`: `static_assert(sizeof(mgCFace) == 0x20)` -> pointer-size aware.
- `platform/gamedata.cpp`: `const auto *` from `std::ranges::find_if` -> `const auto`.
- `port/CMakeLists.txt`: `set_property` on the alias `SDL3::Headers` -> resolve `ALIASED_TARGET`.
- `config_loaders.cpp`, `luau_runtime.cpp`: the same `const auto *const` from `std::ranges::find_if` over a
  `std::array` -> `const auto` (libc++'s hardened `__wrap_iter`; the tree holds no other).
- Link: `pthread_setname_np`, `pthread_getname_np` and `sched_setaffinity` undefined, called by quill's backend
  (`BackendUtilities.h`, `ThreadUtilities.h`). Emscripten's libc declares them but skips musl's two
  `pthread_*name_np.c` (emscripten #12216) and has no `sched_setaffinity` -> `port/web/src/threads.cpp` defines
  them: a per-thread name for the thread itself, affinity `ENOSYS`.

## Not done

See docs/WEB.md "Status" and the end of this file as the work goes on.
