# Remaining rendering and gameplay subsystem acceptance

Snapshot: 2026-10-11. This is an ownership and evidence ledger, not a claim that
the phase gates pass. The active PC source set is `PORT_SOURCES` after
`port/cmake/NativeOwnership.cmake` removes `CT_NATIVE_SUPERSEDED_SOURCES`;
file presence alone does not mean a unit is linked. The current test target
registers `occlusion_test.cpp`, `platform_config_test.cpp`, and `smoke_test.cpp`
plus Kazarov's shared cases. Other `*_standalone.cpp` fixtures are dedicated
standalone checks, not GTest coverage in that target.

| Area | Active ownership | Existing component evidence | Missing acceptance |
|---|---|---|---|
| Shadows | Native linked `port/src/mg_shadow.cpp`; shared renderer depth target. | `PlatformConfig.ShadowDistance`; no shadow draw contract case. | Compare shadow volume/composite against the phase 4.5 spec and a retail/recomp scene with additive and subtractive casters. |
| Billboards | `port/src/mg_sprite.cpp` is the native candidate; renderer shader path. | No billboard component test. | Spec vectors for camera-facing and axis-constrained billboards, then image comparison in a scene. |
| Water | `port/src/water.cpp` is explicitly excluded as superseded; its comment says retained retail implementation owns the unit. | No water-specific active test. | Establish which linked native/retained path supplies the phase 4.7 behavior; test refraction, depth and draw ordering against spec plus scene capture. |
| Texture animation | Native linked `port/src/mg_tanime.cpp`. | No animation component test. | Advance known frame/tick sequences and verify texture/page selection and wrap behavior, then exercise an animated in-game material. |
| Photo and depth | Photo-name/game flow remains retail; host screenshot capture is implemented in `port/src/menudraw.cpp` and graphics capture, while the shared renderer implements depth. Photo-name storage has dedicated `photo_name_storage_standalone.cpp`; `occlusion_test.cpp` covers frame culling, not the depth buffer. | PAL 294-name lookup and allocation fixture is recorded in `PORT_STATE.md`; `color_capture_standalone.cpp` covers color readback. | Active photo capture/readback round trip, photo storage lifespan, and reverse-Z occlusion with overlapping geometry; verify photo output in game. |
| Georama | Primarily retained retail game logic; drawing depends on the native rendering/material path. | No georama component case. | User-controlled edit/place/save/reload acceptance with object placement, previews, and a representative scene capture. |
| Fishing | Retained retail game logic; asynchronous load/thread integration is host-sensitive. | `Bits64Placement.FishingFitsItsRun` covers arena layout only; shared clock/input tests are not fishing acceptance. | Exercise entry, bait/rod selection, cast, bite/reel result and exit with controlled input; prove load completion and no thread/tick stall. |
| Ridepod | Retained retail control/game logic plus native model/render path. | No ridepod component case. | Enter/exit, movement, weapon/action, damage/energy and floor transition in a user-controlled dungeon run. |
| Collision | Retail `collision.cpp` remains; native transforms in `port/src/kernel.cpp`, with `collision_asm.cpp` excluded. `ClipBoxXZ` remains retail `editcoll.cpp`; pickup remains retail per ownership comments. | `Bits64Placement.CollisionFitsItsRun` checks allocation fit only. | Contact/slide/pickup and XZ overlap boundary fixtures from PAL behavior; then collision acceptance during movement and attack in a dungeon. |
| Equipment | Retained retail inventory/equipment logic; item model loading uses host model storage and native renderer. | Menu model memory and loader fixtures exist, but they do not establish all equipment behavior. | Equip/unequip each representative category, stats/model update, save/reload, and menu-to-game transition; verify the shared Items model-storage repair through user-controlled gameplay. |

## Native math contracts added

`port/src/tests/native_math_contract_test.cpp` adds three GTests for active
math code: `NativeMathContract.BoxClipTreatsTouchingFacesAsOverlap` checks
closed-box contact and separation; `NativeMathContract.Vu0NormalizeWritesSpatialUnitVectorAndZeroW`
checks the SDK vector normalize contract for XYZ and the unused output lane;
`NativeMathContract.Vu0NormalizeZeroVectorRemainsFiniteZero` covers the
degenerate resting-vector case used by cloth simulation. The first uses
`mgClipBox` in the native `mg_math.cpp`; the latter two exercise the shared
active `libvu0` implementation. These test expected API behavior, not rendering
implementation details.

The phase 4 plan's end-to-end gate (300 headless frames and named visual
subsystems) remains unproven by these component tests. Phase 7.4's TITLE, EDIT,
DUNGEON, fishing and ridepod sweep also remains open; test-file presence and
retained retail ownership are not acceptance evidence for the PC path.
