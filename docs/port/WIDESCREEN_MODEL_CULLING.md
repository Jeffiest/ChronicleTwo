# Wide-fill character culling

## Symptom

The user reports that distant characters move with the camera and disappear near the vertical edges of the original 4:3 view, while the wider scene remains visible. This is consistent with a mismatch between the renderer's fill viewport and the game's projected character bounds.

## Change

The renderer uses `AspectMode::Fill` for `video.aspect = "auto"`. `gfx::VisibleLogicalRect(gfx::kMainTarget)` reports the logical span actually shown. `mgRENDER_INFO::SetRenderInfo` now scales the PS2 screen width and height by that visible span before building the view projection, guard area and screen box. Letterboxed 4:3 remains at the original extents.

`mgCFrame::GetDrawRect` previously rejected projected bounds against `mgScreenWidth` and `mgScreenHeight` even though the renderer could show beyond those limits. It now tests against the matching `mgRENDER_INFO::screen_box_min/max`; it still offsets surviving rectangles by the original PS2 screen dimensions so downstream GS-to-logical scaling is unchanged.

The shared extent helper rounds expanded spans up to an even number of GS pixels and rejects invalid inputs. It does not change `aspect_y`, native screen dimensions or scene data.

## Regression coverage

- A 512x448 PAL frame stays 512x448 when the visible logical span is 640x480.
- A 16:9 fill span expands that frame to 684x448.
- A tall fill span expands height to 512x624.
- A projected actor at x=300, outside the original ±256 horizontal cull, remains visible inside the expanded ±342 bound.

Windows `darkchronicle` and `darkchronicle_tests` build successfully. CTest reports 367 passed, five prerequisite skips and zero failures out of 372 registered cases.

## Runtime status

The corrected executable initializes Vulkan, prepares 840 pipelines and indexes all 8,751 game data files. It was opened in a direct PowerShell session at 1280x720. The native-window computer-use inventory is unavailable in this session, so the user still needs to confirm the distant circus characters stay attached to their world positions and remain visible across the former 4:3 boundaries. The earlier framebuffer-name experiment was reverted after the user reported that it did not fix model visibility.
