# dng_effect: reverse-engineering notes

Header: `ps2/include/dng_effect.hpp`. Every class size below is asserted there; offsets were also
checked with temporary `offsetof` asserts against MWCC.

## File-local (static) functions -- belong in the .cpp, not the header
- `trans_effect_rate(int)` -> `float`: `min(strength / 255.0f, 1.0f)`.
- `trans_float_to_sceVector(float *vec, float *xyz, int dir)` -> void: dir 0 copies xyz into vec with w=1;
  dir!=0 copies vec.xyz into xyz.
- `LocalTransWorldPrimPos(int (*out)[4], float *pos, float w, float h, float angle)` -> int: projects
  `pos` with `mgRenderInfo` (offsets 0x10 matrix, 0x50, 0x64 scale) and writes four rotated corner
  vertices; returns 0 when behind the camera (w < 1).

## Data (all local in retail; no externs in the header)
- `chill_tex_rect_910` (.data, 0x48): function-local static of CChillAfterHit::Draw, 6 rows of
  `{int u, int v, int size}` (indexed by piece.rect, stride 0xC).
- `gb_tbl_1052` (.data, 0xC): function-local static of CFireAfterHit::Draw, 3 ints (green/blue of the
  three trail glows). `at_1051` (.bss, 0xC) is its guard/copy.
- `thn_tbl` (.data, 0x60, static): 6 rows x 0x10 of float sprite sizes (x at +0, y at +4) for CThunder::Draw.
- `thn_uv` (.data, 0x60, static): 6 rows of `{u, v, du, dv}` floats for CThunder::Draw.
- `at_1214__2`/`at_1215__2`/`at_1216__3`: 0x10 vector literals in CThunder::Draw.
- `at_3214` (.data, 0x20): 4 pairs of ints (texture coords) in CWeaponElement::Draw_Thunder.
- `at_2882` "effect02" (texture name used by
  every CWeaponElement::Draw_*), `at_1981` (0x1C, CDeadEffect::Draw jump table).

## Global instances (owned by dng_main / dng_event / event_func, NOT declared here)
- `BattleFX` BattleEffectMan (0x48), `HealingEffectMan` (0x330), `MiniEffPrimMan` (0x940),
  `SwordLuminous` (0x18, BSS 0x20), `Sparc_fx` CSparcEffect[6] (0x420), `thunder` CThunder[6] (0x5280),
  `tornado` CTornado[6] (0x1500), `chillAfterHit` CChillAfterHit[6] (0x2DC0), `fireAfterHit`
  CFireAfterHit[6] (0x3780), `wep_effect` CWeaponElement[8] (0x3E00), `afterWire` CAfterWire[16]
  (0x1200), `map_effect` CMapEffectsManeger (0x14, dng_event), `HitEffect` CHitEffectImage[5] (0x1E0,
  event_func), `SwordEffect` CSWordAfterImage* (event_func, `new` of 0x60).
- Sizes come from these symbol sizes plus loop strides in `InitDungeonMain` (0xB0/0xDC0/0x380/0x7A0/0x940),
  `CommonStageClassInit` (0x7C0 x 8), `DngMainDraw`/`__sinit_dng_main_cpp` (`__construct_array(afterWire,
  __ct__10CAfterWire, 0x120, 16)`), AllocEffect `__construct_new_array` element sizes (0x60, 0x40, 0x80,
  0x40, 0x660) and `_CREATE_SWORD_EFFECT` (`__nw(0x60)`).

## Constructors emitted outside dng_effect
`__ct__13CFireAfterHitFv`, `__ct__14CChillAfterHitFv` (both just call Initialize), `__ct__8CThunderFv`
(constructs mgCFrame at 0 and mgCFrameAttr at 0x110; no vtable store, so CThunder is NOT derived from
mgCFrame -- `frame` is a member), `__ct__10CAfterWireFv` (`mode = 0`) are all in dng_main, i.e. inline
constructors emitted where used. `__ct__15CHitEffectImageFv` and `__ct__10CPowerLineFv` sit in dng_effect
right after AllocEffect (first use), also consistent with inline definitions. `__ct__11CCharacter2Fv` in
this unit is CCharacter2's inline ctor (owned by character). The header only declares these ctors;
whoever decompiles them should give them inline bodies.

