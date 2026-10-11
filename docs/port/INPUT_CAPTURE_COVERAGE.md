# Input, capture and menu compatibility coverage

Run `cmake --build E:/Chronicle-Two/win/build-rel --target ct_compatibility_tests`.

| Contract | Evidence | Limit |
|---|---|---|
| Xbox Y is Triangle; press/hold/release and reconnect edges | `ct_input_lifecycle_test` exercises host mapping, real pad update/control and language bindings | No controller gameplay is automated |
| Menu navigation state and stick thresholds | Same fixture, including Escape and Japanese/English confirm/cancel | Does not prove every menu's navigation logic |
| Native synchronous color capture | `ct_color_capture_test`: real Vulkan, resize, logical rectangle, explicit texture, orientation, 16/24/32-bit packing and GS alpha | Requires completed recording and a valid descriptor-sized destination |
| Menu background capture | Actual `MenuCapture` fixture: completed frame, half-size averaging, full padded reservation, zero padding, canaries and recording reopened; user reports menu opens | Items model load remains broken |
| Common menu file list | `ct_menu_loader_test`: two native-width filename pointers, byte content, direct/BG callbacks, empty/invalid/missing files and real allocator reserved tail | File callbacks are fixtures; asset decoding is separate |
| Options button selection | `ct_menu_option_test`: actual generated method bodies, minimap 0/1/2, other selections, anger-counter condition, language-specific rows and inactive form | Does not validate malformed saved configuration or all Options input transitions |
| Arena ownership/failure reporting | Real allocator lifecycle fixtures, high-water/name/rebind/reset checks and mandatory failure diagnostics | Bounded 128-owner sidecar; direct field resets observed at the next allocator operation; no guard pages or complete lifetime registry |
| Villager output arrays | Actual method fixture covers primary/fallback paths, two intact pointers, IDs, day/night/progress and canaries | Preserves caller-owned output capacity contract |
| Photo-name storage | Derived host record-width budget, persistent backing and required-failure fixtures; production local PAL loader resolves all 294 names | Active photo capture and aligned input-buffer capacity are separate contracts |

`MenuCommonReadData` now indexes `char **` natively and reserves space before file writes. The checked generated `menuop.cpp` rewrite uses `loop_row[i]`, keeping the private form guard and retail choice behavior. All seven detected byte-stride pointer-array sites have reviewed repairs; the normal build separately rejects this pattern in generated/native game code. This narrow gate does not prove absence of all pointer hazards. Xbox A is the correct PAL English attack input and its component press/hold/release assertions pass; the user's battle attack failure remains unproven at runtime.

| Shared color caller | Current disposition |
|---|---|
| `MenuCapture` in `menudraw.cpp` | Native caller handles the completed-frame boundary and output storage |
| `CPhoto` in `photo.cpp` | Recording-time synchronous readback remains unsupported; explicit failure |
| Crossfade in `effectlist.cpp` | Recording-time lifecycle needs adaptation |
| Sepia and monochrome effects in `event_func.cpp` | Recording-time lifecycle needs adaptation |
| Depth picking/readback | Separate native operation, still unresolved |

No photo/effect caller is certified by menu success. `mgStoreImage` reports unsupported timing or formats explicitly. The legacy API has no destination capacity argument, so caller allocation contracts remain necessary.
