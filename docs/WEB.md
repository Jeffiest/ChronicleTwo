# The web build

ChronicleTwo builds its port to WebAssembly with Emscripten (`PLATFORM=WEB`, preset `web`), on the layer DC_Web built
for Dark Cloud: Vulkan on WebGPU and on WebGL2, WasmFS on the browser's storage, PS2 threads as fibers under
Asyncify, a page with an on-screen gamepad. This page records how it is built, what it changes, what of it is
Chronicle Two's own, and where it stands. `port/web/PROGRESS.md` is the working log behind it.

## Building

```bash
export EMSCRIPTEN_ROOT=/path/to/emsdk/upstream/emscripten   # Emscripten 6.0.13, as DC_Web pins; or source emsdk_env.sh
rustup component add rust-src                              # the standard library is built for wasm32 (below)
rustup target add wasm32-unknown-emscripten
tools/web/build.sh configure                               # cmake --preset web
tools/web/build.sh build darkchronicle                     # cmake --build --preset web --target darkchronicle
tools/web/serve.py                                         # http://127.0.0.1:8000/, with the headers the page needs
```

`tools/web/build.sh` finds Emscripten from `EMSCRIPTEN_ROOT`, then `EMSDK`, then `emcc` on PATH, and passes each
subdirectory of `CT_DEPS_DIR` (pre-cloned `corrosion`, `simdjson`, `fastgltf`, `luau`, `quill`, `googletest`,
`SDL3`, `vulkanheaders`) as `FETCHCONTENT_SOURCE_DIR_<NAME>` for machines that cannot download archives;
`CT_WEB_JOBS` sets the compile jobs. The build needs `glslangValidator` on the host (the Vulkan shaders are still
compiled to SPIR-V) and Node, which `.emscripten` names.

The output is `port/build/web/darkchronicle.js`, `darkchronicle.wasm` and `darkchronicle.data` (`glyphs/`, `fonts/`
and the Luau `scripts/`, preloaded), the game drawing with WebGPU, with the page (`port/web/site/`: `index.html`,
`shell.js`, `sw.js`) copied beside them, so the build directory is the site. `darkchronicle_gl`, the same game
drawing with WebGL2, is a second target (`-DCT_WEB_GL_IN_ALL=ON` builds it with `all`). Linking is the heavy step:
Asyncify and `wasm-opt` over the whole game take several minutes and most of the memory, so link one target at a
time (`web.cmake` puts the links in a job pool of one).

`-DCT_WEB_PROFILING=ON` (`tools/web/build.sh configure -DCT_WEB_PROFILING=ON`) links with the functions' names
kept, so a browser's profiler says which of the game's functions the time went to; the files are a few megabytes
larger and the game is otherwise the same.

Kazarov's renderer tests for the two layers are `kazarov_gfx_tests` and `kazarov_gfx_tests_gl`
(`port/web/test/gfx_tests.html`), not built by default.

### Smoke runs

`tools/web/smoke.sh` runs the build without game data, as CI would:

- `node`: `node darkchronicle.js --headless --offscreen --frames 30 --save <dir>` under Node, which runs the
  runtime, WasmFS, the fibers and the host's setup up to the game data check, where the game logs what to put in
  the browser and exits with 3 (`kExitNoData`, `port/src/exitcodes.hpp`).
- `browser`: the page in headless Chromium (Playwright) on SwiftShader's WebGPU, from `tools/web/serve.py`; the
  driver (`port/web/test/browser_smoke.mjs`) stores a stand-in `data.zip` in the origin private file system as the
  page would, starts the game and waits for the page's `exit=<code>` line. `--renderer gl` runs `darkchronicle_gl.js`;
  `--with-marker` stores a current `data.json` too, so the game gets past the data check, creates its device,
  compiles its pipelines and runs the frames on the stand-in data (exit 0).

Both check the exit code (0 or 3), the log's session header and missing-data message, and that no `abort:`,
`RuntimeError`, `pageerror` or "signature mismatch" was printed. `tools/gauntlet` (GAUNTLET.md) runs the same two
checks as its `web-node` and `web-browser` stages.

