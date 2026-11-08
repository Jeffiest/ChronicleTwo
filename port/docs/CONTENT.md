# MoonMod content registration on Chronicle Two

Work package E of `DC2_PLAN.md`: mods add items, weapons, Georama parts, town villagers and playable characters to
Dark Cloud 2 through MoonMod. This page says what each operation does, where the data lives, which game units carry a
hook, what is not supported yet and why, and how it is tested.

## Where the code is

| Piece | Files |
|---|---|
| Stable numbers (slot registries) | `src/moonmod/content_slots.cpp`, `include/moonmod/content_slots.hpp` |
| Items and weapons: text, icon, model, owner | `src/moonmod/content_items.cpp`, `include/moonmod/content_items.hpp` |
| Georama parts: definition id, town, stock, model, territory | `src/moonmod/content_georama.cpp`, `include/moonmod/content_georama.hpp` |
| Town villagers (`characters.register` kind = "npc") | `src/moonmod/content_npcs.cpp`, `include/moonmod/content_npcs.hpp` |
| Playable characters (`characters.register_playable`) | `src/moonmod/content_characters.cpp`, `include/moonmod/content_characters.hpp` |
| Tests | `src/tests/moonmod_content_test.cpp` (gtest suites `MoonModContent*`) |

The adapter (`src/moonmod/chronicle_two_adapter.cpp`) only parses the new keys through these files and calls them at
the points where it already materializes content. `src/moonmod/api.cpp` gains the `characters.register_playable`
catalogue entry and the `game.npc.talk` event.

### Hooks in game units

Each is one or two lines that call into the files above and do nothing when no mod registered anything.

| Unit | Hook |
|---|---|
| `src/platform/gamedata.cpp` via `src/moonmod/gamedata_moonmod.cpp` | `SetMoonModNextItemNo`: the next mod item/weapon row takes its persisted item number. |
| `src/message_text.cpp` | `Lookup`: a line no table has is asked of `ChronicleTwoModMessageLine` (mod item descriptions). Adds `dc2::text::EncodeMessageText`. |
| `src/menu_icons.cpp` | `GetMenuItemIconTexInfo`: a mod's own icon comes before the model-drawn one. |
| `src/object/world/scenevillager.cpp` | `CScene::LoadVillager`: mod villagers load into the free main-map villager slots after the game's pick. |
| `src/object/world/editloop.cpp` | Talk button: a mod villager answers with its own event or `game.npc.talk` instead of event 1000. |
| `src/platform/userdata.cpp` | `MainCharaModelName`, `GetCharaDataPtr`, `GetCharaHpGage` see registered heroes; `Initialize` resets them for a new game. |
| `src/object/character/charasetup.cpp` | `SetupMainUnit` builds a registered hero as the character he fights as (dormant until the active switch is supported). |
| `src/platform/memcard.cpp` | The dungeon sidecar writer/reader also writes/reads `characters.json` under the same save identity. |

No capacity constant changes: the mod tables keep the 32-entry limits of `gamedata_moonmod.hpp` and the adapter.

## Stable numbers

`<save root>/moonmod/game-content/chronicle-two-{item,weapon,georama}-slots.json`:

```json
{"format_version": 1, "items": {"my.mod:tonic": 511, "other.mod:gem": 510}}
```

The key is `"items"`, `"weapons"` or `"georama"`. A stable id (`<mod id>:<local id>`) gets its number the first time
it materializes and keeps it for ever: an id whose mod is gone stays in the file (a tombstone) so its number is never
handed to other content, and a save that holds item 511 still holds the tonic after mods are added or removed.

- Items and weapons share the game's item numbers. They are handed out from `ITEM_NO_MAX - 1` (511) downward, skipping
  numbers the native tables use and numbers either file already assigns.
- Georama definition ids are handed out from `0x7F` down to `0x40` (the Georama menus list ids below `0x80`); a
  persisted id that a town's native `.gpi` already uses fails for that town with a reason in `georama.get()`.
- A file that is unreadable, malformed, has a duplicate or non-positive number, or sits behind a symbolic link stops
  that kind's content from materializing (logged, `native_item_no` stays null) rather than guessing numbers that could
  clash with a save.

## Operations

### `weapons.register` (supported)

