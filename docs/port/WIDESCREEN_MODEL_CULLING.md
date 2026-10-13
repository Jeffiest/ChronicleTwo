# Wide-fill model visibility

## Symptom

At wide resolutions, the user saw people shift with the camera and vanish at the old 4:3 side bounds while the background remained visible. That points to the model culling and model clip transform using a different visible extent from the active render target.

## Upstream renderer path

The synced renderer derives `mgPortVisibleExtent()` from `gfx::VisibleLogicalRect()` for the active target. `mgSetRenderInfo()` updates `mgRENDER_INFO::screen_box_min/max` to that extent, and `mgCFrame::GetDrawRect()` culls against the same bounds. This keeps the model hierarchy's visibility test aligned with the pixels the target shows.

The native mesh path in `port/src/mg_visual.cpp` applies `VisibleClipFit()` to model clip matrices. The fit maps the retail guard band onto the active target's visible extent and accounts for its center. A 4:3 target produces the identity fit; a wider Fill target keeps models from being clipped at the original 4:3 edges. `mgPortVisibleExtent()` follows the active render target, so the calculation also works while rendering into another target.

This is the upstream `VisibleClipFit` design, not the earlier local `PortMgScreenToClip` experiment. That experiment and its projection test were removed during the sync because the upstream renderer now owns the model mesh and clip path.

## Regression coverage

- `MgVisual.VisibleExtentKeepsBoxAtSixteenNine` verifies a model box remains drawable at 16:9.
- `MgVisual.VisibleExtentCullsBoxAtFourThree` verifies the same box remains culled when it lies outside a 4:3 target.
- `mglib_aspect_test.cpp` covers visible extents and GS/logical coordinate mapping for supported aspect modes.

## Runtime status

The user confirms Max and the camera were moving together when the circus room looked disordered. Their indoor screenshots show Max intact while nearby machinery and props occlude or cross the view. The copied File 2 replay combines forward movement with a right-stick turn at the tent frontage and reproduces close foreground obstruction; its interpolated display samples change view angle while Max's model stays intact. The replay does not reproduce the same indoor route, so the camera-collision cause is still unproven. A read of `C:\Users\armor\AppData\Local\chronicletwo\save\config.json` on 2026-10-12 showed `video.present_mode: mailbox`, `video.interpolation: false` and `video.aspect: auto`; an already-running process may have loaded earlier values. `auto` selects wide Fill. The camera matching change applies only when interpolation is active. VSync/present mode changes presentation cadence and cannot correct a game camera or geometry transform. The earlier 4:3 Letterbox walk shows the same outdoor close-up as Fill, so that obstruction is not specific to widescreen.

A matched real-data A/B now runs the copied File 2 save through frame 26000, with the same input, 1280x720 Fill layout, and `video.interpolation=true` in both builds. The canonical PNGs are byte-identical. Across eight interpolated display samples, mean RGB error to the canonical frame is 0.179 for the patched camera-matching build and 9.022 for baseline. The run turns and walks Max up to the circus tent; he stays intact while close props occlude the view, and neither log has missing-texture or self-sampling warnings. This confirms that camera-record pairing creates visible motion artifacts when interpolation is active. It still does not reach the user's dark backstage scene or reproduce the distant-character disappearance. Headless `PresentScreenshotDisplay` renders samples regardless of the config, so the A/B validates the renderer path on real game data, not the live config toggle. Captures and logs are under `win/build-rel/diag-circus-camera-turn-20261013/` and `win/build-rel/diag-circus-camera-turn-baseline-20261013/`.

The experimental executable is `win/build-rel/darkchronicle_camera.exe`; its copied save `win/build-rel/diag-camera-match-patched/config.json` enables interpolation. The standard `darkchronicle.exe` remains the baseline. The matching change is local Kazarov commit `743e087` on `fix/camera-interpolation-matching`; TheMoonPeople/Kazarov `main` remains `a0a5ff8`, and pushing the change there returned HTTP 403, so the ChronicleTwo submodule pointer was not changed. The PR branch remains at `300fd2a7`. The observed global config is `mailbox`, interpolation false, aspect auto; changing VSync/present mode does not change camera transforms.

Headless real-data captures from a scratch copy of File 2 (`Palm Brinks / A night at the circus`) confirm the corrected path at 1280x720 Fill. A partial right turn at frame 15140, a full right turn at frame 15400, and a full left turn at frame 15400 show Max and nearby NPCs at the corresponding camera angles without the old inner 4:3 lines or bright dropped-draw blocks. The replay logs contain zero main-target self-sampling and missing-texture warnings after the snapshot fix. The current `195a6ee0` executable was replayed at `--window 1280x720` through frame 15180 with both right and left camera input; Max and NPCs remain visible past the former 4:3 edge, with zero self-sampling/missing-texture warnings. Captures and logs are under `win/build-rel/verify-post195-camera/` and `win/build-rel/verify-post195-camera-left/`. A horizontal ground seam is also visible in the left-angle image at both 4:3 and 16:9, so it is distinct from the old inner 4:3 boundaries and remains unexplained. This verifies the town wide-fill case, but the user's distant bench/cutscene disappearance and movie playback remain open.