## What the web build changes

All of it is in `port/web/`, `tools/web/` or behind `__EMSCRIPTEN__` / `if(EMSCRIPTEN)`:

- **CMake**: `PLATFORM=WEB` in the top-level `CMakeLists.txt` with the `web` presets (`port/build/web`, Release,
  tests, Tracy, LTO and split symbols off) and `port/web/toolchain.cmake`; `port/web/web.cmake` builds SDL3 3.4.18
  and takes Vulkan-Headers 1.4.366 from git, generates the Vulkan stubs and the WGSL embed, defines the
  `ct_vk_webgpu` and `ct_vk_gl` object libraries, the link options, the page copy, the WebGPU probe and the
  renderer tests. `port/cmake/data.cmake` (dc2data), `crash.cmake` (cpptrace) and `movies.cmake` (FFmpeg) are
  skipped on the web; `web.cmake` includes `KazarovRust.cmake` itself.
- **Vulkan on WebGPU** (`port/web/vulkan/`) and **Vulkan on WebGL2** (`port/web/gl/`), DC_Web's layers: the Vulkan
  commands Kazarov's renderer calls, implemented on WebGPU (Emscripten's `emdawnwebgpu` port) and on WebGL2, so the
  renderer runs unchanged; weak stubs that fail stand for every other command. DC_Web's `docs/WEB.md` describes
  both layers (the dynamic-state pipeline variants, push constants as a storage buffer, the swapchain copy, the
  hand-ported WGSL and GLSL ES shaders, Mali's waits, the `_noclip` variants). The one shader that changed
  between the Kazarov DC_Web ports and this tree's (`draw_body.glsl`: the palette slot as an AEM mask, flag 2048,
  and alpha function 8, RGB-only source-over) is ported in both. When a GLSL shader in Kazarov's `gfx/shaders/`
  changes, its WGSL and GLSL ES ports must follow.
- **PS2 threads** (`sce/eekernel_threads.cpp`) are Emscripten fibers, each with a 256 KiB Asyncify stack
  (`-sASYNCIFY`).
- **The game runs on a pthread** (`-sPROXY_TO_PTHREAD`), so its blocking waits do not stall the page, and the
  renderer stays on it (render thread off): WebGPU's objects belong to the thread that made the device. Hosting
  needs cross-origin isolation (COOP/COEP headers, which `tools/web/serve.py` sends, or `sw.js` on hosts that
  cannot, GitHub Pages among them). The canvas is handed to the game's thread as an OffscreenCanvas.
- **Arenas** are heap blocks (`port/web/src/memory.cpp`); WebAssembly has no page protection. `WidenNear`
  returns the low word on 32-bit pointers (`platform/arena.hpp`).
- **Kazarov's Rust core** is built for `wasm32-unknown-emscripten` with cargo building the standard library
  (`-Zbuild-std=std,panic_abort`, `RUSTC_BOOTSTRAP=1`, `+atomics,+bulk-memory,+mutable-globals`): the prebuilt
  std has no atomics and wasm-ld refuses it in a `-pthread` link. Rust unwinds with WebAssembly exceptions, which
  Asyncify does not take, hence `panic=abort`. No default features (no eframe, rfd or FFmpeg).
- **Exceptions** are Emscripten's JavaScript-based ones (`-fexceptions`); native wasm exceptions do not work with
  Asyncify.
