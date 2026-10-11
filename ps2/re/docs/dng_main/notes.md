# dng_main notes

Dungeon main-loop mode (`InitDungeonMain` / `LoopDungeonMain` / `FinishDungeonMain` are the
LoopInit/LoopMain/LoopExit entries) plus most of the dungeon's shared globals.

## Owned types

### MoveCheckInfo (0x110)
Only class owned by the unit (`Initialize` = `memset(this, 0, 0x110)` gives the size). First game:
`MoveCheckInfo` in gameutil.hpp, 0xD0; this game prefixes 0x10 bytes and appends 0x40.
Layout from `MoveCheck` / `GetCPolyAttr` (gameutil):
- 0x00 float radius: read by MoveCheck, <= 0 -> 15.0. CActionChara::RunScript writes height*2+4.
- 0x04 skip_ground: foot-poly search only runs when 0.
- 0x08 landed: set when the foot point is above the step end.
- 0x10 CCPoly ground_poly / 0x60 ground_found / 0x70 CCPoly second_poly (same poly copied twice)
  / 0xC0 ground_point (foot point vector). CActionChara+0x962 = ground_poly.foot_sound.
- 0xD0 width_result = CheckWidth result.
- 0xD4 in_water / 0xE0 water_surface: GetCPolyAttr, a raised probe hitting area_kind 7 or 1.
  DngStep checks MainChara+0x9E4 (=0x910+0xD4) and spawns the water ripple effect at +0x9F0.
- 0xF0 crossed_area / 0xF4 signed_distance (mgDistVector, negated when moving down) /
  0x100 crossed_point: GetCPolyAttr, segment hitting area_kind 7 or 1.
- 0x0C, 0x64..0x6F, 0xD8..0xDF, 0xF8..0xFF never touched -> unk.
Embedded in CActionChara at 0x910 and CActiveMonster at 0x1360, EditMoveCharaInfo (editctrl).
CMonsterMan's inline array construction calls `MoveCheckInfo::Initialize` (the out-of-line copy here).

### DNG_STATUS (0x1C, `DngStatus`) -- name ours, no retail type name known
- 0x00 mode (DNG_STATUS_MODE). Values from LoopDungeonMain/RunMainEvent/DngMainKey:
  0 field (DngMainKey + DngStep), 1 menu from field (MenuMainKey), 2 event (RunMainEvent),
  3 event editor (EventEdit), 4 menu from event (EventLoop returned 2), 5 leave (loop returns 1).
  RunMainEvent: EventLoop 3 -> 5, 2 -> 4, 1 -> 0.
- 0x04 dungeon_no: INIT_LOOP_ARG.map_no; EntryEventScript arg, index into map name table,
  CBPot::SetObject2 arg, monster-talk file name (event_func).
- 0x08 eye_view: InitEyeCamera sets 1, ResetEyeView clears.
- 0x0C active_item: slot * 0x6C into the active item list (actionchara, dng_status).
- 0x10 cursor_fade float (init 1.0; dng_status fades it).
- 0x14 status_count: CheckStatusError counts to 0x2C then flashes status ailments.
- 0x18 debug_window: DebugMainDraw draws a debug box when set; Sphida/DBGCMD clear it.
Symbol size 0x1C (bss slot 0x20).

### ACCUME_EFFECT (0x330, `AccumulateEffect`) -- name ours
Pointed to by CActionChara+0x7CC (`accume_effect`, still `void *` in actionchara.hpp).
`_SET_ACCUME_FLAG` (actscript): [0] = frame from ACTION_ACCUME, 0x310 = mode (script value;
1 = start), on start also 0x314=0, 0x318=0, 0x31C=3.0f, 0x320=0, 0x324=0 and zeroes 0x290..0x30F.
CommonStageClassInit clears 0x0, 0x310, 0x320. No code found that steps/draws it by name;
only those fields are named.

### DNG_STATUS_MODE enum -- names ours (see above).

## Global functions
Not local in retail: GetWeaponEffect, InitDungeonMain, CommonStageClassInit (also called by
dng_event LoadDungeonMapFile), FinishDungeonMain (empty), LoopDungeonMain (returns 1 when
DngStatus becomes 5). All others are local -> static in the .cpp, except the retail prototypes now
declared in dng_main.hpp (memoryInit, CommonClassInit, DngMainDraw, DngStep, RunMainEvent, DngMainKey,
IsEventRun, EventScriptSetup, IsRunDeadEvent, ChangeSetUnit, CheckStatusError, InitEyeCamera,
CheckWeaponEnable, DebugMainDraw, DBGCMD_RunScript); the .cpp does not repeat them. ChangeSetUnit's
parameter is `change` (a DNG_CHANGE_UNIT value).
GetWeaponEffect returns `&wep_effect[cnt]` (CWeaponElement, stride 0x7C0, 8 entries, cycles).

