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

The code-level tests establish that rendering and culling use the active target's visible extent. They do not confirm that the circus characters now render correctly in the user's scene. Visual acceptance is pending; this session is limited to source, build and headless-test work.
