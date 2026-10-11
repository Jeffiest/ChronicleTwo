# dng_hud: reverse-engineering notes

Header: `ps2/include/dng_hud.hpp`. 35 functions in the unit plus `CDamageScore::CDamageScore()`
(inline, emitted in dng_main at 0x1D5D20). All nine classes are owned by this unit. None has a
class counterpart in the first game (it only has a plain `ENEMY_LIFE_GAGE` struct in
`dun/gameloop.cpp`, unrelated in layout).

## Globals
The unit's only plain-named datum is `gekirin_anim` (0x33D210, 0x40 bytes, LOCAL in retail): a
`static` table of 16 `s32` heights (0,3,6,5,4,3,2,1,3,4,3,2,1,2,1,0) indexed by
`CEnemyGekirin::frame`; the y offset of the mark is `-4 * gekirin_anim[frame]`, and the break
animation ends when the next entry is 0. It belongs in the `.cpp`, not the header.
The `"%d"` format string literal is shared (pooled) by both damage-number `SetValue`s.
The class instances themselves are globals of dng_main (`LevelupInfo` 0x38, `DamageScore` 0x90,
`DamageScoreMons` 8 x 0x90, `DamageScore2` 0x28, `LockOnModel` 0xB0, `WarningGage2` 0x20);
`CEnemyLifeGage`/`CPiyori`/`CGiftMark` live inside `CActiveMonster` at 0x1220/0x1270/0x1290 and
one `CEnemyLifeGage` in `CMonsterMan` at 0x10090.

## Sizes (all asserted)
| Class | Size | Evidence |
|---|---|---|
| CLevelupInfo | 0x38 | `LevelupInfo` symbol size; SetLevelUpInfo writes 0x00-0x34 |
| CPiyori | 0x20 | CActiveMonster: Piyori 0x1270, GiftMark 0x1290; s16s at 0x1C/0x1E |
| CGiftMark | 0x14 | s16 `time` at 0x10, pointer alignment; follows Piyori in CActiveMonster |
| CEnemyGekirin | 0x2 | stride 2 in Set/ResetGekirin/Step loops over 0x24+2i |
| CEnemyLifeGage | 0x50 | 0x24 + 16*2 = 0x44, padded to 16 (sceVu0FVECTOR); Piyori follows at +0x50 |
| CDamageScore | 0x90 | `__construct_array(DamageScoreMons, ctor, 0, 0x90, 8)`; `DamageScore` size |
| CDamageScore2 | 0x28 | `DamageScore2` symbol size; last field 0x24 |
| CLockOnModel | 0xB0 | `LockOnModel` symbol size; CObjectFrame is 0x80; pos at 0xA0 |
| CWarningGage2 | 0x20 | `WarningGage2` symbol size; layout at 0x1C |

## Layouts
- **CLevelupInfo**: 0x00-0x1C zeroed by SetLevelUpInfo and never read (8 x s32, kept `unk_`).
  0x20 progress (float, +0.1/frame), 0x24 phase (`LevelupInfoPhase`, 1..4, 0 = off),
  0x28 x = arg1-0x23, 0x2C y = arg2-6, 0x30 weapon_slot = arg3, the weapon slot (0 or 1) whose weapon levelled up (maintex
  `AddExpWeaponParam`); 0x34 = arg4, always 0, never read.
- **CPiyori**: 0x00 target (typed `CCharacter2*`; the retail `Set` signature takes `mgCObject*`; Step calls `CCharacter2::GetEntryObjectPos` on it),
  0x04 star_angle[3] (+12 deg/frame, random start), 0x10 circle_angle (+6 deg/frame),
  0x14 height, 0x18 radius (Set(obj,time) uses `CCharacter2::body_height * 2` and `body_width * 2`), 0x1C s16 time,
  0x1E s16 se_wait (reset to 13 after SE 0x24 plays).
