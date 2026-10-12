# Twelve runtime contract assignments

Created 2026-10-11: four Claude, four Gemini, four Codex. These are **new queued assignments**, not twelve accepted fixes.
Read [COMMON_RULES.md](COMMON_RULES.md). Give each provider its handoff:

- [CLAUDE_HANDOFF.md](CLAUDE_HANDOFF.md): 01–04, combat and data reads.
- [GEMINI_HANDOFF.md](GEMINI_HANDOFF.md): 05–08, image copy, texture ownership and HUD.
- [CODEX_HANDOFF.md](CODEX_HANDOFF.md): 09–12, models, sound, enforcement and integration.
- [ALL_12_HANDOFF.md](ALL_12_HANDOFF.md): single combined handoff with every brief.

| ID | Team | Assignment | Status |
|---|---|---|---|
| 01 | Claude | [Action callback argument/result contracts](tasks/01-action-callbacks.md) | QUEUED |
| 02 | Claude | [Real PAL action bytecode execution](tasks/02-action-vm.md) | QUEUED |
| 03 | Claude | [A/Cross through attack selection and motion](tasks/03-combat-consumer.md) | QUEUED |
| 04 | Claude | [Named archive read failures and bounds](tasks/04-archive-extents.md) | QUEUED |
| 05 | Gemini | [Production menu local-to-local image copy](tasks/05-image-copy.md) | QUEUED |
| 06 | Gemini | [Texture atlas storage and palette identity](tasks/06-atlas-storage.md) | QUEUED |
| 07 | Gemini | [Recorded texture lifetime and stale handles](tasks/07-texture-lifetime.md) | QUEUED |
| 08 | Gemini | [HUD sprite, UV, blend and draw-state propagation](tasks/08-hud-state.md) | QUEUED |
| 09 | Codex | [Real weighted character/equipment ownership](tasks/09-weighted-models.md) | QUEUED |
| 10 | Codex | [Event-to-CSound voice and PCM lifecycle](tasks/10-voice-streams.md) | QUEUED |
| 11 | Codex | [Compiler-backed active contract enforcement](tasks/11-contract-enforcement.md) | QUEUED |
| 12 | Codex | [Central integration and production acceptance](tasks/12-integration.md) | QUEUED |

## Execution

First read canonical upstream integration status; do not race a build freeze. Begin independent reproductions in parallel by exclusive file ownership. Tasks 01/02 feed 03; 05/06 agree on atlas storage/copy, and 07/08 share resource/draw-state evidence. Task 12 prepares integration fixtures while workers run.

Production menu-copy and action execution are the first acceptance milestones. Workers return precise results and state rows in their own files. Task 12 reviews changes, coordinates the edit freeze, builds centrally and records every integrated edit in PORT_STATE.md before reporting completion. No task is FIXED just because it returned an audit or a test count.

After appropriate tests pass, use one user-controlled Items/attack/HUD/audio run. Preserve gameplay acceptance as pending until the user verifies it. Current source fixes in the upstream integration do not establish that these gameplay failures are repaired.

This package does not dispatch providers or start its twelve assignments automatically.