Existing fields (template, stats, caps, durability) are unchanged. New optional fields:

- `owner`: `"max"`, `"monica"` or a registered playable id. The weapon's item type becomes the owner's (melee stays
  melee, Max's gun and Monica's magic swap), so it joins that character's weapons; for a registered hero it joins
  the weapons of the character he fights as, and `assets.holder` names the hero.
- `model`: stem of a `mainchr/<stem>.gltf` this mod owns (`resources.override`). Written into the row's `file_name`,
  so `ItemFilePath` loads it wherever the game draws the weapon.
- `icon`: stem of an `icons/<stem>.png` this mod owns, fitted into the 32x32 menu cell and drawn in place of the
  model-drawn icon (plain mode; dimmed and spectrum modes still use the template sheet).
- `description`: a string, or up to 4 lines of 1-64 bytes. The row gets message number `30000 + n`; the description is
  line `40000 + n` (`GetItemMessageNo` kind 1), served from memory by `message_text.cpp`.
- `menu_scale` is refused: Chronicle Two's weapon menu has no per-weapon scale.

The name is the registered `name` (in the row's `name`, as before). `weapons.get().assets` reports
`{model, icon, description, owner, owner_character, holder, message_no}`.

### `items.register` (supported)

Same new `icon` and `description` as weapons, and the persisted item number. `model` and `owner` are refused.

### `georama.register` (supported)

New optional fields beside `id`, `name`, `template_part_id`:

- `town_id` (0-4): the part materializes only in that town; omitted, in every town.
- `stock` (1-999): built parts given to the save once, when the part materializes and the save holds none built and
  none placed in any town.
- `model = {part = <name>}`: a part of the town's loaded scene (`CEditMap::GetParts`) becomes the part's model; when the
  scene has no such part the template's model stays and `georama.get().mod_pieces[].extras.model_notes` says so. An
  owned scene file (`model.scene`) is refused: Chronicle Two's towns load one scene and the editor takes part models
  from it.
- `territory = {radius, height}` (1-10000 world units): the part's territory in place of the template's.
- The definition id is persistent (see above); `extras.definition_id` reports it.

The copied definition carries the mod's name as its `edit_name` and the template's attributes, so with a template the
Make list shows (no `NO_REFUND` attribute) `CEditMenu`'s Make list lists it, and placement goes through the game's own editor (or `georama.place`). Saved
layouts store the definition id (`TownPart.id`), which the slots file keeps stable, so a layout using the part
round-trips through the JSON save.

### `characters.register` with `kind = "npc"` (supported)

```lua
game.characters.register({id = "moon_trader", kind = "npc", name = "Moon Trader", map_no = 10,
  model = "t01_02", position = {x = 120, y = 0, z = -40}, facing = 90, talk_event = 0})
```

- `map_no`: the town map the villager stands in (0-255). `model`: stem of `chara/<model>.gltf` (a native villager model,
  or one this mod overrides). `position` in world units, `facing` in degrees.
- When the map loads, `CScene::LoadVillager` places the game's own villagers, then each mod villager of that map in the
  next free main-map villager slot (16 slots; a full map leaves the rest out with a note), registered with the
  villager manager at its position and facing. Each gets a character number from `0x7000` up.
- Talk: `talk_event` 1-65535 runs that native event of the map's script; 0 publishes `game.npc.talk`
  `{id, name, map_no, owner, slot}` to mods.
- In memory only; the mod's unload removes them. `characters.list`/`characters.get` report them with `status`
  (`registered` or `placed`) and `note` (why one was not placed).

### `characters.register_playable` (supported, with the limits below)

```lua
game.resources.override({path = "resources/opar.gltf", game_path = "mainchr/opar.gltf"})
game.characters.register_playable({id = "opar", name = "Opar", pack = "mainchr/opar.gltf", fights_as = "monica",
  title = "Moon Sea Frog Mage", portrait = "opar_face", hp = 77, defense = 9})
```

- `pack`: `mainchr/<stem>.gltf` this mod owns, or a native base stem (`c01_base`, `c02_base`), which then follows the
  game's own weapon-numbered models. `fights_as`: `max` (default) or `monica`. `portrait`: `icons/<stem>.png` owned.
  `title` (1-40 bytes, default the name). `hp` (1-999) and `defense` (0-999) default to the starting values of the
  character he fights as.
- Up to 12 heroes, character ids 4-15 (`USER_CHARA_NUM` upward). `character_id` and `count` come back.
- His record is a `CHARA_DATA` outside `CUserDataManager`: `GetCharaDataPtr`, `GetCharaHpGage` and
  `MainCharaModelName` return it for his id, so `player.heal/damage/set {character = <id>}` work on it.
- `party.recruit({id = <hero id>, joined = true|false})` makes him join (default) or leave the party (API-side
  membership, kept in `characters.json`; the save's 16-bit party mask keeps the native ids). `characters.list`,
  `characters.get` and `party.list` (once joined) report him.
- Persistence: `<save root>/moonmod/characters.json`, `{"version": 1, "saves": {"<identity>": {"<id>": {...}}}}`,
  written and read with the dungeon sidecar under the same save identity (`<fingerprint>-<counter>`). Each entry keeps
  `joined`, `hp`, `max_hp`, `defence`, `status_attr`, `status_time`, `keep_costume_on_equip_change`, the three
  active items and the five equipment records (hex of `CGameDataUsed`, so equipped weapons keep their full record).
  Weapons in the bag stay in the retail save (they are Max's or Monica's items). Nothing is written to the retail save
  image. Records of ids no mod registers now stay in the file; a save without an entry starts the hero afresh; a new
  game resets every hero.

Not supported, with the reason:

- **Switching the active character to a hero** (`party.set_active` returns `unsupported`). The dungeon and battle code
  keys on ids 0-3: `CBattleCharaInfo::SetChrNo`, `CUserDataManager::CheckEnableCharaChange` (per-id floor flags),
  `GetCharaMemAllocPtr/Size` (a switch on `USER_CHARA`), `dng_main.cpp`'s single active-unit load, and the menus'
  `USER_CHARA_NUM` tables. None of this can be checked headless. Next step: route `CheckEnableCharaChange`,
  `GetCharaMemAllocPtr/Size` and `CBattleCharaInfo::SetChrNo` through `ChronicleTwoRegisteredCharacterBody` (a hero
  uses his body's memory rows and floor flags), let `CUserDataManager::SetActiveChrNo` accept a registered id, then
  verify in a windowed run that `SetupMainUnit` (already hooked) builds his pack in a dungeon and that town/dungeon
  transitions keep `active_chr_no`.
- **Listing in the game's own party/character menus.** The character-change ring (`menuchr.cpp`, form parts `fc0`-`fc4`,
  `CHR_CNG_SELECT_NPC`) and the status menus are menu-form layouts with one part per native character and tables
  sized `USER_CHARA_NUM`; the save's party mask is 16 bits with ids 0-3 in use. Next step: add form parts for registered
  heroes (or reuse the townsperson slot `fc4` for one hero at a time) and draw his portrait through
  `ChronicleTwoItemIconOverride`'s texture path; then the active switch above.

## Tests

```
cd port/build/pc
DC2_DATA=$(mktemp -d) DC2_SAVE=$(mktemp -d) SDL_VIDEO_DRIVER=offscreen DC2_AUDIO=off \
  ctest -LE gpu -R '^MoonMod' --output-on-failure -j2
```

| Suite | What it checks |
|---|---|
| `MoonModContentSlots` | Two registration orders give the same numbers once the files exist; a removed id keeps its number (tombstone); a duplicate number in the file stops materialization. |
| `MoonModContentWeapons` | Owner changes the item type, model and icon must be owned, `menu_scale` refused, description reaches the message table, stats still apply. |
| `MoonModContentItems` | Icon and description on an item row, the persisted number, `content.list` assets. |
| `MoonModContentNpcs` | Validation, listing, `characters.get`, talk routing (own event / `game.npc.talk` / native), unload. |
| `MoonModContentPlayable` | Registration rules, record routing through user data and model name, `player.damage`, `party.set_active` unsupported, hero-owned weapon, `characters.json` save/load per identity, new-game reset, records outliving the mod. |
| `MoonModContentGeorama` | Persistent ids across registration orders, town filter, scene-part model, territory, stock once (not again when placed), Make-list eligibility, TownLayout JSON round trip. |

Not covered headless: drawing a villager or hero model (needs the retail data and a renderer) and the active switch.
