# Full-cutscene checkpoint — 2026-10-11

Branch: `port/checkpoint-2026-10-11-cutscene` on `Jeffiest/ChronicleTwo`.
Base: `3139f445f3e55210780d409f673d41a3f9bf31d2`.

This checkpoint preserves the native HUD sprite-state fix, typed image-slot cleanup and zero-duration spline
endpoint handling. The user verified that the entire opening cutscene completes, with remaining bugs.
Battle transition fails during monster-script setup after zero-capacity arena allocation errors.

The next work is the host-compatibility audit described in `PORT_REEVALUATION_2026-10-11.md`, beginning with
script allocation sizes and scene/monster arena lifecycle. No upstream review branch was merged.

`PORT_STATE.md` and `HANDOFF.md` here are snapshots of the canonical files in the original
`E:\Chronicle-Two` workspace, taken during checkpoint preparation. The included regression source and runner
are preserved unchanged; the runner uses that workspace's Windows compiler and build paths and requires
the existing RelWithDebInfo build. `spline-regression.log` records both passing corrected paths and the expected
failure of the original implementation. The fresh regression rerun also exited successfully.

Validation: Windows RelWithDebInfo build passed before the manual full-cutscene verification; spline regression
rerun passes; source diff whitespace check passes. Skip cleanup has not received explicit user verification,
and playable combat has not been reached. `ps2/` is unchanged by these checkpoint fixes.