`InitDungeonMain` owns a function-local `mgCMemory` for the debug event stack. At
0x001CF490, retail checks a one-byte GP-relative initialization guard and calls
`mgCMemory::Init()` once before `stSetBuffer` and `InitEventEdit`. The stack is
the 0x30-byte BSS object `debug_event_stack_1106` at 0x01EF7380; the guard is
`init_1107` at 0x0037D470, followed by three alignment bytes. MWCC generates
both naturally from the local static declaration, but its generated ordinal
differs from retail's source ordinal. The object postprocessor maps the pair
to the retail symbol names without defining a compiler initializer manually.
The earlier isolated C++ checkpoint of `InitDungeonMain` was 0x20D8 bytes,
short of retail's 0x2110-byte function, and changed later text layout. The
merged source now selects the native body by default; that change does not
establish an exact match. The layout observations below describe the
earlier checkpoint. Fresh integrated verification is
required, including the naturally emitted local-static storage and helpers.

## Globals (types from __sinit, InitDungeonMain, CommonStageClassInit)
Retail names with `__2` in main.symbols are these globals (other units have locals of the same
name): MainBuffer, MainChara, EventCamera, BuffWorkData. viewAngleH/V, WaveTable are local here.
- MainBuffer mgCMemory* (GetMainStack); BuffReadData u_long128* (stAlloc64 200000).
- NowFloorInfoPtr: CSaveDataDungeon::GetFloorInfoPtr result (0x14-byte record, +0x10 s16 incremented
  by CMonsterMan::ThinkHost). Type name unknown -> forward-declared `DNG_FLOOR_SAVE` (ours).
- ActionScriptEnv RUN_SCRIPT_ENV (8 bytes): item_chara = ItemBaseData, texb = 0x68.
- DngSaveData CSaveData* (GetSaveData); DngUserData = DngSaveData+0x1D2A0 (CUserDataManager*);
  DngSaveDataDungeon = DngSaveData+0x1C5B4.
- DngMainScene CScene* (GetMainScene). BattleAreaScene = DngMainScene + 0x2F90 (same as
  menu_GetBattleAreaScene). Its type is unknown -> forward-declared `DNG_BATTLE_AREA` (ours).
  Offsets used here: 0x8 flags (0x2, 0x400 event, 0x800, 0x8000), 0xC, 0x24, 0x44, 0x46, 0x48,
  0x49, 0x4C, 0x50, 0x54, 0x5C, 0x64, 0x78, 0x7C (CTreasureBoxManager*), 0x84, 0x88, 0x8C, 0x90,
  0x98, 0x9C, 0x9E, 0xA0 (texb). CDngFloorManager sits at scene+0x2FA4.
- DngMainMap CMap* (CScene::GetMap). ActiveMonster CMonsterMan* (new 0x100F0).
- DngMess/DngMess2/EventMess/MonsterMess ClsMes* (new 0x2958).
- RedMarkModel CRedMarkModel* (new 0x90). TreasureBoxModel CCharacter2* (new 0x660).
  TreasureBoxMan CTreasureBoxManager* (new 0xAA0: 0x10 header, 24 CTreasureBox of 0x70, tail).
  `__vt__12CTreasureBox` is emitted in this unit (inline ctor used here).
- MainChara: CScene::GetCharacter(0); CActionChara methods called on it -> CActionChara*.
- FxScriptMan CEffectScriptMan* (new 0x1190). BTsuboCol CColPrim*. TornadoModel mgCFrame*.
- SparcModel mgCFrame*[3] (symbol 0xC; three mgLoadMDSFile results copied into each CSparcEffect).
- PullItemMan CPullItemManager by value (list = PullItem, num = 0x48). PullItem CPullItem[72].
- mgCMemory: BuffPaketList[2], BuffPaketData[2], BaseCharacter[6], BuffEventData[4]
  (__construct_array with mgCMemory ctor), the others single.
- DamageScore CDamageScore (ctor inlined: memset +0x48), DamageScoreMons CDamageScore[8],
  DamageScore2 CDamageScore2, LevelupInfo CLevelupInfo, LockOnModel CLockOnModel,
  WarningGage2 CWarningGage2, ColPrimMan CColPrimMan, RocketLauncher CRocketLauncherMan,
  MachineGun CMachineGun, LaserGun CLaserGunMan, VoiceUnit CRoboVoiceSystem.
