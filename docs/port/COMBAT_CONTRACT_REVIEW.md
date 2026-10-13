# Controller-to-combat contract review

Date: 2026-10-11
Branch: `port/upstream-first-2026-10-11`

## Evidence reviewed

- PAL English binding: `port/src/mainloop.cpp` maps `PAD_BTN_ACTION_CONFIRM` and `PAD_BTN_ACTION_HELD` to the language's confirm button; English confirm is `PAD_CROSS` (the Xbox South/A button in the input fixture). The mapping is intentionally retained.
- Component fixture: `port/src/tests/input_lifecycle_test.cpp` presses Xbox South and verifies action-confirm goes high for one frame and action-held remains high until release. It also checks Xbox East maps to action-cancel. This verifies the host mapping, `CGamePad` update, and `CPadControl` sample, not gameplay consumption.
- Combat consumer: `ps2/src/actionchara.cpp` ORs `PadCtrl.Btn(PAD_BTN_ACTION_CONFIRM)` into `pad_history`, increments `acumu_pad` while `PAD_BTN_ACTION_HELD` is active, then starts/resumes the action script. This agrees with the intended A/Cross action.
- Combat gates: `ps2/src/dng_main.cpp` skips `RunScript` in eye view and while `DNG_PAUSE_PLAYER_CONTROL` or `DebugPause` is active. Eye view exits on R2 or `PAD_BTN_CANCEL`; player-control pause resets eye view. Menu entry returns before the normal action-script call. These are plausible gates, but source inspection cannot establish their runtime values during the user's failed attack.
- Sampling order: `port/src/mainloop.cpp` calls `LoopMain[LoopNo]()` before `GamePad__2.UpDate()` and `PadCtrl.Update()`. Thus game code consumes the most recent completed sample; a newly polled button becomes visible to the next loop call. This is consistent with the retail loop order and does not alone explain a missing attack.
- Argument storage: `ps2/src/scriptinterpreter.cpp` parses textual arguments in order and pushes each onto the stack before invoking the tag. Port `CScriptInterpreter::PushStack` copies the complete `SPI_STACK`, preserving host-width string pointers; no argument-order mismatch was found in this generic interpreter path. The player combat path uses `CActionChara::script` in `actionchara.cpp`, so the generic tag parser does not prove the action bytecode's runtime inputs or state transitions. No sibling operator repair is justified by the reviewed evidence.

## Result

No first semantic divergence is proven from the available fixture and source. Preserve the Cross binding. In particular, first-person mode is a verified script gate, but there is no evidence that it was active when the user pressed attack; it is not identified as the cause.

## Integrated passive trace (`DC2_TRACE_COMBAT`)

The integration adds `port/include/combat_trace.hpp`, a sample hook after `PadCtrl.Update`,
and exact-anchor rewrites of generated host `dng_main.cpp`/`actionchara.cpp`. Retail sources
are unchanged. Enabled output records logical confirm/held, eye/pause/debug gates and
program/end/history/accumulation before/after action dispatch. It emits changed states only,
with a global 256-record cap; absent, empty or `0` environment values disable it. Source
anchor checks and the game build pass. It does not yet record raw controller/connection,
every early-return reason or opcode/PC. The broader proposal below remains the design reference.

Wire an environment-gated, passive trace at these production points:

1. `port/src/mainloop.cpp`, immediately after `PadCtrl.Update`: record frame, loop number, connection state, raw Cross/South held/pressed state, and logical `ACTION_CONFIRM`, `ACTION_HELD`, and `ACTION_CANCEL` values. This distinguishes host polling and language binding from later consumption.
2. `ps2/src/dng_main.cpp`, immediately before eye-view/menu/pause early returns and immediately before `MainChara__2->RunScript`: record `eye_view`, `DNG_PAUSE_PLAYER_CONTROL`, `DebugPause`, mode, active character, and whether the action step is called. Include a reason code for each skip/return.
3. `ps2/src/actionchara.cpp`, at the beginning of the player action step and around `script.run`/`script.resume`: record `prog_no`, `pad_history` before/after confirm OR, `acumu_pad` before/after, script end/state, and the script program/PC or opcode if exposed by the action-script object. Emit only on confirm/held transitions or a changed program/state, to keep the trace bounded.

Use `getenv("DC2_TRACE_COMBAT")` once to enable the trace, write to `stderr`, and cap output (for example, 256 records). Do not mutate game state, consume input, or alter script scheduling. A recording should cover one failed attack press/release, with several frames before and after; the first point where a rising confirm sample disappears, a skip reason appears, or program/state fails to advance identifies the next owner to inspect.

## Integration blocker

A proof requires one user-controlled dungeon recording with the diagnostic build and `DC2_TRACE_COMBAT=1`. The controller fixture has no production dungeon state, and this task did not launch gameplay. Hooks are now integrated through native/generated-host code. No gameplay cause or repair is claimed.
