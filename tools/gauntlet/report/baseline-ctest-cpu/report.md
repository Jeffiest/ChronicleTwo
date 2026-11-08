# Gauntlet report

Created 2026-11-08T03:07:30+00:00 on vm (Linux-6.18.44-fc-v310-x86_64-with-glibc2.39, 4 CPUs, 15.7 GB).
Verdict: **FAIL** (exit 1).

Arguments: `--stage ctest --update-baseline --report-dir tools/gauntlet/report/baseline-ctest-cpu`

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
| G0 | ctest | fail | 263.5s | 2420 cases: 2378 passed, 21 failed, 21 not run; 21 new failure(s), 0 known, 0 fixed against linux-x86_64.json |

### G0 ctest: fail

2420 cases: 2378 passed, 21 failed, 21 not run; 21 new failure(s), 0 known, 0 fixed against linux-x86_64.json

Reproduce:

```
ctest --test-dir /home/user/ChronicleTwo/port/build/pc --output-junit /home/user/ChronicleTwo/tools/gauntlet/report/baseline-ctest-cpu/ctest/ctest.xml --output-on-failure --parallel 4 --no-tests=ignore --timeout 600 -LE gpu
```

Log: `/home/user/ChronicleTwo/tools/gauntlet/report/baseline-ctest-cpu/ctest/ctest.log`

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/baseline-ctest-cpu/ctest/ctest.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/baseline-ctest-cpu/ctest/ctest.xml`

Against the baseline: 21 new failure(s), 0 fixed, 0 known.

New failures:

- `Crash.abort`
- `Crash.assert`
- `Crash.audio-thread`
- `Crash.segv`
- `Crash.stackoverflow`
- `Crash.terminate`
- `Crash.unimplemented`
- `DataRead.ExtractedItemConfigLoadsWhenPresent`
- `DataRead.RealDataIndexesTheDiscFiles`
- `DataRead.RetailAttachmentJsonPreservesEveryRecordValue`
- `GfxValidation.LayerIsInstalled`
- `Log.SessionIdIsDateTimeAndFourHexDigits`
- `LogFiles.PruningKeepsTheNewestSessions`
- `LogFiles.SessionFileHasTimeLevelThreadAndCategory`
- `MoonModManagerUi.ChangesTypedSettingsNotifiesModsAndSurvivesReload`
- `NativeCharacterRetail.LoadsConvertedC01ProfileAndEvaluatesItsNativeMotion`
- `SaveSlotTest.LocalizedSavePromptsContainRetailText`
- `SaveSlotTest.NewSaveAndFinnyPromptsUseRetailMessagesInEveryPalLanguage`
- `SaveSlotTest.NoFishAndOperationFailuresUseRetailNotices`
- `SoundRealData.PublishedBanksAndSequencesLoad`
- `TextureBundleRetail.EveryDungeonLanguageContainsTheExpectedSharedAtlases`