- MsgTaskMan MessageTaskManager (0x36C), StartupEpisodeTitle CStartupEpisodeTitle (0x18),
  BattleFX BattleEffectMan (0x48), map_effect CMapEffectsManeger (0x14), AutoMapGen CAutoMapGen
  (0x2A0, mgCDrawPrim at +0x50), RandomCircle CRandomCircle (0x6A0, CCharacter2 at +0x40),
  GeoStone CGeoStone (0x670, CCharacter2-derived), MainCamera/EventCamera CCameraControl (0x1F0),
  HealingEffectMan CHealingEffectMan (0x330), MiniEffPrimMan CMiniEffPrimMan (0x940, mgCDrawPrim
  at +0x204), BTsubo CPot (0x80), BTsubo2 CBPot (0xC50, CFragment[?] of 0x60 from +0x40).
- ItemBaseData CCharacter2[19]; LaserGunModel CCharacter2.
- Local (static, in .cpp later): debag_param, viewAngleH/V, init_camera, DebugPause, test_dist,
  wep_effect_cnt, debug_cursor/mons_no/mons_cur/mons_num, nowload (NowLoadingInfo),
  WaveTable (CWaveTable, registered for destruction), SwordLuminous (CSwordLuminous),
  wep_effect (CWeaponElement[8]), backup_pos, cam_table, debug_no.

`debug_no` occupies 0x20 bytes at 0x0033D400: eight `int` slots, with only
the first seven indexed by the debug menu. The final zero belongs to the
array itself, before `at_3734` at 0x0033D420.

`DngMainKey` has the retail instruction layout except near 0x001D417C:
the normal game build loads the two immediate coordinates for `SetNextRef`
in reverse order, while passing the same values. The isolated draft build
with `NONMATCHING` defined loads them in retail order, but that configuration
also compiles the unrelated `InitDungeonMain` draft. Disabling just that
earlier draft restores the reverse load order. An isolated game-build link of
`DngMainKey` differs from retail by ten bytes in `.text`; every other section,
including BSS, matches. Local variables, unsuffixed literals, array elements,
and equivalent constant expressions did not fix the ordering. The native body in
`dng_main.cpp` is the one the normal build links.

## Enums (names ours; values from the retail code)
- DNG_CHARA_SLOT: InitDungeonMain fills scene character slots 0-7 (party, CActionChara via
  `GetCharacter(i)`), 8-23 (`i + 8` over 16 event characters, type 2) and 24 onward (`i + 24` over
  MONSTER_ACTIVE_MAX CActiveMonster entries, type 3). DngStep and DngMainDraw iterate the same ranges.
- DNG_MAIN_ARRAY_SIZE: counts of the file-scope arrays, taken from their declarations and loop bounds.
- DNG_SYSTEM_EVENT: the event numbers IsEventRun writes to `*event_no`, each gated by a save bit:
  SAVE_FLAG_GUIDE_MAGIC_SWORD gates 2540 (magic sword guide), SAVE_FLAG_ROBO_BIKE_EVENT_SEEN gates 2530 (ridepod guide, with
  move_type ACTION_MOVE_ROBO_BIKE), SAVE_FLAG_GUIDE_RANDOM_STONE gates 2500 (random stone), SAVE_FLAG_GUIDE_LEVEL_UP gates 2520 (level-up guide,
  with LevelupInfo.phase != 0). 1500 is raised when no monster is left and battle_clear is 0.
- DNG_FOOT_EFFECT: DngStep's `effect[35]` table maps CheckFootEffect ids to a kind. Kind 0 (dust) runs
  the effect script "足砂煙", kind 1 (grass) runs "足砂煙" then "足芝生", kind 2 (splash) runs "足水パシャ".
  -1 raises nothing.
- DNG_CHANGE_UNIT: ChangeSetUnit's `change` argument. DngMainKey sets `unit` to PARTNER on PAD_L3 and
  RIDE on PAD_R3. The PARTNER branch swaps USER_CHARA_MAX and USER_CHARA_MONICA.
- Matching: the footstep array type (`DNG_FOOT_EFFECT effect[35]`) and the camera rotate constants
  (`PI / 24.0f`, `PI / 32.0f`) matched when applied; no candidate was reverted.

## Unresolved / pending
- afterWire, Sparc_fx, thunder, tornado, chillAfterHit and fireAfterHit are declared in dng_main.hpp at
  the DNG_MAIN_ARRAY_SIZE counts.
- DNG_BATTLE_AREA / DNG_FLOOR_SAVE are forward declarations with our names; the owners
  (scene / savedatadungeon) should define and rename them.
