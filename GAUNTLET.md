# The gauntlet

The verification an agent runs without a person before any work package in DC2_PLAN.md is called done.
It mirrors what Dark Cloud 1 had (MoonMod's `GAUNTLET.md` gate loop and `verify-headless.ps1`): gates
worked in order, each with automated checks, a headless part and, where a check needs eyes, a line marked
*(hands)* for a human. One runner does all of it:

```
python3 -I tools/gauntlet/run.py                 # every gate in order; stops at the first red gate
python3 -I tools/gauntlet/run.py --gate G0       # one gate (repeatable)
python3 -I tools/gauntlet/run.py --stage ctest   # one stage (--list names them)
python3 -I tools/gauntlet/run.py --json          # print only the report path
python3 -I tools/gauntlet/run.py --no-build --keep-going   # survey every gate against the binaries as built
python3 -I tools/gauntlet/run.py --keep-going --last toolkit-limits --last toolkit   # the disk-hungry cargo build last
python3 -I tools/gauntlet/test_gauntlet.py       # the runner's own tests
python3 -I tools/gauntlet/export_moonmod_patch.py --check OUT.patch   # MoonMod's patch, checked at the pin
```

The runner is plain Python 3 (standard library only, `python3 -I`). Toolchain paths and the exact cmake
flags live in `tools/gauntlet/env.sh`, which a person sources (`ct_configure`, `ct_build`) and the runner
reads, so there is one source for both. The filters a stage runs (gtest patterns, the cargo test, the smokes,
the stable log markers) are in `tools/gauntlet/stages.json`, not in the code.

Other options: `--web-build DIR` and `--coop-build DIR` point G3 and G4 at package C's and D's builds until
those branches are merged; `--last STAGE` (repeatable) runs a stage after all the others; `--min-free-gb N`
(default 4) keeps the build and toolkit stages from starting on a nearly full disk, and
`--min-free-gb-tests N` (default 1) the ctest stages (`ctest`, `capacity`, `content`, the co-op ctest),
which run binaries already built and write only logs and temporary saves: either is an error with the free
space in the summary (`0` turns the check off).

Nothing here needs a disc to pass. A check that needs retail data (G5) runs only where `DC2_DATA`, `DC2_ISO`
or `rom/pal/*.iso` is present and is reported as **not run** otherwise, never as passed.

## Gates

| Gate | Package | Stages | Green when |
|---|---|---|---|
| G0 Toolchain, build, static | F (and G after a merge) | `env`, `build`, `ctest` | the environment matches "Building here"; the PC port configures and builds; the ctest CPU suite adds no failure the baseline does not have |
| G1 Capacity | A, B | `capacity`, `toolkit`, `toolkit-limits` | every `Capacity*` gtest case passes; DC2-Toolkit's own gauntlet (fmt, clippy, cargo test per crate, tool tests, port tests, its disc stages) is green against its baseline; its `limits` tests pass |
| G2 Content registration | E | `content`, `lua-harness` | every `MoonMod*` gtest case passes (100%, MoonMod's rule); every Lua harness in `MoonMod/tools/tests` exits 0 |
| G3 Web | C | `web-node`, `web-browser` | the web build starts under Node (`--headless --offscreen --frames 30`) and in headless Chromium through Playwright with no abort |
| G4 Co-op | D | `coop` | the relay starts on a free port, a host/join handshake crosses it, and the engine's `MoonModNet*`/`MoonModCoop*` cases pass against it |
| G5 Retail data | all | `retail-title`, `retail-smokes`, `retail-determinism`, `retail-golden` | with a disc: title, town and dungeon reached headless with sane screenshots; two clean runs give identical stable markers and identical screenshots; the shots are within tolerance of the approved golden set |

Each stage ends in one of four states. **pass** and **fail** are the verdict of the check. **not_run** means
there was nothing to check yet (no web build, no relay, no disc, no `Capacity*` tests in the binary) and
says why; it never counts as a pass. **error** means the check itself could not run (no test binary, a
missing tool, a bug in the runner) and counts as red. Gate N+1 does not start while gate N has a fail or
error: its stages are recorded as not run with the reason. `--keep-going` overrides that for a survey.

### G0 Toolchain, build, static

- `env`: clang 20 with libc++ and lld, CMake >= 3.21, Ninja, Rust 1.99, SDL3 3.4.18 (pkg-config or
  `/usr/local/lib/cmake/SDL3`), Vulkan headers 1.4 at `CT_VULKAN_HEADERS`, the loader, `glslangValidator`,
  the libavformat major version and Kazarov's FFmpeg tree, the pinned FetchContent sources, `CXXFLAGS`,
  Node >= 20, Python >= 3.10. Optional, reported for the later gates: lavapipe (`vulkaninfo` finds llvmpipe),
  emcc 6.0.13 with `EM_CONFIG`, Playwright, `xvfb-run`, PIL, a `lua`. Evidence: `env/env.txt` and `env.json`.
- `build`: `ct_configure` when there is no `CMakeCache.txt` (or `--clean`), then `ct_build`, incremental.
  `--target T` builds one target. Fails on a non-zero exit or when `darkchronicle` or `darkchronicle_tests`
  is missing afterwards. Evidence: `configure.log`, `build.log`, `errors.txt` (every `error:` and `FAILED:`
  line). `--no-build` records the stage as not run and tests the binaries as they are, saying when
  `darkchronicle_tests` was linked and how many source files under `port/` are newer (a stale build); use it
  while another agent's edits are in flight, never to claim a pass for a tree that was not built.
- `ctest`: the whole suite less the `gpu` label (`ctest -LE gpu`, each case its own process, as the tree's
  CMake already arranges), with `DC2_DATA` and `DC2_SAVE` pointed at empty fixture directories so no test
  finds a player's data, `SDL_VIDEO_DRIVER=offscreen` and `DC2_AUDIO=off`. `--gpu` keeps the gpu label,
  sets `VK_ICD_FILENAMES` to lavapipe and runs under `xvfb-run -a` when present; its baseline is a
  separate key (`ctest-gpu`). The verdict is **no new failures** against
  `tools/gauntlet/baselines/linux-x86_64.json`: the report lists the new, known and fixed cases. The first
  run on a machine writes the baseline with `--update-baseline`; without a baseline the stage is an error.
  Every ctest stage's summary ends with the link time of `darkchronicle_tests` and, when sources are newer,
  "stale build"; `details.tests_binary` names the first twenty newer files. When the binary is relinked
  while ctest runs (another agent building: ctest says "Could not find executable" for the cases that
  started during the link, and the rest ran against two different binaries) the stage is an **error**, not a
  verdict against the baseline; rerun it when the build is quiet. Likewise when ctest's output has "No space
  left on device": a run on a full disk once failed 189 cases that write files and wrote no JUnit at all.

### G1 Capacity

- `capacity`: `ctest -R '^Capacity'`, all must pass. Not run while the build has no such case (work
  package A adds one per raised limit, filling the structure to the new limit and reading it back).
- `toolkit`: DC2-Toolkit's own runner (`DC2-Toolkit/tools/gauntlet/run.py --gate G0 --gate G5 --keep-going`,
  with `CHRONICLE_TWO` pointing here so it imports this report and baseline code), folded into one stage:
  fail if any of its stages failed, else error if any errored, else pass if any ran, else not run. Its
  stages are `env`, `fmt`, `clippy` (per crate, `-D warnings`, against the toolkit's baseline where
  `single_element_loop` in dc2tk-assets is known), `cargo-test` (per crate, release, against the baseline),
  `tool-tests` (`tools/test_engine_mods.py`, `tools/test_native_prepare.py`), `port-tests`
  (`port/tests/run.py` with clang 20 first on PATH and the SDK at `build/port/sdk`; `--iso` when a disc is
  named; not run without the SDK) and, with a disc, `port-golden` and `event-sweep`. The toolkit's report sits at
  `toolkit/toolkit/report.json` under this run's report, its baseline stays its own
  (`DC2-Toolkit/tools/gauntlet/baselines/`, never rewritten from here), and `DC2-Toolkit/GAUNTLET.md` says
  the rest. Not run when the toolkit is not beside this tree. A toolkit crate that does not compile (another
  agent's edits in flight) is a failed stage, which is the right answer.
- `toolkit-limits`: the toolkit runner's `limits` stage (`cargo test --release -p dc2tk-assets limits`). Not
  run when the toolkit is not beside this tree or no test matches yet.

### G2 Content registration

- `content`: `ctest -R '^MoonMod'` (less `MoonMod.HeadlessProbe`, which needs Vulkan, unless `--gpu`),
  with the fixture environment above. 100% green, as MoonMod's gate loop demands.
- `lua-harness`: every `MoonMod/tools/tests/*/test_*.lua` with the system `lua` or, when there is none,
  MoonMod's vendored 5.5.1 built once with clang into `tools/gauntlet/.cache/lua/` (a few seconds).

### G3 Web

Runs when `port/build/web` (or `--web-build DIR`) holds `darkchronicle.js` (`darkcloud.js` is accepted too).
Until package C's branch `wip/web` is merged the build lives in its worktree:
`--web-build /home/user/ct-web/port/build/web` (`tools/web/build.sh` there makes it; `port/web/PROGRESS.md`
says how). Both stages pass against that build (WebGPU and WebGL2 in Chromium, Node), each game exiting 3
for the missing data. The browser stage writes its stand-in `smoke-data.zip` into the build directory, as
package C's `tools/web/smoke.sh` does.

- `web-node`: `node darkchronicle.js --headless --offscreen --frames 30 --save <fresh dir>`, as DC_Web's
  `web.yml` smoke. Without game data startup stops at the data check, so exit 0 and 3 both pass; `abort:` or
  `RuntimeError` in the log fails.
- `web-browser`: a plain static server on a free port, then `tools/gauntlet/web/browser_smoke.cjs`
  (`port/web/test/browser_smoke.mjs` with `--no-sandbox` and the global Playwright) in headless Chromium on
  SwiftShader (`--enable-unsafe-webgpu --enable-features=Vulkan --use-vulkan=swiftshader
  --use-webgpu-adapter=swiftshader --disable-vulkan-surface --enable-unsafe-swiftshader`): `sw.js` must
  isolate the page, a small `smoke-data.zip` is stored in the origin private file system under
  `chronicletwo/` (the page's `HOME`; the game runs `--run-dir /opfs/chronicletwo`), the game starts and the
  page log must end in `exit=` with 0 or 3 and no `abort:`, `RuntimeError` or `pageerror`. One run per
  renderer the build has (`stages.json` `web.renderers`): WebGPU through `darkchronicle.js`, and WebGL2
  through `darkchronicle_gl.js` with `?renderer=gl` when that was built; both must pass.

### G4 Co-op

- `coop`: the relay (`coop/relay/server.mjs` in this tree, else `../DC_Web/coop/relay`) is copied to
  `tools/gauntlet/.cache/relay/`, `ws` installed there once, and started on a free port. `coop/handshake.mjs`
  hosts a room, joins it by code, checks both `welcome` events and a binary message each way (a failure here
  fails the stage). Then, against the build `--coop-build DIR` names (package D's worktree,
  `/home/user/ct-coop/port/build/pc`, until `wip/coop` is merged; this tree's build without the option), two
  engine parts: `ctest -R 'MoonModNet|MoonModCoop'` with `DC2_COOP_RELAY`, `RELAY_URL` and `MOONMOD_COOP_RELAY`
  set to the relay's URL (same lock and relink check as every ctest stage), and `tools/coop/two_games.py
  --build DIR --relay-dir <the relay copy>` from that build's source tree (`CMAKE_HOME_DIRECTORY` in its
  `CMakeCache.txt`): two headless games host and join a room through the relay and exchange a message (exit
  0 pass, 1 fail, 2 not run). Each part is not run with its reason while its binary or script is missing;
  the stage folds them as the toolkit stage does (fail, else error, else pass if a part passed, else not
  run). A `--coop-build` directory that does not exist is not run, saying so. Evidence: `handshake.log`,
  `relay.log`, `ctest.log`/`ctest.xml`, `two-games.log` and the two games' logs under `two-games/`.

### G5 Retail data

The data root is `--data`, `DC2_DATA`, `port/run/data` or a previous extraction; when only a disc is present
(`--iso`, `DC2_ISO`, `rom/pal/*.iso`) `dc2data extract` makes one under `tools/gauntlet/.cache/retail/`.
Every run uses a fresh save root under the report and `--headless --offscreen --window 960x720`, with
`DC2_AUDIO=off` and lavapipe.

- `retail-title`: `--jump title --fast-load --frames 120 --screenshot`. Exit 0 and a screenshot that is not
  empty and not one colour.
- `retail-smokes`: the smokes in `stages.json`: `title`, `town` (`--jump town:10`, 600 frames) and `dungeon`
  (`--jump dungeon:0:1` with `tools/input/jump-dungeon-floor1.input`, 2300 frames), each with `--seed`.
  Same conditions per smoke.
- `retail-determinism`: the title smoke twice from clean save roots. The lines carrying a stable marker
  prefix (`MOONMOD_API_STATUS|`, `MOONMOD_HEADLESS_`, `MOONFACTORY_`, `dataread: indexed`, `mglib: frame`)
  are extracted from each log and must be identical, at least `min_markers` of them, and the two screenshots
  must be pixel-identical.
- `retail-golden`: each smoke's screenshot against `tools/gauntlet/golden/<name>.png`, mean absolute
  difference <= 2.0 and 99.5th percentile of the per-pixel difference <= 32 (UnrealDC's `golden.py` limits).
  `--approve-golden` makes this run's shots the set, after a person has looked at them. Not run without an
  approved set. The compare uses PIL when installed and a pure-Python PNG reader when not.

## The report

Every run writes `tools/gauntlet/report/<timestamp>/report.json` and `report.md`, and points
`tools/gauntlet/report/latest` at it. Report directories are evidence and stay. The decision on what git
keeps (`tools/gauntlet/.gitignore`, and the same rules in DC2-Toolkit's): `report.json`, `report.md` and
`env/` of every top-level run directory are tracked (a few kB each, enough to read a verdict and its
environment months later); the logs, JUnit files, screenshots, save roots, the nested toolkit report under
`toolkit/` and the `latest` symlink are not (megabytes per run, and machine-local paths). A run worth
keeping gets a name; `--report-dir tools/gauntlet/report/<name>` gives it one (`baseline-ctest-cpu`, `survey-g1-g5`, `full-*`). The JSON holds: `gauntlet_version`; `created`; `host` (name, OS,
CPUs, memory, Python); `repos` (head, branch and dirty flag of ChronicleTwo, DC2-Toolkit, MoonMod and
DC_Web beside it); the arguments and options; per stage its `name`, `gate`, `status`, `duration_s`,
`summary`, the exact `command` to reproduce it, its `log`, its `evidence` paths, the `baseline` diff
(`new`, `known`, `fixed`) where one applies, and `details`; and the `verdict` and `exit_code`. The exit
code is 0 only when every stage that ran passed and no stage errored. Each stage's logs, JUnit files,
screenshots and marker lists sit under `report/<timestamp>/<stage>/`.

## Baselines

`tools/gauntlet/baselines/linux-x86_64.json` holds, per key (`ctest-cpu`, `ctest-gpu`), the cases that
failed when it was written, with the totals, the git head and the date. Which cases fail at baseline
depends on the machine (no GPU, no disc, no fonts), so the file is per platform and the verdict is "no new
failures", as MoonMod's gate loop compares a patched and an unpatched build on the same machine. A fixed
case shows up in the report as fixed; rewrite the baseline (`--update-baseline`) only to record fixes or a
deliberate, explained change, never to make a new failure known.

What `ctest-cpu` knows on this machine (2420 cases: 2378 passed, 21 failed, 21 not run, at `cb6ae0f3`), by
cause, so the next agent does not rediscover it.

**Provenance, read before trusting it.** The baseline was written by one run
(`report/baseline-ctest-cpu/`, 03:07-03:11 UTC on 2026-11-08) that is internally consistent: no "Could not
find executable", no "No space left on device", and its 21 not-run cases are 20 that skip themselves
(retail data, fonts or tool inputs absent: `MoviePlayer`, `TtfFont`, `Luau.Survey`, `Records.Check` and the
like) and one disabled benchmark. But the runner did not yet record the binary's link time, and the report marks the tree dirty at
`cb6ae0f3`: work package A's edits had begun, so it is not provably the unmodified tree. Every one of the 21
has a cause below that does not involve the capacity layer, except possibly the `MoonModManagerUi` case.
When package A's work is committed, rewrite the baseline from a clean checkout of the commit before it (a
worktree with its own build), or from a run of the final tree whose new failures were each explained; do not
rewrite it from a tree with edits in flight.

- **One engine bug, ten cases** — `Crash.abort`, `Crash.assert`, `Crash.audio-thread`, `Crash.segv`,
  `Crash.stackoverflow`, `Crash.terminate`, `Crash.unimplemented`, `Log.SessionIdIsDateTimeAndFourHexDigits`,
  `LogFiles.PruningKeepsTheNewestSessions`, `LogFiles.SessionFileHasTimeLevelThreadAndCategory`.
  `LogSessionId()` in `port/src/log/log.cpp` formats a `char text[32]` with `std::format("{}-{:04x}", text,
  ...)`; libc++ formats a `char[N]` as all N-1 characters, NULs included, so the id is
  `20261108-030731\0\0...-8ba5`, the log and crash-report directories cannot be opened and the crash tests
  find no `report.txt`. `std::string_view(text)` or `text` as `const char *` fixes it; the fix belongs to the
  port, not to this package, and when it lands these ten show as fixed and the baseline is rewritten.
- **Retail data, nine cases** — `DataRead.ExtractedItemConfigLoadsWhenPresent`,
  `DataRead.RealDataIndexesTheDiscFiles`, `DataRead.RetailAttachmentJsonPreservesEveryRecordValue`,
  `SaveSlotTest.LocalizedSavePromptsContainRetailText`,
  `SaveSlotTest.NewSaveAndFinnyPromptsUseRetailMessagesInEveryPalLanguage`,
  `SaveSlotTest.NoFishAndOperationFailuresUseRetailNotices`,
  `NativeCharacterRetail.LoadsConvertedC01ProfileAndEvaluatesItsNativeMotion`,
  `SoundRealData.PublishedBanksAndSequencesLoad`,
  `TextureBundleRetail.EveryDungeonLanguageContainsTheExpectedSharedAtlases`. They read the extracted disc
  and fail against the empty fixture root (the stage points `DC2_DATA` at it on purpose, so no test finds a
  player's data). They would pass with a disc; they should skip without one, which is a test change for
  whoever owns them.
- **Environment, one case** — `GfxValidation.LayerIsInstalled` wants `VK_LAYER_KHRONOS_validation`.
  `vulkan-validationlayers` 1.3.275 is installed now, so the next run reports it fixed.
- **A real failure, one case** — `MoonModManagerUi.ChangesTypedSettingsNotifiesModsAndSurvivesReload`
  (`moonmod_manager_ui_test.cpp:213`, `selected_skill` is 0 where 1 is expected). It is also why G2 `content`
  (100% of `MoonMod*`) is red; package E's area.

Latest full run (`report/full-20261108-final/`, binary linked 15:19:51 from the tree with package A's
capacity edits, not stale): 2431 cases, 2390 passed, 11 failed, 30 not run; **0 new**, 11 known (the ten
`LogSessionId` cases and `MoonModManagerUi`), 1 fixed (`GfxValidation.LayerIsInstalled`, the layer is
installed), and the nine retail-data cases were skipped (they skip themselves without data now, as suggested above). The
baseline is not rewritten for that: per the provenance note, it waits for a clean tree.

## Agent protocol

How an agent runs the gauntlet unattended and what it does with the result.

1. **Order.** Run `python3 -I tools/gauntlet/run.py` from the repository root. It runs G0 to G5 in order
   and stops at the first red gate. Work on one package runs its own gate after G0: `--gate G0 --gate G1`
   for package A. After an upstream merge (package G) the whole run is repeated and MoonMod's patch is
   regenerated (below).
2. **Read the report**, not the console: `report/latest/report.md`. A stage is one of pass, fail, not_run,
   error. Not run is not a pass: say so when summarising, and say why it did not run.
3. **Triage a failure.** Reproduce it with the exact command the report records for that stage (the
   `command` field, and for a single case `ctest --test-dir port/build/pc -R '^Suite\.Case$'
   --output-on-failure`). Read the stage's log and evidence. Fix the smallest cause in the code under test,
   then rerun the gate (`--gate Gn`), not just the stage: a fix can move a failure.
4. **Never** skip, disable, filter out or mark as expected a failing test to get a gate green. Never bump
   the baseline to hide a regression: `--update-baseline` is for the first run on a machine and for recording
   fixes. Never raise a timeout, a tolerance or `min_markers` to make a check pass without writing down why
   in the commit. A test that is wrong is fixed as a test, in its own change, with the reason.
5. **Hands.** Some lines only a person can tick: how a screenshot looks beyond "not one colour", input
   feel, a device the machine lacks (a real GPU, a browser on a phone), anything the gate's notes mark
   *(hands)*. Record the headless part as done and leave the line marked *(hands)*, as MoonMod's gate log
   does; do not tick it. Approving a golden set (`--approve-golden`) is a hands step: an agent runs the
   smokes and asks; a person looks at the shots.
6. **Gate N+1 does not start until gate N is green.** Content for a later package may be prototyped on a
   branch, but it is not reported as verified, and the report will show its gate as not run while an earlier
   gate is red.
7. **Evidence a pass requires.** The report path; for each stage the command, its log and the evidence files
   (JUnit for ctest, the baseline diff with zero new failures, screenshots for the smokes, the two marker
   lists for determinism, `check.json` for the golden compare); the git heads the report records; and the
   environment stage's `env.txt`. A pass claimed without a report is provisional, as MoonMod's rule for a
   Lua-only run.
8. **When the environment is the problem** (a missing tool, a wrong version), fix `tools/gauntlet/env.sh` or
   the machine, and add the fact to "Environment notes" below so the next agent does not rediscover it.
9. **When the tree is in flux** (other agents editing concurrently): do not rebuild over their half-done
   edits to get a verdict; run with `--no-build`, read the "binary linked ... stale build" note in the
   report, and say in the summary that the result is for the binary as built, not the tree. A toolkit
   crate or `cargo fmt --check` broken by edits in flight is reported as that stage's failure; it is their
   fix, not a reason to touch the runner. Agents sharing `port/build/pc` take turns at ctest through marker
   files: `<CT_LOCK_DIR>/ctest-lock.<tag>` (`env.sh`; the tag is the work package letter, `CT_LOCK_TAG`).
   Every ctest stage waits, polling every 30 s up to `--timeout`, while another tag's file exists, holds
   its own while it runs and removes it after; the report's `details.lock` says how long it waited and for
   whom. The link time of `darkchronicle_tests` is recorded before and after each ctest stage
   (`details.tests_binary`, `tests_binary_after`); when it changed, or ctest reported "Could not find
   executable", the stage is an error, not a fail: the run tested two binaries and proves nothing. An
   agent that relinks the binary should hold its own lock file while it links and tests.
10. **Disk.** The machine's disk allowance is about 40 GB shared by every agent (a ChronicleTwo build is about
    7 GB, the toolkit's release target was 8 GB with debug info). Check `df -h /` before a build, a full
    ctest or the toolkit stage and stop with a note when under 4 GB is free; the runner's `--min-free-gb`
    guard does the same per stage (4 GB for the build and the toolkit, 1 GB for a ctest run of binaries
    already built). Run the toolkit stages last (`--last toolkit-limits --last toolkit`) and
    remove `DC2-Toolkit/build/release` afterwards unless a later stage needs it (the port SDK in
    `build/port/sdk` stays). When the disk is full, `/tmp/claude-0` is full too: a command's output is lost
    and a failed command may print nothing, so write output under `/dev/shm` and read it from there.
11. **Rate-limit-safe progress notes.** An agent can be cut off mid-task (a usage limit, a lost container)
    and its successor starts from what is on disk. After each finished step, append one line to a progress
    file in the scratchpad or the report directory (what ran, the report path, the verdict, what is next),
    and keep long runs in the background with their output in a file, not only in the conversation. Name
    report directories (`--report-dir tools/gauntlet/report/full-<date>`), so a successor finds them without
    the transcript. Never leave a `ctest-lock.<tag>` file behind: the runner removes its own on exit, but a
    killed run does not, so a successor removes a stale one of its own tag (check the PID inside first).
12. **Unattended order for a full pass**: `python3 -I tools/gauntlet/test_gauntlet.py` (the runner itself),
    then `run.py --keep-going --last toolkit-limits --last toolkit --report-dir tools/gauntlet/report/full-<date>`
    (with `--web-build`/`--coop-build` while C and D are unmerged) for the survey, then the gate
    under work until green, then `export_moonmod_patch.py --check` and `--kazarov --check` after a merge.
    Summarise from the report: pass / fail / not_run per stage with the reason for every not_run, the
    baseline counts (total, passed, failed, new, known, fixed), the binary link time when `--no-build` was
    used, and the report path.

Lines only a person can tick, by gate *(hands)*:

- G0: none; the build and the CPU suite are headless.
- G1: that a raised limit is the right number for the content it is meant to hold.
- G2: how MoonMod's registered content looks and plays in the manager UI.
- G3: the page in a real browser (sound, input, a phone), beyond the headless Chromium smoke.
- G4: two people in one room over a real network, beyond the loopback relay.
- G5: whether the title, town and dungeon screenshots look like the game (`--approve-golden` after looking),
  input feel, frame pacing on a real GPU.

## Environment notes

What this machine (Ubuntu 24.04, x86-64, 4 CPUs, no GPU) needed beyond DC2_PLAN.md "Building here":

- **Vulkan headers 1.4.363** are a Khronos `Vulkan-Headers` checkout, not Ubuntu's 1.3.275 package;
  `CT_VULKAN_HEADERS` in `env.sh` points at the checkout's `include`. Move the checkout somewhere stable and
  set the variable when the default path is gone.
- **Ubuntu's FFmpeg 6.1** (libavformat major 60): Kazarov's `tools/kazarov/src/c/movie_mux.c` passes a
  `const uint8_t *` write callback to `avio_alloc_context`, which FFmpeg 7 declares that way and 6.1 does not;
  clang 20 makes the mismatch an error, so the file carries
  `typedef const uint8_t *write_packet_buffer;` under `#if LIBAVFORMAT_VERSION_MAJOR >= 61` and
  `typedef uint8_t *write_packet_buffer;` otherwise, used by `memory_write()`. The `env` stage reports the
  major version so a different FFmpeg is noticed before the build. This is one of the four files MoonMod's
  Kazarov patch now carries.
- **Kazarov wants static FFmpeg archives** (`lib{avutil,swresample,swscale,avcodec,avformat}.a` under
  `KZ_FFMPEG_DIR/lib`, headers under `include`). Ubuntu ships shared libraries only, so `/opt/ffmpeg-sys` is
  a tree of one-line linker scripts (`INPUT(/usr/lib/x86_64-linux-gnu/libavformat.so.60)`) and an `include`
  symlink to `/usr/include/x86_64-linux-gnu`. `ct_ffmpeg_tree` in `env.sh` creates it when missing;
  `ct_configure` calls it.
- **`CXXFLAGS=-stdlib=libc++ CXXSTDLIB=c++`** must be in the environment: kazarov_core compiles its
  `cxx.cc` through cc-rs outside CMake's flags, and without them `dc2data` links a libstdc++ `std::string`
  (`std::__cxx11`) against the game's libc++ one (`std::__1`). `env.sh` exports both.
- **Blocked archive URLs.** The proxy answers 403 to every GitHub archive download, so the tree's
  FetchContent URLs cannot be fetched: `github.com/jeremy-rifkin/libdwarf-lite/archive/...tar.gz` and
  `github.com/jeremy-rifkin/cpptrace/archive/refs/tags/v1.0.4.tar.gz` (`port/cmake/crash.cmake`),
  `codeload.github.com/odygrd/quill/tar.gz/refs/tags/v13.1.0` (`logging.cmake`),
  `codeload.github.com/luau-lang/luau/tar.gz/refs/tags/0.740` (`luau.cmake`),
  `codeload.github.com/wolfpld/tracy/tar.gz/...` (`port/CMakeLists.txt`), and likewise
  `github.com/TheMoonPeople/Kazarov/archive/...zip`, `github.com/KhronosGroup/Vulkan-Headers/archive/...`
  and `github.com/simdjson/simdjson/archive/...`. `git clone` of the same repositories works. So the
  pinned FetchContent sources are git clones under `/opt/deps` passed as `FETCHCONTENT_SOURCE_DIR_<NAME>`
  (corrosion, simdjson, fastgltf, luau, quill, cpptrace, libdwarf, tracy, googletest), Vulkan-Headers is a
  clone, and `export_moonmod_patch.py` clones Kazarov and, when no `../themoonpeople/chronicletwo` is around,
  upstream ChronicleTwo into `tools/gauntlet/.cache` (`--no-checkout`, `GIT_LFS_SKIP_SMUDGE=1`).
- **The Vulkan validation layer** (`vulkan-validationlayers` 1.3.275, `VK_LAYER_KHRONOS_validation`) was
  missing and is installed now; `GfxValidation.LayerIsInstalled` wanted it.
- **DC2-Toolkit wants clang 20 on PATH** for `port/tests/run.py` and the port build; the system `clang` is
  18. It is at `/opt/clang20/bin`, which the toolkit runner puts first (`DC2TK_CLANG_BIN` overrides).
- **DC2-Toolkit's port SDK** (`build/port/sdk`: shared SDL3 3.4.18 and an LGPL FFmpeg 8.0.1 MPEG-2 decoder)
  comes from `tools/port_sdk.py`. SDL's release tarball downloads; `ffmpeg.org` is unreachable through the
  proxy (connection reset), so FFmpeg was built from `git clone --depth 1 --branch n8.0.1
  https://github.com/FFmpeg/FFmpeg build/port/sdk-work/ffmpeg-8.0.1` with `port_sdk.build_ffmpeg()` and its
  `fetch` replaced by that folder (same configure flags). Deleting `DC2-Toolkit/build` deletes the SDK too;
  delete `build/release` only.
- **The toolkit's cargo builds** run with `CARGO_INCREMENTAL=0 CARGO_PROFILE_RELEASE_DEBUG=0` (the toolkit
  runner sets both and its recorded commands carry them): the workspace's release profile has `debug = 1`,
  which made `build/release` 8 GB.
- **lavapipe** (`/usr/share/vulkan/icd.d/lvp_icd.json`, llvmpipe LLVM 20.1.2) is the only Vulkan device.
  The CPU ctest suite needs none; `--gpu` cases run on it under `xvfb-run -a`. The game's full pipeline set
  does not fit under software Vulkan for the ASan preset (docs/PC.md), so keep `--gpu` to the Debug preset.
- **Headless WebGPU in Chromium** works on SwiftShader with `--enable-unsafe-webgpu --enable-features=Vulkan
  --use-vulkan=swiftshader --use-webgpu-adapter=swiftshader --disable-vulkan-surface
  --enable-unsafe-swiftshader` (`web/browser_smoke.cjs`, as package C's `tools/web/smoke.sh`); a presented
  canvas loses the device there, so the game runs `--offscreen`. Without data the game exits 3
  (`kExitNoData`) under Node and on the page, which G3 accepts; the page keeps its files in the OPFS
  directory `chronicletwo` (`--run-dir /opfs/chronicletwo`).
- **Emscripten 6.0.13** is at `/opt/emsdk-dl/install/emscripten` with `EM_CONFIG=/root/.emscripten`
  pointing at the system Node 22 (`/opt/node22/bin/node`).
- **Node packages**: Playwright and Chromium are installed globally (`require('playwright')` works,
  `chromium.launch()` needs `--no-sandbox` as root); the relay's `ws` is not, so the `coop` stage installs
  it into its `.cache/relay` copy once.
- **No `lua`** on PATH: the `lua-harness` stage builds MoonMod's vendored 5.5.1 with clang into
  `tools/gauntlet/.cache/lua/lua`.
- **PIL 12.3** is installed; the image compare falls back to a pure-Python PNG reader without it.

## Regenerating MoonMod's patch

MoonMod's `ports/chronicle-two/apply-moonfactory.ps1` applies two patches to a clean upstream checkout:
`moonfactory-chronicle-two.patch` to ChronicleTwo `7a7d7f29` (with Kazarov as the submodule that commit
pins) and `moonfactory-kazarov.patch` inside `tools/kazarov` at `fd72338a`. Both come from this tree:

```
python3 -I tools/gauntlet/export_moonmod_patch.py --committed --check ../MoonMod/ports/chronicle-two/moonfactory-chronicle-two.patch
python3 -I tools/gauntlet/export_moonmod_patch.py --kazarov --check ../MoonMod/ports/chronicle-two/moonfactory-kazarov.patch
```

The first is `git diff 7a7d7f29 HEAD -- port/ scripts/ CMakeLists.txt CMakePresets.json docs/` without
`tools/kazarov` (vendored here, a submodule there) and `port/web`; `.gitmodules` and `DC2_PLAN.md` differ
from the pin too and are left out, since MoonMod's checkout keeps the submodule. `--committed` diffs HEAD,
so edits in flight never reach MoonMod; without it the working tree is diffed, and the script warns about
uncommitted edits (the patch carries them) and untracked files under those paths (the patch lacks them, so
a patch with half of a package's work in it would not build). `--check` runs `git apply --check` in a
throwaway worktree at the pin, made from an upstream clone (`--upstream DIR`, `$CHRONICLETWO_UPSTREAM`,
`../themoonpeople/chronicletwo`, else a clone into `tools/gauntlet/.cache`, else this repository's own
history, which holds the same commit). The second diffs the vendored Kazarov (`tools/kazarov`, working
tree) against a git clone of upstream at its pin (`tools/gauntlet/.cache/kazarov-upstream`) and, with
`--check`, applies it in a worktree of that clone.

Regenerating MoonMod's `ports/chronicle-two` after a merge (package G) or when a package lands:

1. Commit the work in ChronicleTwo first (or accept the warnings knowingly for a preview).
2. Run both commands above into a scratch directory with `--check`; both must say "applies cleanly".
3. If package G moved the upstream pin, pass `--pin` / `--kazarov-pin` and change `$base` / `$kazarovBase`
   in `apply-moonfactory.ps1` and `UPSTREAM_PIN` / `KAZAROV_PIN` in the script, together.
4. Copy the two files into `MoonMod/ports/chronicle-two/`, read `git diff --stat` there (the file count
   should change only by what the packages added), update its `README.md` if the patched file list it
   describes changed, and commit in MoonMod. Then MoonMod's own `verify-headless.ps1` gate applies.

Checked on 2026-11-08 at ChronicleTwo `58dd0868`: `--committed` gives 191 files and is byte-identical to
MoonMod's shipped `moonfactory-chronicle-two.patch` apart from `index` lines; the working tree gives 221
files (the 30 more are work package A's capacity edits in flight, and its 4 untracked files are missing, so
that patch is a preview only); both apply cleanly to upstream `7a7d7f29` (`../themoonpeople/chronicletwo`).
The Kazarov patch has 4 files (`include/platform/input.hpp`, `src/c/movie_mux.c`,
`src/cpp/gfx/displaylist.cpp`, `src/cpp/platform/input.cpp`; MoonMod's shipped one has 3 and lacks
`movie_mux.c`, the FFmpeg 6 fix, which should be shipped) and applies cleanly at `fd72338a`. Untracked work
(`GAUNTLET.md`, `tools/gauntlet/`) never enters the patch, and nothing under `tools/` is in scope.

## Testing the runner

`python3 -I tools/gauntlet/test_gauntlet.py` covers the report schema and exit code, the baseline diff,
stage selection, `--last` and the gate-order rule, the JUnit reader, `env.sh` parsing, the folding of the
toolkit's report into one stage, `--no-build` and the stale-binary note, the ctest lock, the relink and
full-disk contamination checks, the disk guard, `--coop-build` (absent directory, source tree from
`CMakeCache.txt`), every stage run against an empty tree (each returns not run or error with a reason and
none raises), and the image sanity and tolerance compare (pure reader and PIL). `DC2-Toolkit/tools/gauntlet/test_gauntlet.py` covers that runner's parsers (clippy
lints against compile errors, cargo test lines, the port tests' lines), its cargo settings and disk guard. Run both after changing anything
under either `tools/gauntlet/`. Every stage degrades to not run with a reason when there is nothing to
check (no web build, no relay, no disc, no toolkit, no `Capacity*` case) and to error when the check itself
cannot run; a stage that raises is caught by the runner and recorded as an error with its traceback, so a
runner bug never ends a run without a report.
