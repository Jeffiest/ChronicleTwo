# savedata notes

Header: `ps2/include/savedata.hpp`. Includes `editdata.hpp` (CEditData by value), `map.hpp`
(CMapFlagData by value), `savedatadungeon.hpp`, `quest.hpp` (CQuestData, CMonsterBook),
`menusystemdata.hpp` and `userdata.hpp` (CUserDataManager, CGameDataUsed by value). Included by
`memcard.hpp` (SAVEDATA_FORMAT holds a CSaveData), `title.hpp` and `menuop.hpp`. No vtables in this unit; no
plain-named global data (only the literal `at_453` "size : %d\n").

By-value members: `save_dungeon` CSaveDataDungeon at 0x1C5B4 (0xCE4), then `unk_1D298[8]`: the gap to
`user_data` at 0x1D2A0 is explicit, not alignment (CUserDataManager is not 16-aligned; without it the size
comes out 0x65928). `quest_data` CQuestData 0x62A40, `monster_book` CMonsterBook 0x62EC0,
`menu_system_data` CMenuSystemData 0x640C0 (0x308, ends 0x643C8).

## CSaveData (0x65930)
Size: `memset(this,0,0x65930)` in Initialize, global `SaveData` size 0x65930, memcard memcpy.
First game's CSaveData has a wholly different layout; only the role matches.
- 0x0 u32 bit_flag[64]: CheckBitFlagNo accepts 0..0x7FF; Set/GetBitFlag word = no>>5.
- 0x100 s16 short_flag[128]: Set/GetShortFlag, bound 0x7F, lh.
- 0x200 CMapFlagData[256] (0x10 each): GetMapFlag returns this+0x200+n*0x10, bound 0xFF; callers
  cast to CMapFlagData*.
- 0x1200..0x1A00 never accessed (unk).
- 0x1A00 s64 play_time: VSyncCallBack ld/sd +1 per frame while PlayTimeCountFlag; _STOPWATCH ld.
- 0x1A08 game_progress: Initialize=1; gcPROGRESS / _SET_SAVEDATA_ETC(0) set it; GetGameChapter /
  GetGameProgressInfo / GetNowChapter take it; MainLoop: ==2 forces time 22.0.
- 0x1A0C time_stop: EditLoop calls CScene::TimeStep only when it is 0.
- 0x1A10 float now_time (Initialize 12.0f; CScene::SetTime). 0x1A14 day (MainLoop -> scene+0x2F68,
  TimeStep writes back; MenuInternSelectKey ++).
- 0x1A18 s16 map_no, 0x1A1A sub_map_no, 0x1A1C prev_map_no, 0x1A1E prev_sub_map_no, 0x1A20 s32
  area_no (MapJump / LoadSubMap / DeleteInterior; all -1 in Initialize; menuop SaveMapInfo memcpys
  these 0xC bytes to MenuMapInfoSave (size 0xC)). Could be a 0xC struct; no evidence of a name.
- 0x1A24 s16 build_parts_num[256], clamped 0..9999 (Set/Add), bound 0xFF.
- 0x1C24 CEditData[5], stride 0x5510 (GetEditData bound 4; Initialize loop; GetPlaceEditPartsNum).
- 0x1C574 SV_CONFIG_OPTION config (InitSV_CONFIG_OPTION call in Initialize; title/menu memcpy 0x40).
- 0x1D2A0 CUserDataManager user_data (0x457A0, asserted in userdata.hpp; GetItem tail-calls
  CUserDataManager::GetItem with this+0x1D2A0). CheckTourBoot calls ResetRecord on
  user_data.fish_tournament (0x62488 = +0x451E8) with the null-adjust check against -0x1D2A0.
- 0x643C8 u8 bit_ctrl (Init/Set/Reset/GetBitCtrl, lbu; MapJump ResetBitCtrl(1)). Bit meanings unknown.
- 0x643C9 u8: _SET_SAVEDATA_ETC(3) sets it; TitleLoop: if set, scene+0x906C=1 and clears it. Unnamed.
- 0x643D0 SAVE_TOUR_INFO tour (0x1C): Initialize memsets 0x1C at 0x643D0 then base_day=-1;
  LoadFromMc null-checks against -0x643D0 before reading +0xC, so it is a sub-object.
  start_day/finish_day are relative to base_day (CheckEventDay = day - base_day). now_event s16 (lh),
  type s8 (lb), count s8 (lb, clamp 0..100). +0x10..+0x1C unseen.
- 0x643EC..0x65930 (0x1544) never accessed (unk).

Tour logic (CheckTourBoot): rel=day-base_day; running and rel%10>2 -> stop, finish_day=rel; not
running and rel%10<3 -> start unless the last tournament already finished inside this window;
start sets start_day=rel, now_event=1, count=0, type=type+1 (wrap >2 -> 1) if bit flag 0x1A8, else
1 if bit flag 0x158, else 0. type 1 -> fish_tournament.ResetRecord(); type 2 -> AquaFishFatigueClear().
So type 0/1/2 are distinct tournament kinds; names not established (no enum yet). LoadFromMc sets
base_day=7 when base_day<1 and bit flag 0x158 is set.

