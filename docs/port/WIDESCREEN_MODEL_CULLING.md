# Wide-fill character culling

## Symptom

The user reports that distant characters move with the camera and disappear near the vertical edges of the original 4:3 view, while the wider scene remains visible. This is consistent with a mismatch between the renderer's fill viewport and the game's projected character bounds.

## Change

The renderer uses `AspectMode::Fill` for `video.aspect = "auto"`. `gfx::VisibleLogicalRect(gfx::kMainTarget)` reports the logical span actually shown. `mgRENDER_INFO::SetRenderInfo` now scales the PS2 screen width and height by that visible span before building the view projection, guard area and screen box. Letterboxed 4:3 remains at the original extents.

`mgCFrame::GetDrawRect` previously rejected projected bounds against `mgScreenWidth` and `mgScreenHeight` even though the renderer could show beyond those limits. It now tests against the matching `mgRENDER_INFO::screen_box_min/max`; it still offsets surviving rectangles by the original PS2 screen dimensions so downstream GS-to-logical scaling is unchanged.

The shared extent helper rounds expanded spans up to an even number of GS pixels and rejects invalid inputs. It does not change `aspect_y`, native screen dimensions or scene data.

## Off-screen framebuffer targets

The wider camera and culling bounds can still be clipped when a scene pass is redirected to a named render target. `mgSetPkFrameBuffer` now promotes a manager-backed, no-image framebuffer target to the renderer's frame-target shape, which retains side pixels in Fill mode. Ordinary texture lookups for `mgGetFrameBuffer` copies preserve the shape already registered under the same target name, so the copy cannot recreate that target as a 4:3 surface. Textures with their own image data are not promoted.

`MgTexture.FrameBufferPromotionPreservesWideFillSides` first resolves a 512x480 target as an ordinary texture, promotes it during framebuffer binding at 1280x720, then resolves a distinct same-name frame-copy record. It checks that the copy resolves to the same target handle and keeps the expanded visible rectangle.

## Regression coverage

- A 512x448 PAL frame stays 512x448 when the visible logical span is 640x480.
- A 16:9 fill span expands that frame to 684x448.
- A tall fill span expands height to 512x624.
- A projected actor at x=300, outside the original ±256 horizontal cull, remains visible inside the expanded ±342 bound.

Windows `darkchronicle` and `darkchronicle_tests` build successfully. The focused framebuffer-promotion test passes. CTest reports 356 passed, 17 skips and zero failures out of 373 registered cases.

## Runtime status

The game executable was rebuilt, but the user then directed that no more computer-use control be attempted. This source-level regression verifies target shape and name reuse; it does not establish that characters now render correctly in the circus scene. Visual acceptance remains pending. The earlier framebuffer-name experiment was reverted after the user reported that it did not fix model visibility.