- **Game data** is the player's own, from their PAL disc. The page copies what they choose into the origin
  private file system under `chronicletwo/` once: a disc image (`disc.iso`), the disc's `DATA.HD4`, `DATA.DAT`,
  `SOUND.HD3` and `SOUND.DAT` (`disc/`), or a PC extraction (`data.zip` with `data.json`, or `data/` beside
  `data.json`). A disc image is looked at before it is copied (`discFault`): one without `DATA.HD4` and
  `DATA.DAT` in its root, or shorter than its volume says, is refused there instead of at the game's start. The
  copy goes 8 MiB at a time (`copyFiles` in `port/web/site/shell.js`), with the status line
  saying how far it is, the screen kept on, and an error in place of a bar that stands still when the browser runs
  out of room or goes 30 seconds without finishing a read or a write. The game mounts that file system at `/opfs` through WasmFS (`port/web/src/storage.cpp`) and starts
  with `--run-dir /opfs/chronicletwo`, so `data.zip` is read in place and saves, `config.json` and the logs live
  beside it. A disc is extracted at the game's first start by Kazarov's extractor, as `dc2data` does on a PC, on a
  thread of its own with progress on stdout as `extract: <percent> <stage>`, which the page shows over the canvas
  (`port/web/src/firstrun.cpp`). Movies are left out of the extraction (a build without FFmpeg plans no movie
  job, `plan_stream` in `tools/kazarov/src/rust/audio.rs`) and `MoviePlayer` is a no-op
  (`port/web/src/movie.cpp`): the browser has no FFmpeg. Half the device's processors
  convert at once, two to eight of them (four on a phone with eight), or as many as `CT_WEB_EXTRACT_JOBS` says
  (1 to 8, `?args=--env+CT_WEB_EXTRACT_JOBS=4`); the log has a line for each stage with the seconds so far and
  the size of the program's memory, and one for how long the extraction ran. The build's allocator is mimalloc
  (`-sMALLOC=mimalloc`, `port/web/web.cmake`): the default one has a single lock for every thread, and with it
  four conversions at once were slower than two. The program's memory only grows in a browser, so one large read
  sets what the whole extraction holds: the index, the scripts' plan and the packs' plan each read every disc
  file whole to look at its first bytes, the 970 MB sound archive and the movies among them, and now ask for
  those bytes alone (`ResourceRef::head`), which took the memory reached from 3.3 GB to 2.3 GB.
  `DC2DATA_PROFILE_MEMORY` set in the environment has the extractor say on stderr how large the memory is after
  each step of the plan and at each read of 32 MiB or more. WasmFS reads an OPFS file opened only to read through a Blob, an asynchronous round trip for
  every read, and gives a file opened to read and write a synchronous access handle: the disc image and
  `data.zip` are opened to read and write for that reason (`open_read` in `tools/kazarov/src/rust/archive.rs`;
  nothing is written through it), which took the stages that read the disc before any conversion from 200 s to
  17 s and is also how the running game reads its stored archive. On the web the image's fingerprint in
  `data.json` is a hash of 64 samples of it and not of all 4.7 GB, and the finished archive is not read back to be
  verified (`DC2DATA_VERIFY` set in the environment brings that back). The page holds a screen wake lock while it runs, since a phone that sleeps stops the page, and
  once a disc is extracted it offers to remove the disc image, which the game no longer reads. Unlike DC_Web's
  Dark Cloud the game cannot read the disc as it goes: it reads converted data (glTF, PNG, WAV, JSON), and the
  extractor plans textures, sound and models over the whole disc before it converts any of it. OPFS needs
  Chromium or Firefox.
- **Saves**: Kazarov's `FilesWrite` commits a new file with an exclusive rename or a hard link, neither of which
  WasmFS has; on the web it checks the name is free and renames (`platform/files.cpp`).