Return types: SetBitFlag returns previous bit; SetShortFlag returns previous value (lh before sh);
SetBitCtrl returns previous byte (lbu); GetItem returns int (tail call).

## SV_CONFIG_OPTION (0x40)
InitSV_CONFIG_OPTION: null check, memset 0x40, +0x14 = 1. Option menu map (menuop notes) and uses:
+0 cursor_save (CursorSaveOptionState: true when 0); +4 vibration (VibrationEnable(v==0));
+8 message_speed (ClsMes::GetDrawSpeedDef: 0 when ==1); +0xC sound_mode (SetSoundMode: !=0 mono);
+0x10 fast_time (TimeStep 1.5 vs 1.0 in DngStep/EditLoop); +0x14 map (gcOPTION "Map"; DngMainKey
cycles 0..2; DngMainDraw draws minimap if !=0, 1 small rect / 2 large rect); +0x18 unused;
+0x1C enemy_hp (gcOPTION "EnemyHP", DrawLifeGage arg); +0x20 damage_off (damage scores drawn when 0);
+0x24 unk_24: option menu line 9 (OPTION_ITEM_UNK_9, listed between ANGER_COUNTER and MONSTER_NAME)
writes it through the menu's value[] pointer with two choices (choice_num 2); UpdateOptionForm enables
the choice row that matches the value. No other reader found; +0x28 monster_name ("MonsterName"); +0x2C anger_counter ("AngerCounter",
DrawLifeGage arg); +0x30 dof_off (DepthOfField when 0; EditDraw); +0x34 u8 caption_off
(GetConfigCaptionOff); +0x35 u8 read in nowload PauseLoop (unnamed); +0x36 u8 eye_reverse
(EyeCamera negates stick Y when 0); +0x37 u8 inverted into camera fields in EditCameraControl and
DngMainKey (unnamed); +0x38..0x3F unseen. Field names are descriptive, not retail.

## CSubGameData (0x5470)
Size: `__nw(0x5470)` in SubGameSaveInit, Initialize memset 0x5470, LoadOmakeFile size 0x5470.
- +8 u32 play_enable: PlayEnable(bits, enable==1 ? or : and-not). SubGameSaveInit sets bit 1 when
  GetNowLoopNo()==2 and bit 2 when ==1. Rest of 0..0x100 unseen.
- +0x100 CSphidaData (GetSphidaData), +0x1948 CGyoRaceData (GetGyoRaceData), 0x4570..0x5470 unseen.
Ctor order: sphida.Initialize(), 64x CGameDataUsed ctor at +0x1970 stride 0xA0, gyorace.Initialize(),
Initialize(), sphida.Initialize(), gyorace.Initialize(). This is what inline ctors
`CSphidaData(){Initialize();}` and `CGyoRaceData(){Initialize();}` plus GYORACE_DATA's implicit
ctor produce, followed by a body of Initialize(); sphida.Initialize(); gyorace.Initialize();
No first-game counterpart.

## CSphidaData (0x1848) - Spheda
Initialize memset 0x1848.
- +0 .. +0x48 unseen. +0x48 SPHIDA_PLAYER_DATA[64] stride 0x50 (GetPlayerData bound 0x3F).
- +0x1448 u16 hole_score[9] (lhu in GetHorlScore; sh in SetHorlScore, bound 9). InitPlay memsets
  0x30 from 0x1448, so 0x145A..0x1478 is also cleared (unk).
- +0x1478 s16 now_hole (SetHorl/GetNowHorl lh). +0x147C char player_name[0x1C] (InitPlay memset
  0x1C; SphidaMenuKey strcpy; EnterScore strcpy into entry). 0x1498..0x1848 unseen.
SPHIDA_PLAYER_DATA (neutral name, 0x50): +0 name (EncodePassword reads 0x14 bytes; empty = unused);
+0x18 s32 password_key = rand()%255 (low byte goes into the password); +0x20 s32 total_score
(table order: EnterScore inserts before first entry with total <= new); +0x24 u8 hole_score[9]
(truncated copies); +0x38 s32 set to 1 on entry (sw). EnterScore returns place, or 100 if none.
ClearPlayerScore memsets an entry and recursively pulls the next one up.

## CGyoRaceData (0x2C28) - Finny Frenzy roster; GYORACE_DATA (0xA0)
Initialize memset 0x2C28; GetData returns this+0x28+n*0xA0, bound 0x3F; SearchSpace finds first
entry with s16 at +2 (CGameDataUsed::item_no) < 1. 0..0x28 and 0x2828..0x2C28 unseen.
GYORACE_DATA: Init memset 0xA0; IsUsed = item_no > 1. Callers pass the GYORACE_DATA pointer
unadjusted to CGameDataUsed methods (GetName, CopyGameData), and the CSubGameData ctor constructs a
CGameDataUsed at each entry, so it starts with a CGameDataUsed (0x6C). Declared as member `fish`;
it could equally be a base class (same code). +0x6C..+0xA0 unseen.

## Georama part count return type

`CSaveData::GetBuildPartsNum` loads a signed 16-bit count from `build_parts_num`,
but its C++ return type is `int`. Callers use the returned register directly.
Declaring the function as `s16` adds a sign-extension sequence at call sites;
`int` matches the retail body and its callers.