## Layout evidence per class
- **BattleEffectMan (0x48)**: from AllocEffect/Step/Draw and callers (`HitEffectSet`, `GuardEffectSet`,
  `CMachineGun::Step`, `_SET_DEAD_OFF`) which inline a "take next from ring" helper: `ptr==0 ? 0 :
  &ptr[next++]; if (next >= num) next = 0`. Groups: 0x0 hit_prim / 0x4 hit / 0x8 num / 0xC next;
  0x10 flush / 0x14 / 0x18; 0x1C power_prim / 0x20 / 0x24 / 0x28; 0x2C dead_prim / 0x30 / 0x34 / 0x38;
  0x3C chara (CCharacter2[n], 0x660) / 0x40 slots (0x1C each, +0x18 = chara, +4/+8 zeroed) / 0x44 num.
  AllocEffect kind (BATTLE_EFFECT_KIND): 0 hit (prims 0xA0 qwords each = 0x20 prims, spark_max 0x20),
  1 flush, 2 power (0x64 qwords = 0x14 prims), 3 dead (0xF0 qwords = 0x30 prims, memset 0x37, radius/height
  10.0, size 1.0), 4 chara. Returns 1/0. `CommonClassInit` only allocates kinds 0..3 (4,4,8,4).
  The pool sizes are the defines `HIT_EFFECT_SPARK_MAX` (32), `POWER_LINE_PRIM_MAX` (20) and `DEAD_EFFECT_PRIM_MAX` (48); the sparc pattern and mini-sprite tint values are the enums `SPARC_EFFECT_PATTERN` and `MINI_EFF_PRIM_COLOR`.
  The prim blocks are `new u_long128[]`-style arrays (no ctor); `BattleEffectPrim` and `BattleEffectChara`
  are neutral names -- no retail name known.
- **BattleEffectPrim (0x50)**: shared by hit/power/dead blocks. 0x0 kind (dead only: 0 cloud, 1 glitter --
  enum not made, names unknown), 0x10 pos, 0x20 velocity/direction, 0x30 size (set but unread for hit and
  power), 0x34 speed (dead sets 0.2, unread), 0x38 rate (hit: speed lost per step; dead: peak alpha),
  0x3C life, 0x40 life_max (dead), 0x44 alpha / 0x48 alpha_step (hit). Ghidra types the dead-effect
  block as int*; all fields are floats except kind/life/life_max.
- **CHitEffectImage (0x60)**: SethitEffect(pos, dir, spread, distance, slow, gravity, life, num): 0x0 origin,
  0x10 dir (normalized), 0x20 sparks, 0x24 num (clamped to 0x2C spark_max), 0x28 live, 0x30 spread,
  0x34 slow, 0x38 distance, 0x3C gravity (subtracted from spark dir.y), 0x40 sprite size (5.0), 0x44 kind
  (Draw: 0 DrawBord, 1 DrawSpark(3), 2 DrawSpark(9); callers set 0x44 = 0), 0x48..0x4F unused,
  0x50 mgRect<int> tex_rect (ctor `Set(0,0,0,0)`; SethitEffect resets to {0,0,0x1F,0x1F}; callers copy one
  in). tex_rect is used as (u, v, w, h).
- **CFlushEffect (0x40)**: from Step/Draw and HitEffectSet's init: 0x0 follow frame, 0x10 pos, 0x20 fade
  speed (20.0), 0x24 s16 alpha (0xA0), 0x28 size (10.0), 0x2C grow (2.0), 0x30 s16 active, 0x32/0x34/0x36
  s16 u, v, size (0x40, 0xC0, 0x40). Uses `TEX_SystemEffect2`.
- **CPowerLine (0x80)**: ctor sets tex_rect 0 and color {0x80 x4}. 0x0 source frame (vtable +0x18 =
  GetPosition into 0x10), 0x20 radius, 0x24 prim_size, 0x28 rise, 0x2C..0x33 unseen, 0x34 duration,
  0x38 elapsed, 0x3C prim_life, 0x40 height, 0x44..0x4F unseen, 0x50 tex_rect, 0x60 color[4], 0x70 prims,
  0x74 max (0x14), 0x78 live, 0x7C next. No code outside BattleEffectMan reads `BattleFX.power`
  (0x20), so nothing starts a power line in retail.
