# Gauntlet report

Created 2026-11-08T04:44:30+00:00 on vm (Linux-6.18.44-fc-v310-x86_64-with-glibc2.39, 4 CPUs, 15.7 GB).
Verdict: **FAIL** (exit 1).

Arguments: `--no-build --keep-going --report-dir tools/gauntlet/report/full-20261108`

## Repositories

| Repository | Head | Branch | Dirty |
|---|---|---|---|
| ChronicleTwo | `58dd0868a899` | claude/dark-cloud-2-engine-yr3rlk | yes |
| DC2-Toolkit | `b5dc75ee755e` | claude/dark-cloud-2-engine-yr3rlk | yes |
| MoonMod | `875a2350f71a` | claude/dark-cloud-2-engine-yr3rlk | no |
| DC_Web | `49d71f750e25` | claude/dark-cloud-2-engine-yr3rlk | no |

## Stages

| Gate | Stage | Status | Time | Summary |
|---|---|---|---|---|
| G0 | env | pass | 1.2s | 23 checks; optional absent: lua |
| G0 | build | not_run | 0.0s | skipped by --no-build; the stages test the existing binaries; binary linked 2026-11-08T04:43:21 |
| G0 | ctest | fail | 204.4s | 2430 cases: 1271 passed, 21 failed, 1138 not run; 14 new failure(s), 7 known, 0 fixed against linux-x86_64.json; binary linked 2026-11-08T04:43:21 |
| G1 | capacity | fail | 1.9s | 10 cases: 9 passed, 1 failed, 0 not run; failing: Capacity.SaveRoundTripsTheRaisedCountsAndLoadsRetailCounts; binary linked 2026-11-08T04:47:45 |
| G1 | toolkit | fail | 297.9s | toolkit: env pass, fmt pass, clippy pass (0 new, 1 known, 0 fixed), cargo-test fail (4 new, 0 known, 0 fixed), tool-tests pass, port-tests pass, port-golden not_run, event-sweep not_run; cargo-test: 796 tests in 4 crates: 792 passed, 4 failed, 36 ignored; 4 new, 0 known, 0 fixed against linux-x86_64.json |
| G1 | toolkit-limits | fail | 0.2s | toolkit: limits fail; limits: 8 limit test(s), 1 failed: georama_data::tests::the_georama_tables_limits_are_the_profiles |
| G2 | content | fail | 10.2s | 80 cases: 79 passed, 1 failed, 0 not run; failing: MoonModManagerUi.ChangesTypedSettingsNotifiesModsAndSurvivesReload; binary linked 2026-11-08T04:49:01 |
| G2 | lua-harness | pass | 4.5s | 10 harness(es) with /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua, 0 failed |
| G3 | web-node | not_run | 0.0s | no web build at /home/user/ChronicleTwo/port/build/web (work package C) |
| G3 | web-browser | not_run | 0.0s | no web build at /home/user/ChronicleTwo/port/build/web (work package C) |
| G4 | coop | not_run | 0.5s | relay host/join handshake OK on ws://127.0.0.1:41973; no engine co-op tests ('MoonModNet/MoonModCoop') in the build yet (work package D) |
| G5 | retail-title | not_run | 0.0s | no retail data: set DC2_DATA to an extracted root, or DC2_ISO / rom/pal/*.iso to a disc image |
| G5 | retail-smokes | not_run | 0.0s | no retail data: set DC2_DATA to an extracted root, or DC2_ISO / rom/pal/*.iso to a disc image |
| G5 | retail-determinism | not_run | 0.0s | no retail data: set DC2_DATA to an extracted root, or DC2_ISO / rom/pal/*.iso to a disc image |
| G5 | retail-golden | not_run | 0.0s | no retail data: set DC2_DATA to an extracted root, or DC2_ISO / rom/pal/*.iso to a disc image |

### G0 env: pass

23 checks; optional absent: lua

Reproduce:

```
python3 -I tools/gauntlet/run.py --stage env
```

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/env/env.json`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/env/env.txt`

### G0 build: not_run

skipped by --no-build; the stages test the existing binaries; binary linked 2026-11-08T04:43:21

Reproduce:

```
. tools/gauntlet/env.sh && ct_build
```

### G0 ctest: fail

2430 cases: 1271 passed, 21 failed, 1138 not run; 14 new failure(s), 7 known, 0 fixed against linux-x86_64.json; binary linked 2026-11-08T04:43:21

Reproduce:

```
ctest --test-dir /home/user/ChronicleTwo/port/build/pc --output-junit /home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/ctest/ctest.xml --output-on-failure --parallel 4 --no-tests=ignore --timeout 600 -LE gpu
```

Log: `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/ctest/ctest.log`

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/ctest/ctest.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/ctest/ctest.xml`

Against the baseline: 14 new failure(s), 0 fixed, 7 known.

New failures:

- `Capacity.InventoryPlacesFillAndReadBackThroughTheManager`
- `Capacity.SaveRoundTripsTheRaisedCountsAndLoadsRetailCounts`
- `CtArenaPlacement.RetailRunsAgainstHostSizes`
- `GeoramaParts.ModelsMoveToNewHeapsAndFreeFromTheirOwn`
- `MoonModDungeonRegistration.ConfiguresProceduralDungeonLayoutLimits`
- `NativeArchitecture.Check`
- `NativeConfigLoaders.NpcRecordsKeepTheTableBoundsAndNullTexts`
- `NativeDungeon.MonsterFloorsFillOnlyTheCurrentFloor`
- `SaveJson.RandomImagesRoundTripByteForByte`
- `SaveJson.TextFieldsAreReadableAndOddBytesSurvive`
- `SaveJson.VersionsKeysAndValuesAreChecked`
- `SaveSlotTest.CatalogHasNoRetailSlotLimitAndOnlyOneNewRow`
- `SaveSlotTest.MenuScrollsPastThirteenAndClampsItsVisibleWindow`
- `TestHooks.JumpAcceptsModesAndBounds`

### G1 capacity: fail

10 cases: 9 passed, 1 failed, 0 not run; failing: Capacity.SaveRoundTripsTheRaisedCountsAndLoadsRetailCounts; binary linked 2026-11-08T04:47:45

Reproduce:

```
ctest --test-dir /home/user/ChronicleTwo/port/build/pc --output-junit /home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/capacity/ctest.xml --output-on-failure --parallel 4 --no-tests=ignore --timeout 600 -R ^Capacity
```

Log: `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/capacity/ctest.log`

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/capacity/ctest.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/capacity/ctest.xml`

### G1 toolkit: fail

toolkit: env pass, fmt pass, clippy pass (0 new, 1 known, 0 fixed), cargo-test fail (4 new, 0 known, 0 fixed), tool-tests pass, port-tests pass, port-golden not_run, event-sweep not_run; cargo-test: 796 tests in 4 crates: 792 passed, 4 failed, 36 ignored; 4 new, 0 known, 0 fixed against linux-x86_64.json

Reproduce:

```
(cd /home/user/DC2-Toolkit && CHRONICLE_TWO=/home/user/ChronicleTwo python3 -I tools/gauntlet/run.py --gate G0 --gate G5)
```

Log: `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/toolkit/runner.log`

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/toolkit/toolkit/report.json`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/toolkit/toolkit/report.md`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/toolkit/toolkit/env/env.json`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/toolkit/toolkit/env/env.txt`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/toolkit/toolkit/fmt/fmt.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/toolkit/toolkit/clippy/dc2tk-disc.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/toolkit/toolkit/clippy/dc2tk-script.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/toolkit/toolkit/clippy/dc2tk-assets.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/toolkit/toolkit/clippy/dc2tk.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/toolkit/toolkit/cargo-test/dc2tk-disc.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/toolkit/toolkit/cargo-test/dc2tk-script.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/toolkit/toolkit/cargo-test/dc2tk-assets.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/toolkit/toolkit/cargo-test/dc2tk.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/toolkit/toolkit/tool-tests/test_engine_mods.py.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/toolkit/toolkit/tool-tests/test_native_prepare.py.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/toolkit/toolkit/port-tests/port-tests.log`

### G1 toolkit-limits: fail

toolkit: limits fail; limits: 8 limit test(s), 1 failed: georama_data::tests::the_georama_tables_limits_are_the_profiles

Reproduce:

```
(cd /home/user/DC2-Toolkit && CHRONICLE_TWO=/home/user/ChronicleTwo python3 -I tools/gauntlet/run.py --stage limits)
```

Log: `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/toolkit-limits/runner.log`

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/toolkit-limits/toolkit/report.json`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/toolkit-limits/toolkit/report.md`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/toolkit-limits/toolkit/limits/limits.log`

### G2 content: fail

80 cases: 79 passed, 1 failed, 0 not run; failing: MoonModManagerUi.ChangesTypedSettingsNotifiesModsAndSurvivesReload; binary linked 2026-11-08T04:49:01

Reproduce:

```
ctest --test-dir /home/user/ChronicleTwo/port/build/pc --output-junit /home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/content/ctest.xml --output-on-failure --parallel 4 --no-tests=ignore --timeout 600 -R ^MoonMod -E '^MoonMod\.HeadlessProbe$'
```

Log: `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/content/ctest.log`

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/content/ctest.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/content/ctest.xml`

### G2 lua-harness: pass

10 harness(es) with /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua, 0 failed

Reproduce:

```
(cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/challenges/test_challenges.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/coop/test_coop.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/living-towns/test_living_towns.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/modern-cloud/test_modern_cloud.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/randomizer/test_randomizer.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/roguelike/test_runs.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/roguelike/test_standalone.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/rpg/test_rpg.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/sirfancybacon/test_pack.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/vigil/test_vigil.lua)
```

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/lua-harness/tools_tests_challenges_test_challenges.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/lua-harness/tools_tests_coop_test_coop.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/lua-harness/tools_tests_living-towns_test_living_towns.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/lua-harness/tools_tests_modern-cloud_test_modern_cloud.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/lua-harness/tools_tests_randomizer_test_randomizer.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/lua-harness/tools_tests_roguelike_test_runs.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/lua-harness/tools_tests_roguelike_test_standalone.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/lua-harness/tools_tests_rpg_test_rpg.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/lua-harness/tools_tests_sirfancybacon_test_pack.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/lua-harness/tools_tests_vigil_test_vigil.lua.log`

### G4 coop: not_run

relay host/join handshake OK on ws://127.0.0.1:41973; no engine co-op tests ('MoonModNet|MoonModCoop') in the build yet (work package D)

Reproduce:

```
(cd /home/user/ChronicleTwo/tools/gauntlet/.cache/relay && node server.mjs --port 41973 --host 127.0.0.1 &) ; (cd /home/user/ChronicleTwo/tools/gauntlet/.cache/relay && node handshake.mjs ws://127.0.0.1:41973) ; ctest --test-dir /home/user/ChronicleTwo/port/build/pc --output-junit /home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/coop/ctest.xml --output-on-failure --parallel 1 --no-tests=ignore --timeout 600 -R 'MoonModNet|MoonModCoop' (with DC2_COOP_RELAY=ws://127.0.0.1:41973)
```

Log: `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/coop/handshake.log`

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/coop/relay.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/coop/handshake.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/full-20261108/coop/ctest.log`