- **CGiftMark**: 0x00 chara, 0x04 height, 0x08 angle (+7.5 deg, wraps at pi), 0x0C active,
  0x10 s16 time (ends after 240).
- **CEnemyGekirin**: s8 state (`GekirinState`: 0 show, 1 break, 2 none), s8 frame.
- **CEnemyLifeGage**: 0x00 pos (sceVu0CopyVector), 0x10 max_hp, 0x14 hp, 0x18 screen
  (CMonsterMan passes 1 for its total gauge), 0x1C view, 0x20 scale, 0x24 gekirin[16].
- **CDamageScore**: 0x00 s32 zeroed by CommonStageClassInit, never read; 0x04-0x0F unused
  padding before the aligned pos; 0x10 pos (Draw writes w = 1.0); 0x20 text[8]; 0x28 bounce[8]
  (float, starts at pi); 0x48 color[3] s16 (ctor memsets 6 bytes 0x80); 0x4E alpha; 0x50 phase;
  0x52 length; 0x54/0x58 zeroed by CommonStageClassInit, never read; 0x5C digit_w (12),
  0x60 digit_h (17), 0x64 digit_u (0), 0x68 digit_v (0xCA) set by CommonStageClassInit;
  0x6C/0x70 never touched; 0x74 sprite_w, 0x78 sprite_h, 0x7C sprite_u, 0x80 sprite_v,
  0x84 sprite flag, 0x88 active.
- **CDamageScore2**: 0x00 chara_no, 0x04 height (= arg*2), 0x08 offset_y, 0x0C alpha, 0x10 value,
  0x14 text[8], 0x1C phase (`DamageScore2Phase`), 0x20 length, 0x24 progress.
- **CLockOnModel** (`: CObjectFrame`): 0x80 scene (InitDungeonMain stores DngMainScene),
  0x84 mes (InitDungeonMain stores MonsterMess), 0x88 angle (+4 deg/frame), 0x8C name
  (= target chara's `+0x1150` pointer + 4; NULL hides the name), 0x90 s32 set by Draw to
  `chara+0x134C + 5000` (if >= 0) when the target's `+0x1156` equals the controlled monster id,
  else -1 -- never read in any unit seen, named `message_no`; 0x94-0x9F padding; 0xA0 pos.
- **CWarningGage2**: 0x00 warning[3], 0x0C time (0..39, draws when > 19), 0x10 rate[3]
  (0.0 selects a different sprite), 0x1C layout (`WarningGageLayout`, written by dng_status
  DrawMain/Monster = 0, DrawRobo = 1).

## CLockOnModel vtable (`__vt__12CLockOnModel`, 0x88)
Inherited CObjectFrame slots unchanged (29 entries, incl. `Draw__12CObjectFrameFv`), then three
new slots: `Draw()`, `Step()`, `Initialize(CScene*)`. `CObjectFrame::Draw` returns `int` while
`CLockOnModel::Draw` returns nothing (no `$v0` set), so MWCC treats it as a new virtual rather
than an override; the header declares `virtual void Draw()` and compiles. Confirm the emitted
vtable once it leaves asm. `Initialize(CScene*)` is a new slot because of its different
parameters. The instance is constructed inline in `__sinit_dng_main_cpp` (vtable chain
mgCObject -> CObject -> CObjectFrame -> CLockOnModel); there is no out-of-line constructor.

## Unresolved
- dng_status `DrawStatusBord` and dng_main `DngMainKey` zero `LockOnModel + 0xAC` (the w of
  `CLockOnModel::pos`) together with `WarningGage2.warning[0..2]`. Probably an out-of-bounds or
  differently-declared write in the original source; not reflected in the header.
- `CLevelupInfo` 0x00-0x1C, 0x34 and `CDamageScore` 0x00, 0x54, 0x58, 0x6C, 0x70 purpose.

`CPiyori::Draw` and `CGiftMark::Draw` construct their `CPreSprite` only after locating the active target; keeping quad temporaries after that native local reproduces the original stack layout.