- **CDeadEffect (0x40)**: SetDeadEffect(pos, radius->0x14, height->0x10, size->0x18, duration->0x1C,
  elapsed 0x20=0). 0x24 prims, 0x28 max (0x30), 0x2C live, 0x30 next. On the first step 10 extra kind-0
  flecks are made; then one kind 0 and one kind 1 per step. Draw colours by `i % 7` (switch = `at_1981`).
- **CChillAfterHit (0x7A0)**: Initialize zeroes 0/4/8, memsets 0x780 at 0x20, zero-vectors 0x10. Pieces
  stride 0x50 x 24 (piece_num capped at 24). piece: 0x0 pos, 0x10 velocity, 0x20 size, 0x24 damping,
  0x28 angle, 0x2C spin, 0x30 s8 rect (0..4), 0x31 s8 move_time, 0x32 s8 trail_num (starts -1, counts up
  to 1), 0x33 s8 fade_speed, 0x34 s16 alpha, 0x38/0x44 last two positions (float[3] each). Texture
  `TEX_ExFx_ICE`. Strength < 32 starts nothing.
- **CFireAfterHit (0x940)**: Initialize zeroes 0..0xC, memsets 0x2A0 at 0x10 and 0x690 at 0x2B0.
  0x8 counts steps. flame stride 0x30 x 14 (flame_num = rate*10+4, max 14): 0x0 pos, 0x10 velocity[3],
  0x1C size (sceVu0 vector ops on 0x10 also touch it), 0x20 s16 alpha, 0x22 s16 age, 0x24 s16 fade_speed,
  0x26 s8 trail_head (0..5), 0x27 s8 delay. trail [14][6] stride 0x14: pos[3], size, s16 alpha.
  Texture `TEX_ExFx_FIRE`.
- **CTornado (0x380)**: no ctor. 0x0 model (set from `TornadoModel` by InitDungeonMain; vtable +0x10
  SetPosition, +0x20 SetRotation(x,y,z), +0x2C SetScale(x,y,z)), 0x4 live count, 0x8 active, 0xC rate,
  0x10 center, 18 pieces stride 0x30: pos, 0x10 scale, 0x14 angle, 0x18 alpha, 0x1C rise, 0x20 s8 life.
- **CThunder (0xDC0)**: 0x0 mgCFrame (0x110), 0x110 mgCFrameAttr (0x90); Initialize stores `&attr` into
  frame.attr (+0xF4). 0x1A0..0x1AF never touched (kept `unk_1a0`). Sparks stride 0x40 x 48 at 0x1B0:
  pos, velocity (w=0), 0x20 angle, 0x24 spin, 0x28 life (float), 0x2C scale, 0x30 s8 frame (0..5).
  0xDB0 s8 active, 0xDB1 s8 live count (rate*32+8), 0xDB4 rate. SetPos calls frame vtable +0x10
  (SetPosition); Draw calls vtable +0x48 (SetVisual) with a stack mgC3DSprite, then mgDrawDirect.
  The calls go through the vtable although `frame` is a member. SetPos's clearing loop writes
  `spark[0].life` (0x1D8) 48 times rather than each spark's life -- a retail bug to reproduce.
- **CSparcEffect (0xB0)**: 0x0 model[3] (copied from `SparcModel` 0xC), 0x10 pos, 0x20..0x9F never touched,
  0xA0 alpha_max, 0xA4 alpha, 0xA8 s8 pattern, 0xA9 s8 state, 0xAA s8 colour. Nothing outside the class
  writes pos/alpha/state (no SetPos exists), so the effect is effectively unused.
- **CMiniEffPrim (0x20)** / **CMiniEffPrimMan (0x940)**: 64 prims stride 0x20, 0x800 active count,
  0x810 CPreSprite (0x130; sinit constructs mgCDrawPrim at +0x810, Draw uses +0x810). With this tree's
  mgCDrawPrim (8-aligned) the member would land at 0x808, so `unk_804[0xC]` pads it to 0x810; if
  mgCDrawPrim later becomes 16-aligned the padding can go. State 2 = rising (Step: pos.y -= 0.5, alpha -= 1/16;
  returns 1 on the step it frees). Colour 0 green (0x80,0xB4,0x80), 1 gold (0xFA,0xDC,0x40).
