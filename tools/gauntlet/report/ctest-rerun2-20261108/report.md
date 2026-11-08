# Gauntlet report

Created 2026-11-08T10:32:40+00:00 on vm (Linux-6.18.44-fc-v310-x86_64-with-glibc2.39, 4 CPUs, 15.7 GB).
Verdict: **FAIL** (exit 1).

Arguments: `--stage ctest --report-dir tools/gauntlet/report/ctest-rerun2-20261108`

## Repositories

| Repository | Head | Branch | Dirty |
|---|---|---|---|
| ChronicleTwo | `58dd0868a899` | claude/dark-cloud-2-engine-yr3rlk | yes |
| DC2-Toolkit | `b5dc75ee755e` | claude/dark-cloud-2-engine-yr3rlk | yes |
| MoonMod | `875a2350f71a` | claude/dark-cloud-2-engine-yr3rlk | yes |
| DC_Web | `49d71f750e25` | claude/dark-cloud-2-engine-yr3rlk | no |

## Stages

| Gate | Stage | Status | Time | Summary |
|---|---|---|---|---|
| G0 | ctest | error | 203.7s | ctest wrote no results: 	2762 - Luau.Prune (Failed) 	2766 - Matrix.Runner (Failed) 	2767 - PalTiming.IdentitySelfTest (Failed) 	2778 - dc2data_bridge (Subprocess aborted) Errors while running CTest |

### G0 ctest: error

ctest wrote no results: 	2762 - Luau.Prune (Failed)
	2766 - Matrix.Runner (Failed)
	2767 - PalTiming.IdentitySelfTest (Failed)
	2778 - dc2data_bridge (Subprocess aborted)
Errors while running CTest

Reproduce:

```
ctest --test-dir /home/user/ChronicleTwo/port/build/pc --output-junit /home/user/ChronicleTwo/tools/gauntlet/report/ctest-rerun2-20261108/ctest/ctest.xml --output-on-failure --parallel 4 --no-tests=ignore --timeout 600 -LE gpu
```

Log: `/home/user/ChronicleTwo/tools/gauntlet/report/ctest-rerun2-20261108/ctest/ctest.log`

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/ctest-rerun2-20261108/ctest/ctest.log`
