# Gauntlet report

Created 2026-11-08T10:07:08+00:00 on vm (Linux-6.18.44-fc-v310-x86_64-with-glibc2.39, 4 CPUs, 15.7 GB).
Verdict: **FAIL** (exit 1).

Arguments: `--stage ctest --report-dir tools/gauntlet/report/ctest-rerun-20261108`

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
| G0 | ctest | error | 60.0s | darkchronicle_tests was replaced during the run (2365 case(s) could not find the executable; binary linked 2026-11-08T04:49:01 at the start, 2026-11-08T10:07:51 at the end): rerun when the build is quiet; 2430 cases: 44 passed, 11 failed, 2375 not run |

### G0 ctest: error

darkchronicle_tests was replaced during the run (2365 case(s) could not find the executable; binary linked 2026-11-08T04:49:01 at the start, 2026-11-08T10:07:51 at the end): rerun when the build is quiet; 2430 cases: 44 passed, 11 failed, 2375 not run

Reproduce:

```
ctest --test-dir /home/user/ChronicleTwo/port/build/pc --output-junit /home/user/ChronicleTwo/tools/gauntlet/report/ctest-rerun-20261108/ctest/ctest.xml --output-on-failure --parallel 4 --no-tests=ignore --timeout 600 -LE gpu
```

Log: `/home/user/ChronicleTwo/tools/gauntlet/report/ctest-rerun-20261108/ctest/ctest.log`

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/ctest-rerun-20261108/ctest/ctest.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/ctest-rerun-20261108/ctest/ctest.xml`