- **CPalletAnime (0xE)**: SetAnim(r, g, b, pulse_num, duration, repeats) writes 0,2,4,6,0xA,0xC and zeroes
  0x8. CreatPallet: period = duration / pulse_num, factor = sin((elapsed % period) / period * pi); out =
  base + factor * (colour - base), out.w = 128. Step: repeats -1 loops, 0 stops. Embedded in CActionChara
  (actionchara.hpp still names one copy `unk_67c`).
- **CHealingEffectMan (0x330)**: 0x0 s16 active, 16 lights stride 0x30 at 0x10 (0x10 radius, 0x14 angle,
  0x18 bob height, 0x1C bob phase, 0x20 bob speed, 0x24 spin), 0x310 brightness, 0x314 s16 mode,
  0x320 center. Modes from Step/Draw and automap callers: 1 (CHealingPoint::CheckHealingTime) flare and
  fade to 3 (off); 2 (CHealingPoint::Step / CAutoMapGen::Step) fade in to 0.
- **CSwordLuminous (0x18)**: s8 mode (lb), 0x4 tip frame, 0x8 root frame (both cleared by
  CommonStageClassInit; nothing else sets them), 0xC unseen, 0x10 fade, 0x14 pulse. Draw requires mode,
  both frames non-null.
- **CSWordAfterImage (0x60)**: Initialize(memory, point_max, division) allocates 0x0/0x4 (point_max vec4),
  0xC/0x10 (point_max*(division+2) vec4), 0x8 (point_max floats), 0x14 (point_max*(division+2) floats);
  colours 0x20..0x3C ({0x60,0x40,0x30,0xB4}, {0x40,0x30,0x20,0x60}; `_CREATE_SWORD_EFFECT` first sets all
  to 0x80); 0x40 division, 0x44 smooth count, 0x48 point_max, 0x4C point count, 0x50 write index (fills
  downward), 0x54 head, 0x58 active. 0x18..0x1F and 0x5C..0x5F unseen.
- **CAfterWire (0x120)**: 0x0 mode, 16 points at 0x10, 0x110 smooth count (CreatSmoothPass result),
  0x112 point count, 0x114 oldest, 0x116 write, 0x118 newest. DrawWire takes a work buffer from
  DngMainDraw's stack. StepWire is empty.
- **CMapEffect_Sprite (0x50)** / **CMapEffectsManeger (0x14)**: Init_LightBoll allocates `new
  CMapEffect_Sprite[n]` (no ctor, stride 0x50) and zeroes life (0x38). Manager: 0x0 spawn wait, 0x4 live
  count, 0x8 n, 0xC array, 0x10 type. Type is set by `LoadDungeonMapFile` from the map name
  ("d01f01" 0, "d02f01" 1, "d03f01".."d03f03" 2, else -1) -- MAP_EFFECT_TYPE names derive from that.
  Sprite: pos, 0x10 target, 0x20 initial direction, 0x30 bob angle, 0x34 bob height, 0x38 life,
  0x3C life max, 0x40 speed, 0x44 type.
- **CWeaponElement (0x7C0)**: same layout as the first game's `CWeaponElement` (weaponelement.hpp);
  every offset checked here (0x10 fire_pos in Init_Fire, 0x20/0x220/0x420/0x4A0/0x520 arrays, 0x5A0..0x5B0
  scalars, 0x5B4/0x634 spin arrays, 0x6B4..0x6B8, 0x6BA fading, 0x6FA frame (stores row*0x30, a texture
  V), 0x73A frame_timer, 0x73C..0x7BC bolts). Differences from the first game: no Holy element (Set/Step/
  Draw dispatch 2 thunder, 0 fire, 3 wind, anything else cold), Initialize sets every size and alpha to 1.0
  and clears `on`, `on` is cleared/set as s16 (0x5AC), the texture is fetched by name "effect02" from
  mgTexManager. Set stores `power = (power_arg + 1) * 0.01`.

## Non-member functions
- `CreatSmoothPass(out, ring, n, division, start, ring_size)`: Catmull-Rom (matrix constants 1.5/-0.5/...
  built on the stack) through ring points from `start`, wrapping by ring_size; writes `division-1` points
  per span with w=1; returns count, 0 if n < 3. Used by CAfterWire, CSWordAfterImage, dng_object, menudraw.