- **Crashes** exit through `WebQuickExit` (`port/web/src/crash.cpp`, the Switch stub's shape); there is no
  cpptrace.
- **On-screen gamepad**: on a touch screen (`pointer: coarse`) the page draws a PS2 pad over the game and hands
  its state to the game as Android's overlay does (`port/web/src/touch.cpp`, `KZ_TOUCH_OVERLAY`,
  `WebTouchOverlayPoll` from `main.cpp`'s pump). The game says when to show it.
- **The page** (`port/web/site/`): `shell.js` stores the data and starts the game, which fills the window, scaled to
  fit, with Fullscreen, Log and Capture buttons and the extraction's progress over it; the previous run's last
  lines are kept in `localStorage` for a tab the browser killed. `?args=--headless+--frames+30` passes extra
  arguments and `--env NAME=VALUE` among them sets an environment variable (`DC2_LOG=debug`, say), as a browser has
  no other way to. The page starts `darkchronicle_gl.js` where there is no WebGPU and on Arm's Mali GPUs, and
  `darkchronicle.js` elsewhere; `?renderer=gl` or `?renderer=webgpu` chooses, and the log's first lines say which
  ran on what GPU. SDL sizes the canvas by `devicePixelRatio`, about three on a phone; a touch screen draws at no
  more than 1.5 pixels to the CSS pixel and `?scale=1` or `?scale=3` chooses (`pixelRatio` in `shell.js`).
- **Frames**: a browser has no present mode to hold the game to the screen, and without a limit the game drew
  three frames for each one shown. The page counts the screen's refresh rate from half a second of its frames
  and passes it as `CT_WEB_REFRESH`; `PresentSettingsFor` (`main.cpp`) makes it the most frames a second, 60
  where the page could not tell, unless the configuration names a limit. That limit is kept by timed sleeps,
  and a browser's are no finer than its system's timer: in Chrome on Windows a sleep of 1 ms ends up to 15.6 ms
  later, so a town (two ticks to a game frame, with a display between) showed 34 frames a second of 60 while
  the thread slept three quarters of the time. The game's thread is a worker with animation frames of its own,
  which arrive on time, so once the first has come (`ClockHostFrame`, `platform/clock.hpp`) the wait for a tick
  ends on the first frame at or after it, one display goes to each frame (`BetweenTicks` in `present.cpp`), and
  the screen's rate limits nothing more; a limit the configuration names still holds. A hidden page gets no
  frames, and the game then goes on at a tenth of a second a step. Each present ends by letting the
  browser have its turn through a message to the thread itself (`YieldToBrowser` in each layer's `device.cpp`),
  as a timer of no length is held to 4 ms. The WebGL2 layer keeps the images that are never drawn into (the
  renderer's scratch image for a copy within one image, several a frame in a menu) for the next of the same
  format and size, and asks GL for an error only after making an image of 1 MiB or more (`objects.cpp`): each
  such question waits for the browser's GPU process. The logger's thread wakes every 10 ms, not every 1 ms.
- **Data from the server**: `tools/web/serve.py --data DIR` serves a `dc2data` extraction at `gamedata/`, and the
  page then plays with it in place of stored data: `storage.cpp` mounts `data.zip` on a WasmFS fetch backend of
  64 KiB chunks, kept in memory up to 192 MiB (`port/web/src/fetch_cache.js`), each request reaching at least
  128 KiB so an entry's header and a small file come in one. A phone has taken a fifth of a second for each
  request however small, where a desktop takes a millisecond, so what counts is how few requests a load makes.
  A read that starts where the last request ended or within 1 MiB after it is a part of the file being read
  onward (a large file read straight through, or the first bytes of each texture of a folder): each such
  request reaches twice as far as the one before, up to 2 MiB, and once a read is past the middle of what was
  last asked for, the stretch after it is asked for without waiting, a read that needs it waiting for that
  answer and not a second one. Four such parts of a file are followed at once. The requests are not kept by
  the browser's own cache (`no-store`), which gives one address to one request at a time. The server keeps a
  device's connection open with answers that leave at once (`TCP_NODELAY`), answers an unchanged build with a
  304, and logs each range's size and speed, the connection it came on (the device's port and how many ranges
  that connection has carried), how long after the last answer it was asked for and, on Windows, the
  connection's round trip, window, bytes sent again and timeouts: a device that waits long between requests on
  a connection with a short round trip is slow itself, not its network. `--tls DIR` adds https for the other
  devices on the network, and `--compress` serves gzip copies of the build.
- **A device's log on the server**: a page playing from the server's data posts its log's lines to the server's
  `client-log` every two seconds, and the server writes them in its own log as `client: ...` beside that
  device's requests. Among them are a line for the device (processors, memory, the screen and its rate), the
  times of reads of the server's data as the game starts (`network: ...`, from `probeNetwork` in `shell.js`:
  of one byte, 128 KiB and 1 MiB one after another, six at once, two after a pause and two the browser's cache
  may keep, each as the page saw it and as the browser's network clock did), the game's rates each second
  (`--log-fps`, which such a page passes; these go to the server and not to the page's log), and every five
  seconds the reads' count, how many of them were sent ahead, their size and the time the game waited on them
  (`fetch: ...`, from `fetch_cache.js`). A site without the server
  answers the first post with something other than 204 and the page stops sending.
- **Several games on one site**: a `games.json` beside the page, `[{"name": "Dark Cloud", "href": "../"},
  {"name": "Dark Chronicle", "href": "./"}]`, puts a row of links above the title, the entry for this folder
  marked (`showGames` in `shell.js`). DC_Web's site writes one for each of its games; a site with this game alone
  has none and shows no row.
- **Libraries**: quill's `Rdtsc.h` includes `x86gprintrin.h` on every non-Arm target, which clang ships but wasm32
  cannot use; `port/web/include/x86gprintrin.h` shadows it and the port's logger uses quill's user clock. quill's
  backend calls `pthread_setname_np`, `pthread_getname_np` and `sched_setaffinity`, which Emscripten's libc declares
  but leaves out; `port/web/src/threads.cpp` defines them. `runtime.cpp` defines `LIBC_NOEXCEPT` for musl.
- **32-bit pointers**: `sizeof(mgCFace)` is 20 (`mg_visual.hpp`); `std::ranges::find_if` over a `std::array`
  returns libc++'s wrapped iterator in hardened mode, not a pointer (`gamedata.cpp`, `config_loaders.cpp`,
  `luau_runtime.cpp`).

## What is Chronicle Two's own

Against DC_Web's layer for Dark Cloud, this tree's web build differs in:

- **The data**: a Dark Chronicle PAL disc, `DATA.HD4`/`DATA.DAT`/`SOUND.HD3`/`SOUND.DAT` or a `dc2data` extraction
  (`data.zip` with `data.json`, whose `data_version` must be current), stored under `chronicletwo/`, with the game
  started by `--run-dir`; a disc is extracted by Kazarov's Rust extractor in the browser (DC_Web's game reads Dark
  Cloud's disc in place instead). The extractor is why the Rust core is linked into the game, and why the
  standard library is built for wasm32 with atomics.
- **Kazarov** is this tree's vendored copy (`tools/kazarov`, upstream `fd72338a` plus MoonMod's patch), newer than
  DC_Web's; its Emscripten patches were re-applied to it (`port/web/PROGRESS.md` lists each). `draw_body`'s
  shader ports carry the newer AEM mask and alpha function.
- **No movies** on the web (`movie.cpp`), where DC_Web had none to stub.
- **MoonMod** is applied in-tree, so its Luau mods load as on the desktop; the page has no Mods card yet (below).
- **Left out** of DC_Web's page: the Mods, PC downloads and animation editor cards, the built-in mods, the co-op
  transport (work package D).

## Status

| Stage | State |
| :-- | :-- |
| Compiles and links to wasm (`darkchronicle`, WebGPU) | Done: `darkchronicle.wasm` 26.3 MB, `.js` 662 KB, `.data` 37.8 MB; the link about 3 min 15 s |
| `darkchronicle_gl` (WebGL2) | Done: `darkchronicle_gl.wasm` 26.2 MB, `.js` 639 KB; its Chromium smoke passes (`--renderer gl`), and with `--with-marker` it runs 30 frames and exits 0 cleanly |
| Smoke run under Node without data (exit 3) | Passes (`tools/web/smoke.sh node`; a stand-in canvas lets the game's thread start, `port/web/src/node_canvas.js`) |
| Smoke run in headless Chromium (SwiftShader) | Passes on WebGPU (`tools/web/smoke.sh browser`): the page isolates, stores the archive, the game reports the missing data, `exit=3`. With a current `data.json` (`--with-marker`) the game creates the WebGPU device, compiles 21860 pipelines and runs 30 frames on the stand-in data |
| Game data in the browser: disc image, disc files, `data.zip` or `data/` | Played from a `dc2data` extraction, stored and from the server (`--data`), on a PC and on an Android phone. A disc image extracted on the page in headless Chrome on a PC: the 4.7 GB image copied in 20 s and the extraction ran 78 s with eight conversions at once (8 s to the first conversion, 28 s of textures, sound and packs, 41 s of models), 122 s with four and 220 s with two; it was 460 s with two and the default allocator, and 1123 s while the image was also read through a Blob, hashed whole and the archive verified. The program's memory reached 2.3 GB (0.6 GB with the disc indexed, 2.1 GB with the textures planned) and Chrome's process 2.1 GB, `data.zip` came to 2232 MiB without movies, and the game started from it, with the image and after the page removed it |
| Renderer on WebGPU / WebGL2 with the game's frames | Both draw the title, the towns, the dungeon and the events. On WebGL2 a copy within one image (the eyes' texture animation) read the frame instead until the layer kept the read framebuffer binding (`FramebufferFor`, `port/web/gl/objects.cpp`) |
| Kazarov's renderer tests in the browser (`kazarov_gfx_tests`) | Not built here yet |
| Saves in the browser's storage | Written (WasmFS on OPFS); not checked with a save |
| Mods on the page (MoonMod's Mods card), co-op | Not done |
| Hosting | DC_Web's `web.yml` builds this tree at the commit its `port/web/dc2.json` pins and publishes it beside Dark Cloud, at `dc2/` of its GitHub Pages site |

## Not done, and the next steps

- **The exit path.** Emscripten's `exit()` runs the C `atexit` handlers on the page's main thread
  (`exitRuntime`), not the game's; the host's shutdown is those handlers, and the renderer's ran off the device's
  thread (`webgpu: vkDeviceWaitIdle off the device's thread`, an abort after the exit code was reported). On the
  web the frames exit (`mglib.cpp`) and a closed window (`main.cpp`, `PumpHost`) call `WebExit`
  (`port/web/src/exit.cpp`), which runs the handlers on the game's thread first. Checked by the `--with-marker`
  smoke: both layers now end `exit=0` with no abort. Other `std::exit` calls (usage errors, the
  missing-data exit) happen before the renderer exists and are left as they are.
- **A disc image on a phone.** The page's own extraction of a disc image (`extract:` progress, `firstrun.cpp`)
  runs to the end on a PC; it is still to be run through on a phone, where the image and its extraction both have
  to fit the browser's storage (about 7 GB until the image is removed) and the extraction's 2.3 GB of memory
  (2.1 GB of it reached while the textures are planned, which is the next thing to bring down) may be more than
  the browser gives a page. Converting the whole disc is about 510 seconds of processor time in a native release
  build on a desktop (`dc2data extract --jobs 2`, movies included) and 78 s on a desktop's eight threads in the
  browser, so no phone does it in a minute: a phone is served in seconds by the server's data
  (`--data`) or a `data.zip` with its `data.json` made on a PC and picked on the page. Starting from a disc
  image that fast needs the game to convert a file when it first asks for it, which the extractor cannot do yet:
  the archive's index is every converted name, and textures and models are planned across the whole disc.
- **Kazarov's renderer tests** (`kazarov_gfx_tests`, `kazarov_gfx_tests_gl`) in Chromium: `cmake --build --preset web
  --target kazarov_gfx_tests`, then `port/web/test/gfx_tests.mjs`. DC_Web's CI runs them on both layers; this tree's
  renderer is newer and the layers were ported, so the suite is the check that the port holds.
- **The page's Mods card** (MoonMod's mods from a folder or zip into `saves/mods/`, as DC_Web's page does with
  `zip.js`) and **co-op** over the relay (work package D).
- **Movies** stay out (no FFmpeg in the browser); **main-depth copies** are not offered by the WebGPU layer, as in
  DC_Web.

## Watch for

wasm-ld turns a call whose type differs from its definition into a trap, where native linkers let it through.
Grep the link log for "function signature mismatch"; each one is a crash waiting at that call. The gauntlet's
`web-browser` stage stores the stand-in archive under `chronicle/` (DC_Web's directory) where this page reads
`chronicletwo/`; see PROGRESS.md.
