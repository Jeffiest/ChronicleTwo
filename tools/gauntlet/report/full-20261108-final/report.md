# Gauntlet report

Created 2026-11-08T15:38:37+00:00 on vm (Linux-6.18.44-fc-v310-x86_64-with-glibc2.39, 4 CPUs, 15.7 GB).
Verdict: **FAIL** (exit 1).

Arguments: `--no-build --keep-going --web-build /home/user/ct-web/port/build/web --coop-build /home/user/ct-coop/port/build/pc --last toolkit-limits --last toolkit --report-dir tools/gauntlet/report/full-20261108-final`

## Repositories

| Repository | Head | Branch | Dirty |
|---|---|---|---|
| ChronicleTwo | `58dd0868a899` | claude/dark-cloud-2-engine-yr3rlk | yes |
| DC2-Toolkit | `8e4137172aab` | claude/dark-cloud-2-engine-yr3rlk | yes |
| MoonMod | `875a2350f71a` | claude/dark-cloud-2-engine-yr3rlk | yes |
| DC_Web | `49d71f750e25` | claude/dark-cloud-2-engine-yr3rlk | no |

## Stages

| Gate | Stage | Status | Time | Summary |
|---|---|---|---|---|
| G0 | env | pass | 1.1s | 23 checks; optional absent: lua |
| G0 | build | not_run | 0.0s | skipped by --no-build; the stages test the existing binaries; binary linked 2026-11-08T15:19:51 |
| G0 | ctest | pass | 219.9s | 2431 cases: 2390 passed, 11 failed, 30 not run; 0 new failure(s), 11 known, 1 fixed against linux-x86_64.json; binary linked 2026-11-08T15:19:51 |
| G1 | capacity | pass | 1.2s | 11 cases: 11 passed, 0 failed, 0 not run; binary linked 2026-11-08T15:19:51 |
| G2 | content | fail | 6.7s | 80 cases: 79 passed, 1 failed, 0 not run; failing: MoonModManagerUi.ChangesTypedSettingsNotifiesModsAndSurvivesReload; binary linked 2026-11-08T15:19:51 |
| G2 | lua-harness | pass | 4.1s | 10 harness(es) with /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua, 0 failed |
| G3 | web-node | pass | 0.6s | node darkchronicle.js exited 3 |
| G3 | web-browser | pass | 3.8s | Chromium smoke: webgpu driver exit 0, game exit 3; gl driver exit 0, game exit 3 |
| G4 | coop | pass | 11.7s | relay host/join handshake OK on ws://127.0.0.1:40553; build /home/user/ct-coop/port/build/pc; ctest pass: 27 engine case(s): 27 passed, 0 failed, 0 skipped; two-games pass: two_games.py exit 0: PASS two games met through the relay |
| G5 | retail-title | not_run | 0.0s | no retail data: set DC2_DATA to an extracted root, or DC2_ISO / rom/pal/*.iso to a disc image |
| G5 | retail-smokes | not_run | 0.0s | no retail data: set DC2_DATA to an extracted root, or DC2_ISO / rom/pal/*.iso to a disc image |
| G5 | retail-determinism | not_run | 0.0s | no retail data: set DC2_DATA to an extracted root, or DC2_ISO / rom/pal/*.iso to a disc image |
| G5 | retail-golden | not_run | 0.0s | no retail data: set DC2_DATA to an extracted root, or DC2_ISO / rom/pal/*.iso to a disc image |
| G1 | toolkit-limits | error | 0.0s | only 2.8 GB free on the disk holding /home/user/DC2-Toolkit (the gauntlet wants 4.0 GB, --min-free-gb): free space first; the stage did not start |
| G1 | toolkit | error | 0.0s | only 2.8 GB free on the disk holding /home/user/DC2-Toolkit (the gauntlet wants 4.0 GB, --min-free-gb): free space first; the stage did not start |

### G0 env: pass

23 checks; optional absent: lua

Reproduce:

```
python3 -I tools/gauntlet/run.py --stage env
```

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/env/env.json`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/env/env.txt`

### G0 build: not_run

skipped by --no-build; the stages test the existing binaries; binary linked 2026-11-08T15:19:51

Reproduce:

```
. tools/gauntlet/env.sh && ct_build
```

### G0 ctest: pass

2431 cases: 2390 passed, 11 failed, 30 not run; 0 new failure(s), 11 known, 1 fixed against linux-x86_64.json; binary linked 2026-11-08T15:19:51

Reproduce:

```
ctest --test-dir /home/user/ChronicleTwo/port/build/pc --output-junit /home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/ctest/ctest.xml --output-on-failure --parallel 4 --no-tests=ignore --timeout 600 -LE gpu
```

Log: `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/ctest/ctest.log`

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/ctest/ctest.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/ctest/ctest.xml`

Against the baseline: 0 new failure(s), 1 fixed, 11 known.

Fixed since the baseline:

- `GfxValidation.LayerIsInstalled`

### G1 capacity: pass

11 cases: 11 passed, 0 failed, 0 not run; binary linked 2026-11-08T15:19:51

Reproduce:

```
ctest --test-dir /home/user/ChronicleTwo/port/build/pc --output-junit /home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/capacity/ctest.xml --output-on-failure --parallel 4 --no-tests=ignore --timeout 600 -R ^Capacity
```

Log: `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/capacity/ctest.log`

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/capacity/ctest.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/capacity/ctest.xml`

### G2 content: fail

80 cases: 79 passed, 1 failed, 0 not run; failing: MoonModManagerUi.ChangesTypedSettingsNotifiesModsAndSurvivesReload; binary linked 2026-11-08T15:19:51

Reproduce:

```
ctest --test-dir /home/user/ChronicleTwo/port/build/pc --output-junit /home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/content/ctest.xml --output-on-failure --parallel 4 --no-tests=ignore --timeout 600 -R ^MoonMod -E '^MoonMod\.HeadlessProbe$'
```

Log: `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/content/ctest.log`

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/content/ctest.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/content/ctest.xml`

### G2 lua-harness: pass

10 harness(es) with /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua, 0 failed

Reproduce:

```
(cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/challenges/test_challenges.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/coop/test_coop.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/living-towns/test_living_towns.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/modern-cloud/test_modern_cloud.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/randomizer/test_randomizer.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/roguelike/test_runs.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/roguelike/test_standalone.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/rpg/test_rpg.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/sirfancybacon/test_pack.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/vigil/test_vigil.lua)
```

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/lua-harness/tools_tests_challenges_test_challenges.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/lua-harness/tools_tests_coop_test_coop.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/lua-harness/tools_tests_living-towns_test_living_towns.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/lua-harness/tools_tests_modern-cloud_test_modern_cloud.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/lua-harness/tools_tests_randomizer_test_randomizer.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/lua-harness/tools_tests_roguelike_test_runs.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/lua-harness/tools_tests_roguelike_test_standalone.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/lua-harness/tools_tests_rpg_test_rpg.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/lua-harness/tools_tests_sirfancybacon_test_pack.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/lua-harness/tools_tests_vigil_test_vigil.lua.log`

### G3 web-node: pass

node darkchronicle.js exited 3

Reproduce:

```
/opt/node22/bin/node darkchronicle.js --headless --offscreen --frames 30 --save /home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/web-node/smoke-save
```

Log: `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/web-node/node.log`

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/web-node/node.log`

### G3 web-browser: pass

Chromium smoke: webgpu driver exit 0, game exit 3; gl driver exit 0, game exit 3

Reproduce:

```
/opt/node22/bin/node /home/user/ChronicleTwo/tools/gauntlet/web/browser_smoke.cjs http://127.0.0.1:45977/index.html /smoke-data.zip /home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/web-browser/browser-webgpu.log --headless --offscreen --frames 30 ; /opt/node22/bin/node /home/user/ChronicleTwo/tools/gauntlet/web/browser_smoke.cjs 'http://127.0.0.1:45977/index.html?renderer=gl' /smoke-data.zip /home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/web-browser/browser-gl.log --headless --offscreen --frames 30
```

Log: `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/web-browser/smoke-run-webgpu.log`

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/web-browser/smoke-run-webgpu.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/web-browser/browser-webgpu.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/web-browser/smoke-run-gl.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/web-browser/browser-gl.log`

### G4 coop: pass

relay host/join handshake OK on ws://127.0.0.1:40553; build /home/user/ct-coop/port/build/pc; ctest pass: 27 engine case(s): 27 passed, 0 failed, 0 skipped; two-games pass: two_games.py exit 0: PASS two games met through the relay

Reproduce:

```
(cd /home/user/ChronicleTwo/tools/gauntlet/.cache/relay && node server.mjs --port 40553 --host 127.0.0.1 &) ; (cd /home/user/ChronicleTwo/tools/gauntlet/.cache/relay && node handshake.mjs ws://127.0.0.1:40553) ; ctest --test-dir /home/user/ct-coop/port/build/pc --output-junit /home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/coop/ctest.xml --output-on-failure --parallel 1 --no-tests=ignore --timeout 600 -R 'MoonModNet|MoonModCoop' (with DC2_COOP_RELAY=ws://127.0.0.1:40553) ; /usr/local/bin/python3 -I /home/user/ct-coop/tools/coop/two_games.py --build /home/user/ct-coop/port/build/pc --relay-dir /home/user/ChronicleTwo/tools/gauntlet/.cache/relay --node /opt/node22/bin/node --work /home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/coop/two-games
```

Log: `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/coop/two-games.log`

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/coop/relay.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/coop/handshake.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/coop/ctest.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108-final/coop/two-games.log`

### G1 toolkit-limits: error

only 2.8 GB free on the disk holding /home/user/DC2-Toolkit (the gauntlet wants 4.0 GB, --min-free-gb): free space first; the stage did not start

### G1 toolkit: error

only 2.8 GB free on the disk holding /home/user/DC2-Toolkit (the gauntlet wants 4.0 GB, --min-free-gb): free space first; the stage did not start
