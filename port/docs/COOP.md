# Online co-op for Chronicle Two

Up to four players through the co-op relay, driven by MoonMod's `moonmod.coop` mod (MoonMod
`mods/moonmod.coop`, `docs/COOP_PLAN.md`). The engine gives the mod a session to the relay and the operations
the mod needs; the mod decides what the messages mean. The host's game is the world; a guest visits it in a fresh
in-memory game that writes no save.

## Who owns what

| | The host's game | A guest's game |
|---|---|---|
| The world: story and map flags, the day and time, the towns' Georama, dungeon progress, quests, the monster book | **owns** (`save.world_export`) | takes it at the visit's start (`session.start {visit}`), never writes its own save |
| A player's own things: Max, Monica and the ridepod, the bag, Gilda, badges, inventions, photos, fishing | its own | its own (`save.player_export`), sent back to the host every so often, kept in `<save root>/moonmod/coop_accounts.json` (`save.account_*`) |
| A floor | the seed from the room code (`dungeons.set_seed` from `game.dungeon.generating`): the layout (a preset floor's pick too), the chests and the monsters each start from a number made of it | the same seed, so the same floor; `world.get().floor_layout` is a number for the floor as built, equal in two games that built the same one |
| Monsters on a floor | **runs them**; they go for the nearest player, their own or an avatar (`monsters.mirror_get`) | copies of the host's (`monsters.puppet_set`), their scripts paused; a guest's blow goes to the host (`monsters.mirror_hit`) |
| Doors, gates, switches (map events started with the action or item button) | shared both ways (`dungeons.shared_events_get` / `shared_event_run`); the key a remote event asks for is lent and taken back | the same |
| Georama edits | merged into the host's town (`georama.town_import` with `base`), sent to everyone | sent to the host on leaving the editor |
| The clock | the host's | set to the host's when it drifts (`world.set_time`) |

The other players are drawn as avatars (`world.avatar_set`, slots 1-3): Max's or Monica's dungeon model (town
model in a town), this game's hero's clothes and shoes, eased to the last report, tinted through the ambient light
(a guest in blue). They take the scene's last three event slots on a floor and the last three talk slots in a town,
stepped and drawn by the scene itself.

Each avatar carries its player's name (`world.avatar_set {name}`) over its head, in towns and on floors
(`coop_avatar_names_moonmod.cpp`, `DrawAvatarNames`). The place is measured while the scene draws the body, with the
scene's own camera (`AvatarBody::MeasureLabel`): the highest joint of the drawn pose (a hero's body; a ridepod or a
monster with all its parts) is raised a little, a fifth for a hero and a tenth for the other shapes, and goes through
the camera's world-to-screen matrix with the feet. The 2D pass reads that measure once (`AvatarNameOnScreen`, which
also forgets it, so a body the scene stops drawing loses its name) and centres the name over the head in the game's
own font, outlined. It is not drawn behind the camera, when the hero is drawn
shorter than 14 lines of the screen's 448 (too far to read, fading in up to 30), while the scene hides the avatar,
nor during an event scene, a photo, the floor's menus or the Georama editor. The choice panel's `Draw2D` calls it
first, so a panel lies over the names.

## The relay