- `unitRotation(frame, target, divide)` -> float: reads rotation via vtable +0x24 (GetRotation), steps the
  Y angle toward target by pi/(2*divide) or pi/divide, wraps to (-pi, pi).
- `iRand(n)` = `(int)(n * rand() / 2147483647.0f)`, `fRand(f)` = `f * rand() / 2147483647.0f`
  (Ghidra shows iRand as void; it returns the converted int).

## Unresolved
- Retail names of the helper structs (BattleEffectPrim, BattleEffectChara, CHILL_AFTER_HIT_PIECE,
  FIRE_AFTER_HIT_FLAME/TRAIL, TORNADO_PIECE, THUNDER_SPARK, HEALING_LIGHT) are unknown; the names are neutral.
- BattleEffectChara fields other than `chara`; CPowerLine 0x2C..0x33 and 0x44..0x4F; CSparcEffect 0x20..0x9F;
  CThunder 0x1A0..0x1AF; CSwordLuminous 0xC; CSWordAfterImage 0x18..0x1F, 0x5C.
- Whether CThunder's virtual calls on its own `frame` member come from code like `((mgCFrame *)this)->...`
  or a pointer; needs matching work.

## Element and map ball implementations
`CWeaponElement::Init_Cold`, `Init_Wind`, and `Init_Fire` reset the size and blend arrays, then seed sparks from the charged power and spread. Wind also normalizes and scales each spark velocity and sets spin. Thunder seeds velocities, offsets and bolt endpoints separately; the bolt count caps at 16. `CMapEffect_Sprite::Draw` fades floor balls by remaining life, projects a billboard, and emits two textured triangles. These five functions now have active C++ definitions with no assembly gaps in this unit. Earlier drafts had instruction differences; the current object needs an integrated comparison.

## Native draw constructors

`CChillAfterHit`, `CSparcEffect`, `CSwordLuminous`, `CSWordAfterImage`, `CFlushEffect`, `CPowerLine`, and the cold, wind and fire weapon-element draws construct their sprite or frame-attribute locals through native C++ declarations. The local must be declared at the original constructor execution point, and later temporary arrays must follow it in declaration order to retain their stack slots. For `CSparcEffect`, the `sx` and `sz` values also follow the attribute in the active branch. Placement-array new (`new (memory->Alloc(...)) T[n]`) matches for the plain-data pools `BattleEffectChara`, `BattleEffectPrim` (hit, power and dead prims). It changes code generation for `CMapEffectsManeger::Init_LightBoll` and for the `CFlushEffect` and `CDeadEffect` pools, which have no user-declared constructor; those keep the runtime array helper declared in `mw_runtime.h`.

## Division-check pragma

The unit-level `divbyzerocheck` pragma was redundant with the global MWCC flag; removing it left the full compiled object identical in objdiff.

## CDeadEffect::Draw native match

`Draw__11CDeadEffectFv` (0x001C4EC0, 0x480-byte manifest extent)
draws each live fleck as a projected, alpha-faded textured quad. The sprite is
constructed after the inactive-effect guard, before the projection buffers.
Expired flecks advance the cursor and continue; independent kind-0 and kind-1
tests retain the texture coordinates across other kinds. The glitter glyph is
selected by `life % 3`. The seven `i % 7` palette entries are red, green, blue,
cyan, yellow, magenta and white, as the `at_1981` jump table and its targets show.

The texture load is GP-relative -0x71E8: with retail `_gp` 0x003846F0 this is
0x0037D508, `TEX_SystemEffect1` (effect00). The former draft's effect2 name was
a resolved-relocation error even after its masked instructions matched.

Native integer casts for alpha conversion reproduce the conversion helpers and
honor the existing GPR helper mask 0x30/FPR mask zero. Explicit `fptosi` calls
instead allow different scratch registers in the mixed-corner copies. No new
profile selector is required. Native switch generation supplies the jump labels
and table; the existing data fallback is preserved by the wrapper.

The final canonical wrapper plus section fixup check passes the complete unit:
0xAC54 allocated bytes and 994 resolved relocations. The function is promoted
manually. Its baseline draft differed in 284/288 words and had 0x464 bytes
against retail's 0x480; final instructions and resolved relocations have zero
differences. The m2c parser needs a recognized `jtbl_` name and the seven table
entries to reconstruct this switch; a private copy used for analysis changes
only that label spelling, leaving generated retail assembly untouched.
