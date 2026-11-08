# Gauntlet report

Created 2026-11-08T17:19:47+00:00 on vm (Linux-6.18.44-fc-v310-x86_64-with-glibc2.39, 4 CPUs, 15.7 GB).
Verdict: **PASS** (exit 0).

Arguments: `--keep-going --web-build /home/user/ct-web/port/build/web --coop-build /home/user/ChronicleTwo/port/build/pc --last toolkit-limits --last toolkit --report-dir tools/gauntlet/report/merged-20261108`

## Repositories

| Repository | Head | Branch | Dirty |
|---|---|---|---|
| ChronicleTwo | `5a2286e049e6` | claude/dark-cloud-2-engine-yr3rlk | yes |
| DC2-Toolkit | `19314329c436` | claude/dark-cloud-2-engine-yr3rlk | no |
| MoonMod | `32e0ffe87eec` | claude/dark-cloud-2-engine-yr3rlk | no |
| DC_Web | `49d71f750e25` | claude/dark-cloud-2-engine-yr3rlk | no |

## Stages

| Gate | Stage | Status | Time | Summary |
|---|---|---|---|---|
| G0 | env | pass | 1.1s | 23 checks; optional absent: lua |
| G0 | build | pass | 0.8s | built /home/user/ChronicleTwo/port/build/pc (incremental) |
| G0 | ctest | pass | 229.6s | 2465 cases: 2424 passed, 10 failed, 31 not run; 0 new failure(s), 10 known, 2 fixed against linux-x86_64.json (9 known did not run: skipped or absent now, neither fixed nor failing); binary linked 2026-11-08T17:19:35 |
| G1 | capacity | pass | 1.2s | 11 cases: 11 passed, 0 failed, 0 not run; binary linked 2026-11-08T17:19:35 |
| G2 | content | pass | 10.2s | 114 cases: 113 passed, 0 failed, 1 not run; binary linked 2026-11-08T17:19:35 |
| G2 | lua-harness | pass | 3.5s | 10 harness(es) with /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua, 0 failed |
| G3 | web-node | pass | 0.5s | node darkchronicle.js exited 3 |
| G3 | web-browser | pass | 3.1s | Chromium smoke: webgpu driver exit 0, game exit 3; gl driver exit 0, game exit 3 |
| G4 | coop | pass | 11.2s | relay host/join handshake OK on ws://127.0.0.1:41309; build /home/user/ChronicleTwo/port/build/pc; ctest pass: 27 engine case(s): 27 passed, 0 failed, 0 skipped; two-games pass: two_games.py exit 0: PASS two games met through the relay |
| G5 | retail-title | not_run | 0.0s | no retail data: set DC2_DATA to an extracted root, or DC2_ISO / rom/pal/*.iso to a disc image |
| G5 | retail-smokes | not_run | 0.0s | no retail data: set DC2_DATA to an extracted root, or DC2_ISO / rom/pal/*.iso to a disc image |
| G5 | retail-determinism | not_run | 0.0s | no retail data: set DC2_DATA to an extracted root, or DC2_ISO / rom/pal/*.iso to a disc image |
| G5 | retail-golden | not_run | 0.0s | no retail data: set DC2_DATA to an extracted root, or DC2_ISO / rom/pal/*.iso to a disc image |
| G1 | toolkit-limits | pass | 0.1s | toolkit: limits pass |
| G1 | toolkit | pass | 57.6s | toolkit: env pass, fmt pass, clippy pass (0 new, 1 known, 0 fixed), cargo-test pass, tool-tests pass, port-tests pass, port-golden not_run, event-sweep not_run |

### G0 env: pass

23 checks; optional absent: lua

Reproduce:

```
python3 -I tools/gauntlet/run.py --stage env
```

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/env/env.json`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/env/env.txt`

### G0 build: pass

built /home/user/ChronicleTwo/port/build/pc (incremental)

Reproduce:

```
. tools/gauntlet/env.sh && ct_build
```

Log: `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/build/build.log`

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/build/build.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/build/errors.txt`

### G0 ctest: pass

2465 cases: 2424 passed, 10 failed, 31 not run; 0 new failure(s), 10 known, 2 fixed against linux-x86_64.json (9 known did not run: skipped or absent now, neither fixed nor failing); binary linked 2026-11-08T17:19:35

Reproduce:

```
ctest --test-dir /home/user/ChronicleTwo/port/build/pc --output-junit /home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/ctest/ctest.xml --output-on-failure --parallel 4 --no-tests=ignore --timeout 600 -LE gpu
```

Log: `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/ctest/ctest.log`

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/ctest/ctest.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/ctest/ctest.xml`

Against the baseline: 0 new failure(s), 2 fixed, 10 known.

Fixed since the baseline:

- `GfxValidation.LayerIsInstalled`
- `MoonModManagerUi.ChangesTypedSettingsNotifiesModsAndSurvivesReload`

### G1 capacity: pass

11 cases: 11 passed, 0 failed, 0 not run; binary linked 2026-11-08T17:19:35

Reproduce:

```
ctest --test-dir /home/user/ChronicleTwo/port/build/pc --output-junit /home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/capacity/ctest.xml --output-on-failure --parallel 4 --no-tests=ignore --timeout 600 -R ^Capacity
```

Log: `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/capacity/ctest.log`

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/capacity/ctest.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/capacity/ctest.xml`

### G2 content: pass

114 cases: 113 passed, 0 failed, 1 not run; binary linked 2026-11-08T17:19:35

Reproduce:

```
ctest --test-dir /home/user/ChronicleTwo/port/build/pc --output-junit /home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/content/ctest.xml --output-on-failure --parallel 4 --no-tests=ignore --timeout 600 -R ^MoonMod -E '^MoonMod\.HeadlessProbe$'
```

Log: `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/content/ctest.log`

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/content/ctest.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/content/ctest.xml`

### G2 lua-harness: pass

10 harness(es) with /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua, 0 failed

Reproduce:

```
(cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/challenges/test_challenges.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/coop/test_coop.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/living-towns/test_living_towns.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/modern-cloud/test_modern_cloud.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/randomizer/test_randomizer.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/roguelike/test_runs.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/roguelike/test_standalone.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/rpg/test_rpg.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/sirfancybacon/test_pack.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/vigil/test_vigil.lua)
```

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/lua-harness/tools_tests_challenges_test_challenges.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/lua-harness/tools_tests_coop_test_coop.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/lua-harness/tools_tests_living-towns_test_living_towns.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/lua-harness/tools_tests_modern-cloud_test_modern_cloud.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/lua-harness/tools_tests_randomizer_test_randomizer.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/lua-harness/tools_tests_roguelike_test_runs.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/lua-harness/tools_tests_roguelike_test_standalone.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/lua-harness/tools_tests_rpg_test_rpg.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/lua-harness/tools_tests_sirfancybacon_test_pack.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/lua-harness/tools_tests_vigil_test_vigil.lua.log`

### G3 web-node: pass

node darkchronicle.js exited 3

Reproduce:

```
/opt/node22/bin/node darkchronicle.js --headless --offscreen --frames 30 --save /home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/web-node/smoke-save
```

Log: `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/web-node/node.log`

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/web-node/node.log`

### G3 web-browser: pass

Chromium smoke: webgpu driver exit 0, game exit 3; gl driver exit 0, game exit 3

Reproduce:

```
/opt/node22/bin/node /home/user/ChronicleTwo/tools/gauntlet/web/browser_smoke.cjs http://127.0.0.1:48729/index.html /smoke-data.zip /home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/web-browser/browser-webgpu.log --headless --offscreen --frames 30 ; /opt/node22/bin/node /home/user/ChronicleTwo/tools/gauntlet/web/browser_smoke.cjs 'http://127.0.0.1:48729/index.html?renderer=gl' /smoke-data.zip /home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/web-browser/browser-gl.log --headless --offscreen --frames 30
```

Log: `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/web-browser/smoke-run-webgpu.log`

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/web-browser/smoke-run-webgpu.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/web-browser/browser-webgpu.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/web-browser/smoke-run-gl.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/web-browser/browser-gl.log`

### G4 coop: pass

relay host/join handshake OK on ws://127.0.0.1:41309; build /home/user/ChronicleTwo/port/build/pc; ctest pass: 27 engine case(s): 27 passed, 0 failed, 0 skipped; two-games pass: two_games.py exit 0: PASS two games met through the relay

Reproduce:

```
(cd /home/user/ChronicleTwo/tools/gauntlet/.cache/relay && node server.mjs --port 41309 --host 127.0.0.1 &) ; (cd /home/user/ChronicleTwo/tools/gauntlet/.cache/relay && node handshake.mjs ws://127.0.0.1:41309) ; ctest --test-dir /home/user/ChronicleTwo/port/build/pc --output-junit /home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/coop/ctest.xml --output-on-failure --parallel 1 --no-tests=ignore --timeout 600 -R 'MoonModNet|MoonModCoop' (with DC2_COOP_RELAY=ws://127.0.0.1:41309) ; /usr/local/bin/python3 -I /home/user/ChronicleTwo/tools/coop/two_games.py --build /home/user/ChronicleTwo/port/build/pc --relay-dir /home/user/ChronicleTwo/tools/gauntlet/.cache/relay --node /opt/node22/bin/node --work /home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/coop/two-games
```

Log: `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/coop/two-games.log`

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/coop/relay.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/coop/handshake.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/coop/ctest.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/coop/two-games.log`

### G1 toolkit-limits: pass

toolkit: limits pass

Reproduce:

```
(cd /home/user/DC2-Toolkit && CHRONICLE_TWO=/home/user/ChronicleTwo python3 -I tools/gauntlet/run.py --stage limits)
```

Log: `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/toolkit-limits/runner.log`

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/toolkit-limits/toolkit/report.json`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/toolkit-limits/toolkit/report.md`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/toolkit-limits/toolkit/limits/limits.log`

### G1 toolkit: pass

toolkit: env pass, fmt pass, clippy pass (0 new, 1 known, 0 fixed), cargo-test pass, tool-tests pass, port-tests pass, port-golden not_run, event-sweep not_run

Reproduce:

```
(cd /home/user/DC2-Toolkit && CHRONICLE_TWO=/home/user/ChronicleTwo python3 -I tools/gauntlet/run.py --gate G0 --gate G5)
```

Log: `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/toolkit/runner.log`

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/toolkit/toolkit/report.json`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/toolkit/toolkit/report.md`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/toolkit/toolkit/env/env.json`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/toolkit/toolkit/env/env.txt`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/toolkit/toolkit/fmt/fmt.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/toolkit/toolkit/clippy/dc2tk-disc.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/toolkit/toolkit/clippy/dc2tk-script.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/toolkit/toolkit/clippy/dc2tk-assets.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/toolkit/toolkit/clippy/dc2tk.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/toolkit/toolkit/cargo-test/dc2tk-disc.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/toolkit/toolkit/cargo-test/dc2tk-script.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/toolkit/toolkit/cargo-test/dc2tk-assets.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/toolkit/toolkit/cargo-test/dc2tk.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/toolkit/toolkit/tool-tests/test_engine_mods.py.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/toolkit/toolkit/tool-tests/test_native_prepare.py.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/merged-20261108/toolkit/toolkit/port-tests/port-tests.log`
