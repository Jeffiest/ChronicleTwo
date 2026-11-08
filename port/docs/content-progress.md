# Work package E (content registration) progress

Branch `wip/content` in the worktree `/home/user/ct-content`. Plan: `DC2_PLAN.md` ("Work packages", row E).
Research notes used: scratchpad `content_research.md` (DC1 reference in DC_Web, DC2 engine facts with file:line).

## Design (decided)

- New code lives in `port/src/moonmod/content_{slots,items,npcs,characters,georama}.cpp` with headers under
  `port/include/moonmod/`. Game units get one- or two-line hooks only (listed in `port/docs/CONTENT.md`).
- Stable numbers: `<save root>/moonmod/game-content/chronicle-two-{item,weapon,georama}-slots.json`
  (`{"format_version":1,"<kind>":{"<mod:id>":number}}`, tombstones kept). Item and weapon numbers share the
  item-number space and are allocated top-down from `ITEM_NO_MAX-1`; Georama definition ids from `0x7F` down.
- Registered heroes: `kRegisteredCharacterSlots` (12) state records outside `CUserDataManager`, indices
  `USER_CHARA_NUM..`; persisted in `<save root>/moonmod/characters.json` keyed by the save identity the dungeon
  sidecar uses. The active switch to a hero stays unsupported (see CONTENT.md) until the battle code stops keying
  on ids 0..3.

## Milestones

- [x] M1 slots registry + items/weapons text, icon, model, owner
- [x] M2 Georama extensions (persistent id, town, stock once per save, scene-part model, territory, Make list)
- [x] M3 NPC villagers (registration, load hook in CScene::LoadVillager, talk hook in EditLoop, game.npc.talk)
- [x] M4 playable heroes: data model, characters.json persistence, API (list/get/party.list/player.*), hooks.
      Unsupported with reason and next step in CONTENT.md: the active switch, and listing in the native menus.
- [x] M5 docs: port/docs/CONTENT.md; MoonMod/docs/API.md section "Chronicle Two content" (uncommitted, for the
      coordinator). No Lua example mod was added; the gtest cases drive the API through the adapter's handler.
- [ ] M6 full test run against the baseline

## State

- Builds with clang 20 (darkchronicle_tests). `MoonModContent*` (10 cases) pass. The `^MoonMod` ctest subset had
  three failures before the Georama API tests were moved to the persistent ids: two of those were the hard-coded
  id 1 in `moonmod_georama_api_test.cpp` (now read through `ChronicleTwoGeoramaNativePartId`), the third
  (`MoonModManagerUi.ChangesTypedSettingsNotifiesModsAndSurvivesReload`) is in the baseline.
- Fixed from the predecessor's diff: hook includes sat in later sections of concatenated units (after their use);
  a `*/` inside a comment; `GetModelNo` on a const record; `player.*` and `characters.get` did not take a hero id.

## Build and test

```
export PATH=/opt/clang20/bin:/root/.cargo/bin:$PATH CXXFLAGS=-stdlib=libc++ CXXSTDLIB=c++
CT_ROOT=/home/user/ct-content CT_JOBS=2 . /home/user/ChronicleTwo/tools/gauntlet/env.sh && ct_configure && ct_build
cd /home/user/ct-content/port/build/pc && DC2_DATA=$(mktemp -d) DC2_SAVE=$(mktemp -d) SDL_VIDEO_DRIVER=offscreen DC2_AUDIO=off \
  ctest -LE gpu -R MoonMod --output-on-failure
```