- Single by-value globals of classes whose headers do not exist yet (CMapEffectsManeger,
  MessageTaskManager, CStartupEpisodeTitle, BattleEffectMan, CAutoMapGen, CRandomCircle,
  CGeoStone, CCameraControl, CHealingEffectMan, CMiniEffPrimMan, CPot, CBPot) are declared
  with forward-declared classes; include their headers when they exist.

## Isolated InitDungeonMain checkpoint

The native body of `InitDungeonMain` remains unresolved. Its isolated
section is 0x20D8 bytes, shorter than the retail 0x2110-byte function.
The first differing region is the sequence of global vector copies;
later instruction streams no longer align because of the shorter
section. The unit also contains pre-existing local-data layout and
symbol-resolution discrepancies in isolated object validation, so it
requires the coordinator's integrated toolchain/data checkpoint before
further matching claims. No source changes from this trial are retained.

## Native local-static BSS binding

`InitDungeonMain` declares one local `mgCMemory debug_event_stack` (0x30 bytes),
and `DngMainKey` declares one local `sceVu0FVECTOR chk_pos` (0x10 bytes). The
retail BSS objects `debug_event_stack_1106` (0x01EF7380) and `chk_pos_2870`
(0x01EF7400) correspond to these typed C++ objects, and both storages coexist
before object postprocessing.

MWCC gives a local static a generated numeric suffix. The compiler's current
suffix depends on the source/header parse; its value is not the variable's
identity. The retail split records an independently generated suffix. Matching
these objects by the position of a relocation in retail code also fails when a
native function has a different instruction sequence.

`postprocess_object.py` binds these cases by the translation unit, a unique
source static declaration, the variable's base name, and its exact storage
extent. A matching target is a retail BSS/SBSS object within that unit's retail
BSS/SBSS ranges. Native storage must be a local, whole-section
NOBITS object of the same BSS kind and exact size. Retail storage must have the
same declared size and contain only zeroed storage. Interior symbols,
initialized native objects, and ambiguous declarations, native names, or marker
names are left alone. Simple typed declarations are recognized conservatively;
more complex C++ declaration spellings are not guessed.

Every relocation against the native object or its section aliases is retargeted
to the explicit retail symbol. Both symbols denote the beginning of their
storage, so the compiler's relocation addends stay unchanged. Only the resulting
unreferenced native storage is marked `.dead`, then removed by the existing
`fixup_sections.sh` step. Function instructions are never changed by this pass.

### Verification

A genuine MWCCgap `dng_main` object was compiled through Satan's Fiddle and
processed both with and without this binding. The new final object differs only
by removal of two native BSS copies and eight relocation targets: six references
to the event stack in `InitDungeonMain`, and two references to the position
vector in `DngMainKey`. Every remaining allocated section retains its bytes and
extent, and the total relocation count stays 3647. The final BSS piece count is
62, matching retail, instead of 64.

The raw compiler object contains additional local statics which the existing
instruction-position binder already resolves. Binding their names first
produces the same final storage and references. The already processed object
copy and raw object both pass `scripts/build/test_static_bss.py` checks; pure
selection tests also cover suffix changes, missing declarations, different
extents, and ambiguous native/retail names without creating compiler objects.

Whole-unit checking still reports the existing `InitDungeonMain` length and code
mismatch, BSS padding differences, and an unmatched SBSS piece. Correcting BSS
piece recognition allows the checker to inspect more data and resolve formerly
unknown targets; the lower total problem count is not a claim that those game
functions became matched.

## DngMainKey floating-point calibration

`./decompile.sh DngMainKey__Fv` confirms the debug weapon-element call
uses 10.0, and the tornado call uses size 20.0 alongside `fRand(255.0f)`.
The argument-consumer default changed the optimized local 20's evaluation
byte, materializing it after the nested random call; this altered the saved
float register and shifted the first code region. The final source is
unchanged. Binary32 evaluate-first policies for `0x41200000` (10) and
`0x41A00000` (20), scoped to dng_main.cpp / DngMainKey__Fv, restore the
complete 0x1E4C-byte function's instruction shape. Both initializer and
consumer applications were verified by Satan's Fiddle diagnostics.

Canonical wrapper plus section fixup gives zero nonrelocated instruction
differences. Comparison with the assembled retail reference also gives
zero differences across all 440 shared relocation tuples: function-relative
offset, relocation type, normalized symbol identity and encoded addend.
The native object additionally carries 128 GP-relative relocations where
the reference encodes final GP offsets directly. Its known unresolved
global data names remain a dng_main layout limitation; this calibration
does not claim that those addresses or the complete unit are exact.

# `MoveCheckInfo::Initialize`

The 0x110-byte movement-query record is cleared with `memset`. Its inline
definition in `gameutil.hpp` produces the retail tail call.
