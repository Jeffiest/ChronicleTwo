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

The user confirms Max and the camera were moving together when the circus room looked disordered. Their indoor screenshots show Max intact while nearby machinery and props occlude or cross the view. The copied File 2 replay combines forward movement with a right-stick turn at the tent frontage and reproduces close foreground obstruction; its interpolated display samples change view angle while Max's model stays intact. The replay does not reproduce the same indoor route, so the camera-collision cause is still unproven. The user's current A/B is to disable `video.interpolation` while leaving FIFO present mode unchanged; VSync alone is not yet implicated. The earlier 4:3 Letterbox walk shows the same outdoor close-up as Fill, so that obstruction is not specific to widescreen.

Headless real-data captures from a scratch copy of File 2 (`Palm Brinks / A night at the circus`) confirm the corrected path at 1280x720 Fill. A partial right turn at frame 15140, a full right turn at frame 15400, and a full left turn at frame 15400 show Max and nearby NPCs at the corresponding camera angles without the old inner 4:3 lines or bright dropped-draw blocks. The replay logs contain zero main-target self-sampling and missing-texture warnings after the snapshot fix. The current `195a6ee0` executable was replayed at `--window 1280x720` through frame 15180 with both right and left camera input; Max and NPCs remain visible past the former 4:3 edge, with zero self-sampling/missing-texture warnings. Captures and logs are under `win/build-rel/verify-post195-camera/` and `win/build-rel/verify-post195-camera-left/`. A horizontal ground seam is also visible in the left-angle image at both 4:3 and 16:9, so it is distinct from the old inner 4:3 boundaries and remains unexplained. This verifies the town wide-fill case, but the user's distant bench/cutscene disappearance and movie playback remain open.
