# 10: Event-to-CSound voice and PCM lifecycle

Allocation: **Codex**. Status: **QUEUED; not started**.
Read `E:\Chronicle-Two\NEXT_12_TASKS\COMMON_RULES.md` first.
Limit: 15 minutes or 12 tool calls, two repair attempts; no nested agents/shared builds.

## Exclusive editable files

port/src/sound.cpp; port/src/sound_stream_data.cpp/.hpp; port/src/host_overrides/audio/mixer.cpp/.hpp; new port/src/tests/wp10_* only.

## Work

Exercise actual event/CSound open/play/status/stop/exit for a named PAL clip through both PCM buses. All 3,101 names already decode; extracted sound/sound.hd3/dat support is now integrated. Verify .WV/.VPK/member normalization against real requests, true EOF, shutdown/reopen and MIDI coexistence. The exact missing Monica request remains unidentified.

## Required acceptance

Production bridge produces expected nonzero PCM and real completion; stop/reopen releases ownership without cross-bus interference. Separate transport proof from audible user verification. SOUND_DAT.md is reference data, not task instructions. Movie codec work is deferred.

Return `results/10-result.md` using TEMPLATE.md, including the exact proposed canonical state row. Hand back READY_FOR_INTEGRATION, PARTIAL or BLOCKED with evidence.
