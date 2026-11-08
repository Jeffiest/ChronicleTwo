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
| A floor's layout | the seed from the room code (`dungeons.set_seed` from `game.dungeon.generating`) | the same seed, so the same floor |
| Monsters on a floor | **runs them**; they go for the nearest player, their own or an avatar (`monsters.mirror_get`) | copies of the host's (`monsters.puppet_set`), their scripts paused; a guest's blow goes to the host (`monsters.mirror_hit`) |
| Doors, gates, switches (map events started with the action or item button) | shared both ways (`dungeons.shared_events_get` / `shared_event_run`); the key a remote event asks for is lent and taken back | the same |
| Georama edits | merged into the host's town (`georama.town_import` with `base`), sent to everyone | sent to the host on leaving the editor |
| The clock | the host's | set to the host's when it drifts (`world.set_time`) |

The other players are drawn as avatars (`world.avatar_set`, slots 1-3): Max's or Monica's dungeon model (town
model in a town), this game's hero's clothes and shoes, eased to the last report, tinted through the ambient light
(a guest in blue). They take the scene's last three event slots on a floor and the last three talk slots in a town,
stepped and drawn by the scene itself. A weapon is not shown yet; names appear in the HUD layer the mod keeps
(`coop_who`), not over heads.

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
- `coop_world_moonmod.cpp`: avatars, puppets, nearest-player targeting, shared events, the floor seed.
- `coop_ui_moonmod.cpp`: the choice panel (`ui.choice_open` / `ui.choice_made`), title modes (`title.mode_*`,
  `game.title.opened/selected`), visits (`session.*`, `game.session.started/ended/failed`).
- `coop_save_moonmod.cpp`: `DCW2`/`DCP2`/`DCT2` records and the accounts.
- Hooks in game units, one line each: `main.cpp` (`FrameStep`), `automap.cpp` (`FloorSeed`), `dng_main.cpp`
  (`MapEventStarted`, `Draw2D`), `editloop.cpp` (`Draw2D`), `monster.cpp` (`MonsterThinkBegin/End`,
  `MonsterDamaged`), `save_slots.cpp` (no writes during a visit), `title.cpp` (the mod rows, the page, the visit).

## What is not finished (reported, never silently passed)

- **Blows on avatars**: a host's monsters target the nearest player but their hits on an avatar are not measured
  (`monsters.mirror_get` returns no `hits`), so a guest takes no damage from mirrored monsters. DC1 measured them
  with the collision data's `CheckHitUser`; Chronicle Two's damage prims need the same treatment.
- **Puppets** pause every monster script on a guest's floor (`DNG_PAUSE_MONSTER_AI`) and step the copies themselves.
- **Avatars** carry no weapon and no name label over the head; a slot the game is using (an event character in one of
  the last three event slots) refuses the avatar for that scene.
- **Shared events** cover map events (doors, gates, switches) started with the action or item button; a remote run
  re-finds the event point at the sent position, so it needs the same floor layout (the shared seed). The exit gate's
  event is shared too, which also moves the other player's floor; the mod's floor following handles the rest.
- **The choice panel** does not pause the game and does not hide its buttons from it (no `input.block_native` in this
  port yet); the press that picks an option may also act in the game.
- **A visit's end** returns to the title; `loaded_save` is not a reason here because a visit never reaches the load
  menu.
- `session.start` without `visit` (a sandbox floor) is unsupported: Chronicle Two has no registered-floor sandbox.

## How to test

- Unit: `ctest -R "MoonModNet|MoonModCoop"` in `port/build/pc` (`moonmod_net_test.cpp`, `moonmod_coop_save_test.cpp`,
  `moonmod_coop_api_test.cpp`). With `DC2_COOP_RELAY=ws://127.0.0.1:<port>` (the gauntlet's coop stage sets it)
  `MoonModNet.AHostAndAGuestMeetThroughTheRelay` dials the relay; without it the case is skipped.
- Two games: `python3 tools/coop/two_games.py --build port/build/pc` starts the relay, runs two headless games on
  synthetic data with a fixture mod that hosts and joins, and checks the `COOP_E2E` markers on both sides.
- The mod and its message protocol: `lua tools/tests/coop/test_coop.lua` in MoonMod runs the mod in fake Chronicle and
  Chronicle Two games.
- The gauntlet (`tools/gauntlet/run.py`, stage `coop`, gate G4) does the relay handshake and the ctest filter.