DC_Web's `coop/relay` (protocol v1, rooms of four, game id `chronicle-two`): `wss://<relay>/v1/host?game=chronicle-two
&proto=<n>&name=<name>` and `/v1/join/<CODE>?...`. Text frames are JSON control messages (`welcome`, `join`, `leave`,
`error`, `closed`); binary frames are `[to][payload]`, `to` 255 for everyone. The engine side is
`port/include/moonmod/net_session.hpp` and `port/src/moonmod/net_session.cpp` (DC_Web's, unchanged) over one of
three sockets: libcurl's TLS connection with our own WebSocket on Linux and macOS (`net_transport_curl.cpp`,
`DC_HAVE_CURL`), WinHTTP on Windows, the browser's WebSocket on the web (`-lwebsocket.js`, which the web preset adds
from `CT_COOP_WEB_LINK_OPTIONS`). The default relay is `DC_COOP_RELAY_DEFAULT` (CMake); `DC_COOP_RELAY` in the
environment and the mod's `server` setting override it. A build without a socket lists `net.*` as unsupported.

## Where it lives

- `port/include/moonmod/coop.hpp`, `coop_save.hpp`: the declarations.
- `port/src/moonmod/coop_api_moonmod.cpp`: the operations (dispatched before the adapter's own in
  `chronicle_two_adapter.cpp`'s `Call`), `player.get`/`world.get`'s extra fields.
- `coop_world_moonmod.cpp`: avatars, puppets, nearest-player targeting, shared events, the floor seed. An avatar
  fades from sight where it stands while one of the story's scenes plays in this game (`StoryScenePlaying`, in
  `coop_avatar_names_moonmod.cpp`: from the scene's start, `DramaSceneStarted`, while the game offers its skip,
  `EdEventInfo.skip_state`, and the place's event has the game, until the player is free again;
  `world.get().story_scene`) or while its player watches one in theirs (`world.avatar_set`'s `away`: then no monster
  here goes for it either), and fades back in after; its name fades with it. A copy whose monster died in the
  leader's game dies here as the game's own once it is shown at that moment (8 frames behind the leader's clock):
  `CMonsterMan::ThinkHost` plays its death, the monsters thinking that frame with every other copy resting
  (`CopiesDying`, `CopyRests`), so its death motion, burst, orbs, drops, a key it carries and the floor's count are
  this game's own. The killing blow is this hero's when it struck the copy in the last 3 seconds (its weapon takes
  the orbs' absorption), else another player's (`last_hit_chara` -1: the game shares it between the hero's
  weapons, as the leader's game does for a kill by a guest's blow, `monsters.mirror_hit`). A dying copy deals no
  blow (`MonsterThinkEnd` drops its damage primitives); the game's events and menus hold it as they hold every
  monster. The other players' heroes are on the floor's map (`AvatarsOnMap`): on the aerial map each one's model is
  drawn as this game's hero is (`DrawFloorModels` in `automap.cpp`, anywhere the map reaches, revealed or not), and on
  the plain map each has the hero's arrow in that player's colour, light blue, orange or white, none of the map's own
  (`CMiniMapSymbol::DrawSymbol_Chara` given a colour: the arrow's texture is green, which a colour laid over it
  cannot change, so theirs is its shape drawn flat, on a dark edge, at its size and turned as it); both fade with the
  hero and are not drawn on a boss's floor, as the hero's own arrow is not.
- `coop_ui_moonmod.cpp`: the choice panel (`ui.choice_open` / `ui.choice_close` / `ui.choice_made`), title modes (`title.mode_*`,
  `game.title.opened/selected`), visits (`session.*`, `game.session.started/ended/failed`). A question and a notice
  wait while the player is not free (`PlayerFree`, in `coop_avatar_names_moonmod.cpp`: a floor's field with no
  event script, or a town walked with the town's own control, or the Georama editor between actions; never a scene,
  a talk, a menu, a photo, a door, a minigame or a load). Meanwhile the panel is not drawn and does not hold the pad,
  and it is asked 8 frames after the player is free again.
- `coop_save_moonmod.cpp`: `DCW2`/`DCP2`/`DCT2` records and the accounts.
- Hooks in game units, one line each: `main.cpp` (`FrameStep`), `automap.cpp` (`FloorSeed`; the aerial map's `AvatarsOnMap`, a few lines, since its helpers are the file's own), `dng_event.cpp`
  (`FloorBuildBegin`), `event_func.cpp` (`FloorPlacing`, `FloorPlaced` around the floor's chests and monsters; `DramaSceneStarted`),
  `dng_main.cpp` (`MapEventStarted`, `Draw2D`, `CopiesDying`, `DrawAvatarsOnMiniMap`), `editloop.cpp` (`Draw2D`), `monster.cpp`
  (`MonsterThinkBegin/End`, `MonsterDamaged`, `CopyRests`), `save_slots.cpp` (no writes during a visit), `title.cpp` (the mod rows, the page, the visit),
  `effscript.cpp` (another player's shot or spell is drawn only, `shown_only`: it makes no collision primitive, and
  its script's questions after one are answered as a primitive that hits nothing, never logged as illegal calls).

## What the real game changed (2026-11-14)

Until `tools/coop/live.py` none of this had run on the disc's data. The first runs found, and these fixed:

- **The title's mod row** could not be opened on a game without the hard disk row: the first mod row has that
  row's number, and its branch came first (`title.cpp`).
- **A world or a player's things** is several hundred kilobytes as plain base64, over the 64 KiB an operation may
  answer and a visit's request may hold. The bytes are packed first (what repeats is written once; `!` after the
  tag): a new game's world is a few kilobytes. `PlainText` gives the unpacked form; imports read both.
- **A visit's place** was a town's area with a Georama map. Palm Brinks has none, so nobody could join a host in
  chapter 1: `session.start {visit = {map_no}}` takes a map's own number (`world.get().map_no`).
- **A visit ended the frame it began**, the game still being at the title then; it now ends on a return to it.
- **`dungeons.enter_floor`** stopped at the dungeon's floor list; it runs the floor load event (1010) and lands on
  the floor.
- **A place** is the map the town runs, not its area (Palm Brinks is five maps in one area): `world.get()` gives
  `scene_map`, `house` (the sub map inside one) and `map_title`; also `progress` and `chapter`, and `walking` on a
  floor.
- **A headless game** runs unpaced; `DC2_HEADLESS_BOUNDED=1` keeps it at the real tick rate, so two games share
  a clock.
- **The 64 KiB an operation may take or answer** is lifted to 2 MiB for the operations that carry a save's text
  (`CarriesSaveText` in `lua_runtime.cpp`): a played save's photos alone are 120 KiB that no packing shrinks.

## A joiner's own save (2026-11-14)

In Dark Chronicle a player who joins brings one of their own saves, and the host keeps nothing of theirs
(MoonMod `docs/plans/DC2_COOP_PLAN.md`, decision 2). Three operations, in `coop_save_moonmod.cpp`:

- `save.slots_list`: this game's numbered saves with each one's story progress and chapter, from the save
  headers (`SaveSlotScan`). The mod's Join page offers those at or past the gate (progress 2: the talk with Donny
  at the Channel Entrance is over).
- `save.slot_player_export {slot}`: a player's things out of a save that is not loaded (`SaveSlotRead`, then
  `ExportPlayer`).
- `save.slot_player_import {slot, data}`: during the mod's visit, the player's things back into that save and
  nothing else of it. The save is read, the things replaced, and the file written by `SaveSlotWriteOwnThings`
  (`save_slots.cpp`: `SaveSlotWrite` without its rule that a visit writes no save). Before the first write to a
  save in a run the file is copied to `<save root>/moonmod/coop-own-saves/<name>.YYYYMMDD-HHMMSS-NN<ext>`: a new
  copy each run, never written over an older one, and the newest five copies of each save kept (2026-11-15). A
  copy taken in the same second as others gets the number after theirs, not a number the pruning freed. Its test
  (`SaveSlotTest.CoopCopiesASaveAsideEachRun...`, seven runs inside a second or two) still fails about one run in
  ten on a loaded machine with a copy's contents one run behind; not understood yet, and not seen in play, where
  runs are far apart.
- Since 2026-11-15 the save keeps its own townsfolk (`party_chara`: who has been recruited, who lives where) as it
  keeps its own hero; only the player's things are the visit's. Only a JSON save is read or written: an old
  memory-card save is refused ("open it once outside a visit to convert it"), and the game does not convert one
  while a visit runs (`ImportLegacy` in `save_slots.cpp` waits), because a visit writes no save and the old one
  would be archived with nothing in its place.

The accounts (`save.account_*`) stay for a mod that keeps a guest's things with the host, as Dark Cloud 1's does.

## The world as it changes (2026-11-14)

A visit takes the host's world whole at its start. After that both games change it, apart or together: a scene
seen, a floor cleared, a townsperson won over. `coop_register_moonmod.cpp` tells a mod of the changes this game
makes and takes the other game's, without a hook in any game unit:

- `world.watch_set {enabled}`: once a frame, while a town or a floor runs, the live save is compared with what the
  games last agreed on. Each difference is one `game.world.changed {kind, id, value}` for the watching mod and
  becomes the agreed value: a story flag (`flag`, true or false), a story counter (`counter`), the story's progress
  (`progress`, id 0), the story's party (`party`, id 0 the members, 1 who can be changed to, 2 the mask), a
  townsperson who can join (`recruit`, value `{chara_no, status}`), a dungeon floor's record (`floor`, id its
  place in the save's records, value its nine numbers), a Georama part's stock (`stock`, id the part's number,
  value how many are built and waiting to be placed) and a town's Georama (`town`, id 0-4, see below). At most 48
  a frame; the rest follow the next frame. A
  town or a floor beginning (a save loaded, a visit begun, `save.world_import`) is where the games start from and
  tells nothing.
- `world.change_apply {kind, id, value}`: another game's change, written to the save and to the agreed value
  together, so it is not told back. A floor's record is merged with this game's (its flags together, the higher
  counts, the better time); what this game holds beyond what was sent is then told in turn, so both end the same.

Each map's own flags (a town's opened chests) are not shared: chests are each player's own.

Since 2026-11-15:

- **Progress only goes forward.** A `progress` change from another game is taken only when it is higher: this
  game's progress becomes the higher of the two, and the agreed value is theirs (so a game ahead tells its own
  again, and both end at the higher).
- **Who walks with whom is each game's own.** A `recruit`'s status is shared without its in-party bit
  (`PARTY_CHARA_STATUS_IN_PARTY`): recruited and housed are the world's, and whether the townsperson walks with
  this game's player stays this game's. A player taking a townsperson into the party tells nothing.

**What the story gives.** Each player has a bag of their own, and the story's scripts give items to whoever stood
in the scene (`_ADD_ITEM`). In a room that would leave the host's world without a thing its story needs whenever
the joiner saw the scene alone. So an item an event script gives is told as `gift` (id the item's number, value
how many) and `world.change_apply` adds it to the other game's bag with the game's own `GetItem` (an item there
is only one of is not given twice; what does not fit goes where the game puts it). A dungeon's system scripts are
left out (`SystemScriptBegan`, from `EventScriptSetup`): a chest's content, a floor's prize and a Spheda win are
the player's own. One line each in `event_func.cpp` and `dng_main.cpp`.

Since 2026-11-15 a gift is told only when the story moved beside it: a story flag, a counter or the progress
changed within 300 frames of it, before or after. A gift with no such change after 300 frames is dropped (it stays
in this player's bag only). In the decompiled town scripts every story gift found sets a story flag in the same
script around its `add_item` (Sindain's Lafrescia seed, Badge Box and Jurak's gun set `FLAG_SINDAIN_EVENT_272` and
`_285_SEEN`; the gold paint sets `FLAG_GOLD_PAINT_RECEIVED`), while a dungeon chest sets only its box's state under
a system script (already left out). No town-map chest script was found giving through `add_item`, so the rule's
first use is the case of a script that gives without moving the story (a shop, a trade), which now stays here.

## A town as it is built (2026-11-14)

A Georama town is its map's number, 0-4 (`CSaveData::GetEditData(MapNo)`), not its area: `georama.town_export` and
`town_import` go by that number, and `world.get().georama_town` gives it for the town on screen.

A town is two things while it is on screen: its record in the save and the live map built from it. The game brings
them together only when its player leaves the editor (`EditDataSave`) or walks into the town (`EditDataLoad`).
`coop_town_moonmod.cpp` brings them together sooner, for two games:

- **Told part by part.** Every 20 frames the register takes a number for each town's record
  (`TownFingerprint`) and tells the watching mod `game.world.changed {kind = "town", id}` when it differs. Before
  that look, a town in its editor is written into its record (`TownLiveSync`, the map's own `SaveData`), so each
  part placed, moved or taken away is a change, as is anything the town works out when the building is done. The
  mod sends the town itself (`georama.town_export`); a town taken from another game is agreed as it arrives and is
  not told back.
- **Taken where the player stands.** `georama.town_import` into the town on screen no longer walks the player
  into it again. It writes the record and asks for a rebuild (`TownRebuildWanted`); `TownRebuildStep`, each frame,
  runs the steps of the game's own load of a town (`EditDataLoad`, as its debug menu calls it: clear the parts,
  `LoadData`, the ground, the houses, then `GeoUpdateNpcPos`; not its analysis, which the builder's game does
  and sends with the record) once the player walks free: not in a scene or a menu, not inside a house (then on coming out). `EditDataSave` calls
  `TownBeforeSave` first, so a town waiting for its rebuild is never overwritten by the old live map. The import
  is still refused as busy while this game's player is in that town's editor, where the live map is ahead of the
  record.
- **One builder's town is taken whole.** `MergeTown` (a joiner's town with the one it was changed from) takes
  the edited town as it is when this game's town still equals the one it was changed from, which is every time
  while one player builds part by part: the parts keep their order and the placement log that indexes them.
  Only when both changed the town is it merged by parts. Since 2026-11-15 the placement log is kept then too,
  renumbered: this game's entries and theirs for the parts they added, each with its parts' places in the merged
  list, and an entry whose part was taken away goes with it. (`CEditMap::LoadData` loads a part saved as placed as
  unplaced, and places it only through a log entry that rests it on a placed part: a merged town with no log loaded
  with its parts standing unplaced. `GeoramaParts.AMergedTownLoadsEveryPartBothGamesPlaced` loads one.) The
  town's `save_count`, which the game bumps at every house entered and map changed, is left out of
  `TownFingerprint` and of the "nobody here changed it" comparison.
- **Built parts are the world's.** `build_parts_num` is shared as `stock`, so a part one player builds can be
  placed by the other.
- **The town on screen goes as it stands** (2026-11-17). A town often stands ahead of its record outside the editor
  too: one loaded before the game first saved it, or one the story changed, is written only when its player leaves
  it. The record is what the other game gets (in the world a visit starts from, and as the town a joiner's change was
  made from), so a host whose Sindain stood with 15 parts over an older record took a joiner's first part as a town
  "nobody here changed", took it whole and built its own map again from it, losing the 2 parts that only stood
  (`build_edit_opening_450` and `_600`: 17 parts where 19 should stand, in both games). `TownLiveSync` now writes the
  town on screen into its record while its player walks free as well as in the editor, and
  `TownRecordFromLiveMap` does it at once before `georama.town_export`, `save.world_export` and a merging
  `georama.town_import` into the town on screen; neither writes while a rebuild from the record is pending. A part
  both games put up in one spot since the agreed town stands once after `MergeTown`
  (`MoonModCoopSave.APartBothGamesPutUpInOneSpotStandsOnce`), and `save.world_import` while a town is on screen
  asks for its rebuild. The engine logs `co-op: town N taken (...)`, `built again from its record (N parts)` and
  `is written from its live map (N parts; its record N)` as it goes.

Since 2026-11-15:

- **The editor writes no stale map.** The town is brought up to its record (`TownBeforeSave`) where `editloop.cpp`
  enters Georama mode too (from the menu and from walking), and `TownLiveSync` never writes a live map over its
  record while a rebuild from the record is pending for that town (`TownRecordWritable`).
- **A player is not left inside a part.** After a rebuild, a player standing where a part just landed is set down
  on the nearest spot the game would let them walk out of Georama mode onto (`CheckEditWalkSpot`, retail's
  `CheckEditToWalk` for any spot: walkable town ground, no river, nothing over it), searched in rings of 40 to 800
  units, 16 spots each. A spot that was not walkable before the rebuild is left alone. The log says
  `co-op: a part landed where the player stood (...): moved to ...`. Live scenario `shove`.
- `LayoutFromBytes` refuses a river grid whose cells are not `num_x` by `num_z` or whose counts are below zero,
  and an import refuses a text that gives one field twice.

Not done: two players in the same town's editor at once do not see each other's parts until one leaves it (the mod
then merges both), nor each other's cursor.

## Down, not out (2026-11-14)

Alone, a hero whose life runs out dies: `IsRunDeadEvent` asks for the death, `CActionChara::RunScript` runs the
death program and raises the dungeon's death event (1200), which changes to another hero or ends the game. In a
room each game plays one hero, so that would be Game Over at every fall. `coop_down_moonmod.cpp`:

- `player.down_rule_set {enabled}` (the rule belongs to the mod that set it). Under it the death program still
  plays, to its last pose, and the hero stays there (`HeroFalls`, asked where the death event would be raised;
  `HeroHeldDown`, asked where an ended script would go back to the hero's normal program). No new death is asked
  for while the life is at nothing (`HeroIsDown` in `IsRunDeadEvent` and `CheckStatusError`), and each frame the
  hero's grace is kept up (`DownStep`), so nothing strikes them. `player.get().down` says so.
- `player.revive {hp, x, y, z}`: life back, the normal program again, two seconds of grace. `player.give_up`:
  the death event is raised after all. Turning the rule off while the hero is down gives up too (2026-11-15).
- The others: `world.avatar_set`'s `down` marks another player's hero as down; a monster's turn does not go for
  it and its blows are not tested against it. A game whose own hero is down has its monsters go for any other
  player on the floor first.
- `world.get().exit_map` on a floor: the map the dungeon's own way out leads to (the table the game's menu uses),
  for the mod to walk a fallen party out with `world.enter_map`.

A player in the ridepod or a monster's form is drawn as it (`world.avatar_set` takes `"ridepod"` with
`ridepod = {leg, arm, body, pack}`, the parts' item numbers, and `"monster"` with `form = monster_id`; their game's
`player.get()` gives `ridepod` and `form` while it plays them). The ridepod is built as `SetupMainUnit` and
`SetupRobo` build the player's (`charasetup.cpp`): the leg's model is the body, the arm hangs on its `body<n>` frame
(n the arm's number), the body on `spine`, Max seated (`mints<type><outfit>`, from the body's type and the outfit the
other player sent) on `joint2`, the energy pack on `joint1` and Max's hat on `hat`, each found down the chain of
parts already hung as `CActionChara::SearchObject` finds it; the arm's `ude` frame takes the matrix of the body's
`arm<n>` frame. The motion numbers the other game sends are its leg's, and every part plays the same number, as the
game's own `CActionChara::SetMotion` has the chain do. A form is the monster's model read with its own profile
(`GetMonsterModelFile`). When the parts or the form are not said, or a model or a frame is missing, the hero stands
in, running while the reports move. A change of shape, hero or outfit loads the new body in the avatar's own slot.

Three lines in game units: `actionchara.cpp` (two) and `dng_main.cpp` (two guards). Not done: the hero's own
Resurrection Powder is not looked at before they go down; the ridepod and monster forms go down like a hero but
`player.revive` gives back only the hero's life.

## One hero each (2026-11-14)

`party.room_hero_set {hero = "max" | "monica"}` makes this game play one hero until `party.room_hero_clear` (or
the mod that set it unloads); `world.get().room_hero` says which. It is in `coop_party_moonmod.cpp`, declared in
`moonmod/coop_party.hpp` (no JSON, for the game units that call it), and it is a filter where the game reads its
party, never a change to the save:

- `CUserDataManager::GetNowPartyMember` answers the hero, and its second shape (the ridepod for Max, a monster form
  for Monica) when the story's party has it. Monica is in the party for her player before the story adds her.
- `CUserDataManager::GetEnableCharaChangeFlag` answers the hero and that shape when the story allows the change;
  the other hero cannot be changed to (L3 does nothing), and a floor that disables the hero does not.
- The story's scripts asking who is in the party (`event_func.cpp`, under a `StoryPartyScope`) hear the story's
  own answer, so a scene behaves as the story has it.
- `CDngFloorManager::IsSealFloor`: no seal in a room. A seal locks one hero out, and a player has only one.
- A visit starts as the room's hero (`TitleBeginVisit` sets the active character before the map loads). Set while
  playing, the hero changes through the game's own change as soon as the player could have pressed it:
  `RoomHeroFloorChange` in `dng_main.cpp` (out of a ridepod or monster form first, then across) and
  `RoomHeroTownChange` in `editloop.cpp`, each asked at most once a second.

The story's party fields (`party_member`, `chara_change`, `chara_change_mask`) are the world's in the save's
split (`DCW2` fields 14-16), no longer a player's; a joiner's own save keeps the character it plays as when its
things are written back.

## A floor cleared for both (2026-11-15)

The game clears a floor when its own count is at nothing (`IsEventRun`: `GetMonsterNum(-1)`, every active slot whose
`chara_kind` is a script's, and `MimicCount`), and then runs system script 1500 (`play_floor_clear`). Two things
kept that count above nothing in a room:

- **A monster struck down out of sight never died.** `ThinkHost` skips a monster `CheckView` puts out of view unless
  its state is already dead, so a `damage_req` of dead set on one far away was never taken up, and its slot kept
  counting. Retail only kills monsters in sight; a co-op host also takes blows from another player's game, far
  from its own hero. A dying monster (death asked, or life at nothing) now passes the view test.
- **A guest's copies kept their slots.** A monster gone on the leader set the copy's state to none but left its
  `chara_kind`, so the guest's count never fell. `monsters.puppet_set` lists the leader's whole floor: a slot it
  does not name is freed (state, `chara_kind`, life, the scene's active bit).

`world.get()` on a floor gives `monsters_counted` (the game's own count), `mimic_chests` and `floor_cleared`. Live
scenario `clear`: both games count nothing, run system script 1500 and stay in the visit. (The clear's own scene
ends in two messages that wait for a button; a headless game stays in it.)

## Monsters the leader gains (2026-11-15)

A monster the leader gains after the floor is built (a mod's foe, a monster the floor sends in) is made in the
follower's game in the same slot: `CMonsterMan::SetActiveMonster` takes the slot to use (`wanted_slot`, refused
unless free), and `ChronicleTwoMonsterReferFor` finds or loads the kind (a refer loaded already, a mod's enemy as
`characters.spawn` loads it, or one of the game's own kinds through its loader). A kind this game cannot make is
refused for that slot only, once in the log (`co-op: the leader's monster in slot N (kind K) can't be made here`);
the others still follow. A leader's monster already dead is not made. Live scenarios `hurt` and `down` check that
the follower's count matches the leader's and that it is never out of step.

## A mod closes its own panel (2026-11-15)

`ui.choice_close {}` closes the calling mod's own open choice panel without a `ui.choice_made` (the mod closed it,
so it knows) and answers `{closed = true}`; with no panel of its own open it answers `{closed = false}`, and a
panel another mod opened stays open.

## A boss's room together (2026-11-18)

A boss's room is a map of its own (the Underground Channel's is `d01b01`), loaded by the floor's own event in place
of a built floor: it has no seed, so the mod had nothing to know it by. `world.get()` on a floor now gives
`floor_map` (the map loaded, a built floor's too) and `boss_floor`: true on a map named `d<dungeon>b<room>` from the
moment it loads (`BossRoomMap`), or once the game itself marks the floor (`BattleAreaScene->boss_map`, set only as
the boss comes out at the end of the room's own scene). A made map clears the last floor's seed
(`FloorMapLoading`, from `LoadDungeonMapFile`), so a boss's room never reports the floor before it. The mod names
the place `D<dungeon>:<floor>:<map>` and asks the other player "Max faces the boss of the Underground Channel: join
the fight?" in the game's window.

`monsters.hold {frames}` (0-36000; 0 ends it) has the fight wait while the other player answers and walks in: the
floor's monsters do not think (`ThinkHost` is gated, not `DNG_PAUSE_MONSTER_AI`, so a copy dying still dies), the
floor's effect scripts rest, and this hero's script does not run, so the hero does not walk up to the boss and start
its scene. The frames count only while the dungeon is in play (not in a menu or a scene); the hold ends by itself
when they run out, when the player leaves the floor, or when another scene begins. `world.get().fight_held` is true
meanwhile. The mod holds for at most a minute of play, tells the player who it waits for and asks, in the game's
window, whether to start without them; the other player arriving, saying "Stay here", or the room closing ends the
wait.

Live scenario `boss`: the host walks into the Underground Channel's boss's room (story flag 10 set first, so the
room's way in is its short one: the first visit's hint ends in a message that waits for a button), the joiner is
asked and joins, the host's fight waits and then goes on, both are in `d01b01`, and both games scale the room's
monsters for two. Not covered: the boss's own scene and the fight (the scene starts where the hero walks up to the
boss, and a headless game does not walk there), so how the scene plays in the other game and the boss copied there
are unverified.

## Harder with two (2026-11-18)

A floor played by two is played harder, and each part is the co-op mod's setting (the host's choice is the room's,
sent as `SCALE life blows bosses monsters floors`):

- `dungeons.set_modifiers {rooms, monsters}` (0-300 percent, owners added, `{}` clears the caller's) on Chronicle Two:
  a floor built afterwards has that many more rooms (`RoomGoal`, after the floor's own `DngLayoutRooms`) on a grid
  as much wider (`BuildArea`: 14 squares become 21 at +50%, up to `DNG_GRID`; grown only by the square root, the
  random placement gave up on rooms it could not fit), and that many
  more of the floor's own monsters, placed as the game places them (`AutoSetMonster`'s last step; mimics left out;
  the active-monster limit raised to `MONSTER_MORE_MAX` meanwhile). Both games apply the same numbers before the
  same seed, so they still build one floor: `floor_rooms`, `floor_grid` and `monsters_added` in `world.get()` say
  what was built. `chests` and `chest_value` are refused here (not built yet).
- `dungeons.set_enemy_scale {life, blows, boss_life, boss_blows}` (percent, 10-1000, owners multiplied, `{}` or all
  100 clears): a monster made afterwards has `life` percent of its life (`boss_life` for a boss, by its table's
  `boss`), a monster's blow on the hero lands `blows` percent as hard (`boss_blows` for a boss's, or for a blow no
  monster dealt on a boss's floor; `Blow`, in `actionchara.cpp` and in an avatar's hit), and the monsters already out
  are rescaled at once, keeping their share of life. A follower's copies take the leader's life as they stand.

The mod sets the floors' numbers while anyone else is in the room, and the monsters' while another player is on
this floor (100 + each setting for each other player there); alone, or once the room closes, it sets `{}` and the
game is its own. Live scenario `scale`: both games build floor 2 larger and fuller and alike, the host's monsters
have their own life while the host is alone there and 60% more once the joiner stands there too, in both games.

## What is not finished (reported, never silently passed)

- **Blows on avatars** are measured since 2026-11-14: each frame the attack primitives aimed at the player's side
  are tested against each avatar's body with the game's own test (`CColPrim::IsHit`, in `AvatarHitsStep`), and what
  lands is in `monsters.mirror_get`'s `hits` as `{avatar, monster, damage, kind, flags}` (`damage` the blow's
  strength, `kind` 2 a flinch or 3 a knock-down). The other game takes it with `combat.strike_player`, where its
  own hero's defence comes off the strength as off a monster's blow. Guarding and dodging are not weighed yet: a
  blow that reaches the avatar's body lands.
- **Puppets** pause every monster script on a guest's floor (`DNG_PAUSE_MONSTER_AI`) and step the copies themselves;
  a copy whose monster died there dies here through the game's own `ThinkHost` (above).
- **Avatars** wear and carry what the other player's own hero does (`world.avatar_set {outfit}`, from
  `player.get().outfit`): the hat or hair piece and the weapons hang on the body's frames as the hero's own do. A
  slot the game is using (an event character in one of the last three event slots) refuses the avatar for that scene.
  The names over heads are not hidden by walls between (no depth test against the map).
- **Shared events** cover map events (doors, gates, switches) started with the action or item button; a remote run
  re-finds the event point at the sent position, so it needs the same floor layout (the shared seed). The exit gate's
  event is shared too, which also moves the other player's floor; the mod's floor following handles the rest.
- **The choice panel** does not pause the game, and it has the pad's buttons while it is open
  (`PanelHoldsPad`, asked in `input/gamepad.cpp`): the press that picks an option does not also act in the game.
  The sticks stay the game's, so the hero can still walk while the player decides.
- **A visit's end** returns to the title; `loaded_save` is not a reason here because a visit never reaches the load
  menu.
- `session.start` without `visit` (a sandbox floor) is unsupported: Chronicle Two has no registered-floor sandbox.

## How to test

- Unit: `ctest -R "MoonModNet|MoonModCoop"` in `port/build/pc` (`moonmod_net_test.cpp`, `moonmod_coop_save_test.cpp`,
  `moonmod_coop_api_test.cpp`). With `DC2_COOP_RELAY=ws://127.0.0.1:<port>` (the gauntlet's coop stage sets it)
  `MoonModNet.AHostAndAGuestMeetThroughTheRelay` dials the relay; without it the case is skipped.
- Two games: `python3 tools/coop/two_games.py --build port/build/pc` starts the relay, runs two headless games on
  synthetic data with a fixture mod that hosts and joins, and checks the `COOP_E2E` markers on both sides.
- Two games on the disc: `python3 -I tools/coop/live.py --scenario meet|floor|clear|hurt|down|build|shove|boss|scale --keep --out DIR` runs a host and a
  guest on extracted retail data (`--data`, `DC2_DATA` or the gauntlet's cache) with MoonMod's co-op mod and its
  test driver (`--moonmod`), and checks what both logged: the visit, each game showing the other in town and on a
  floor, the same seed and `floor_layout`, the guest's monsters copying the host's, a kill ending on both. With no
  data or no MoonMod checkout it prints `NOT RUN` and exits 2.
- The mod and its message protocol: `lua tools/tests/coop/test_coop.lua` in MoonMod runs the mod in fake Chronicle and
  Chronicle Two games.
- The gauntlet (`tools/gauntlet/run.py`, stage `coop`, gate G4) does the relay handshake and the ctest filter.
