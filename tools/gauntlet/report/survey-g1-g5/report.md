# Gauntlet report

Created 2026-11-08T03:13:31+00:00 on vm (Linux-6.18.44-fc-v310-x86_64-with-glibc2.39, 4 CPUs, 15.7 GB).
Verdict: **FAIL** (exit 1).

Arguments: `--gate G1 --gate G2 --gate G3 --gate G4 --gate G5 --keep-going --report-dir tools/gauntlet/report/survey-g1-g5`

## Repositories

| Repository | Head | Branch | Dirty |
|---|---|---|---|
| ChronicleTwo | `cb6ae0f3ab9d` | claude/dark-cloud-2-engine-yr3rlk | yes |
| DC2-Toolkit | `b5dc75ee755e` | claude/dark-cloud-2-engine-yr3rlk | yes |
| MoonMod | `875a2350f71a` | claude/dark-cloud-2-engine-yr3rlk | no |
| DC_Web | `49d71f750e25` | claude/dark-cloud-2-engine-yr3rlk | no |

## Stages

| Gate | Stage | Status | Time | Summary |
|---|---|---|---|---|
| G1 | capacity | not_run | 0.1s | no Capacity* tests in the build yet (work package A adds them) |
| G1 | toolkit-limits | pass | 227.3s | 5 test(s) matched, 0 failed |
| G2 | content | fail | 10.6s | 80 cases: 79 passed, 1 failed, 0 not run; failing: MoonModManagerUi.ChangesTypedSettingsNotifiesModsAndSurvivesReload |
| G2 | lua-harness | pass | 3.8s | 10 harness(es) with /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua, 0 failed |
| G3 | web-node | not_run | 0.0s | no web build at /home/user/ChronicleTwo/port/build/web (work package C) |
| G3 | web-browser | not_run | 0.0s | no web build at /home/user/ChronicleTwo/port/build/web (work package C) |
| G4 | coop | not_run | 0.6s | relay host/join handshake OK on ws://127.0.0.1:56501; no engine co-op tests ('MoonModNet/MoonModCoop') in the build yet (work package D) |
| G5 | retail-title | not_run | 0.0s | no retail data: set DC2_DATA to an extracted root, or DC2_ISO / rom/pal/*.iso to a disc image |
| G5 | retail-smokes | not_run | 0.0s | no retail data: set DC2_DATA to an extracted root, or DC2_ISO / rom/pal/*.iso to a disc image |
| G5 | retail-determinism | not_run | 0.0s | no retail data: set DC2_DATA to an extracted root, or DC2_ISO / rom/pal/*.iso to a disc image |
| G5 | retail-golden | not_run | 0.0s | no retail data: set DC2_DATA to an extracted root, or DC2_ISO / rom/pal/*.iso to a disc image |

### G1 capacity: not_run

no Capacity* tests in the build yet (work package A adds them)

Reproduce:

```
ctest --test-dir /home/user/ChronicleTwo/port/build/pc --output-junit /home/user/ChronicleTwo/tools/gauntlet/report/survey-g1-g5/capacity/ctest.xml --output-on-failure --parallel 4 --no-tests=ignore --timeout 600 -R ^Capacity
```

Log: `/home/user/ChronicleTwo/tools/gauntlet/report/survey-g1-g5/capacity/ctest.log`

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/survey-g1-g5/capacity/ctest.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/survey-g1-g5/capacity/ctest.xml`

### G1 toolkit-limits: pass

5 test(s) matched, 0 failed

Reproduce:

```
cargo test --release -p dc2tk-assets limits
```

Log: `/home/user/ChronicleTwo/tools/gauntlet/report/survey-g1-g5/toolkit-limits/cargo-test.log`

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/survey-g1-g5/toolkit-limits/cargo-test.log`

### G2 content: fail

80 cases: 79 passed, 1 failed, 0 not run; failing: MoonModManagerUi.ChangesTypedSettingsNotifiesModsAndSurvivesReload

Reproduce:

```
ctest --test-dir /home/user/ChronicleTwo/port/build/pc --output-junit /home/user/ChronicleTwo/tools/gauntlet/report/survey-g1-g5/content/ctest.xml --output-on-failure --parallel 4 --no-tests=ignore --timeout 600 -R ^MoonMod -E '^MoonMod\.HeadlessProbe$'
```

Log: `/home/user/ChronicleTwo/tools/gauntlet/report/survey-g1-g5/content/ctest.log`

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/survey-g1-g5/content/ctest.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/survey-g1-g5/content/ctest.xml`

### G2 lua-harness: pass

10 harness(es) with /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua, 0 failed

Reproduce:

```
(cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/challenges/test_challenges.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/coop/test_coop.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/living-towns/test_living_towns.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/modern-cloud/test_modern_cloud.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/randomizer/test_randomizer.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/roguelike/test_runs.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/roguelike/test_standalone.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/rpg/test_rpg.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/sirfancybacon/test_pack.lua) ; (cd /home/user/MoonMod && /home/user/ChronicleTwo/tools/gauntlet/.cache/lua/lua tools/tests/vigil/test_vigil.lua)
```

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/survey-g1-g5/lua-harness/tools_tests_challenges_test_challenges.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/survey-g1-g5/lua-harness/tools_tests_coop_test_coop.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/survey-g1-g5/lua-harness/tools_tests_living-towns_test_living_towns.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/survey-g1-g5/lua-harness/tools_tests_modern-cloud_test_modern_cloud.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/survey-g1-g5/lua-harness/tools_tests_randomizer_test_randomizer.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/survey-g1-g5/lua-harness/tools_tests_roguelike_test_runs.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/survey-g1-g5/lua-harness/tools_tests_roguelike_test_standalone.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/survey-g1-g5/lua-harness/tools_tests_rpg_test_rpg.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/survey-g1-g5/lua-harness/tools_tests_sirfancybacon_test_pack.lua.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/survey-g1-g5/lua-harness/tools_tests_vigil_test_vigil.lua.log`

### G4 coop: not_run

relay host/join handshake OK on ws://127.0.0.1:56501; no engine co-op tests ('MoonModNet|MoonModCoop') in the build yet (work package D)

Reproduce:

```
(cd /home/user/ChronicleTwo/tools/gauntlet/.cache/relay && node server.mjs --port 56501 --host 127.0.0.1 &) ; (cd /home/user/ChronicleTwo/tools/gauntlet/.cache/relay && node handshake.mjs ws://127.0.0.1:56501) ; ctest --test-dir /home/user/ChronicleTwo/port/build/pc --output-junit /home/user/ChronicleTwo/tools/gauntlet/report/survey-g1-g5/coop/ctest.xml --output-on-failure --parallel 1 --no-tests=ignore --timeout 600 -R 'MoonModNet|MoonModCoop' (with DC2_COOP_RELAY=ws://127.0.0.1:56501)
```

Log: `/home/user/ChronicleTwo/tools/gauntlet/report/survey-g1-g5/coop/handshake.log`

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/survey-g1-g5/coop/npm-install.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/survey-g1-g5/coop/relay.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/survey-g1-g5/coop/handshake.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/survey-g1-g5/coop/ctest.log`
