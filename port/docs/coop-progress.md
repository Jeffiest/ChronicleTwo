# Co-op for Chronicle Two: progress (work package D)

Branch `wip/coop` of ChronicleTwo (worktree `/home/user/ct-coop`), from `58dd0868`. The plan is DC2_PLAN.md
("Co-op is MoonMod's"); the design is docs/COOP.md once written. This file is the hand-off: what is done, how it
was checked, and the next step, so a successor can continue.

## Done

1. **Net layer** (`port/include/moonmod/net_session.hpp`, `port/src/moonmod/net_session.cpp`,
   `net_transport_{curl,winhttp,web}.cpp`): copied from DC_Web unchanged (same API). CMake
   (`port/CMakeLists.txt` after `ct_options`): `DC_COOP_RELAY_DEFAULT` cache string, `find_package(CURL)` on
   Linux → `DC_HAVE_CURL` + `CURL::libcurl`, `winhttp` on Windows, `curl` by name on macOS, and
   `CT_COOP_WEB_LINK_OPTIONS` (`-lwebsocket.js`) for the web preset to add (port/web not edited).
   `DC_NET_LIBS` is linked in `ct_link_game` (game and tests).
   Checked standalone: `clang++ -std=c++23 -stdlib=libc++ -DDC_HAVE_CURL -I port/include
   DC_Web/coop/relay/test/native_session_test.cpp port/src/moonmod/net_session.cpp
   port/src/moonmod/net_transport_curl.cpp -lcurl -lpthread` against
   `node DC_Web/coop/relay/server.mjs --port N --host 127.0.0.1 --quiet` with `RELAY_URL=ws://127.0.0.1:N`: 17/17.
   `port/src/tests/moonmod_net_test.cpp`: DC_Web's cases plus `AHostAndAGuestMeetThroughTheRelay`, which uses the
   relay named by `DC2_COOP_RELAY` / `RELAY_URL` / `MOONMOD_COOP_RELAY` (the gauntlet's coop stage sets them) and
   skips otherwise.

2. **Runtime core** (ported from DC_Web): manifest `coop` role (`manifest.hpp/.cpp`), `mods.list/settings_get/
   settings_overlay/settings_overlay_clear` (`runtime.hpp/.cpp` `ModList`, `ModSettings`, `OverlayModSettings`,
   `ClearModSettingsOverlay`, `Impl::setting_overlay`; `lua_runtime.cpp` `ModsApi`), permissions `net.session` and
   `mods.settings`, and the catalogue entries and events for every co-op op (`api.cpp`). Overlays clear on
   `game.session.ended/failed`. Compiled as single objects (`ninja port/CMakeFiles/ct_port.dir/src/moonmod/X.cpp.o`).
3. **Save split for DC2** (`moonmod/coop_save.hpp`, `coop_save_moonmod.cpp`): `DCW2` world (bit/short/map flags,
   time, day, map/sub map/area, build parts, progress, dungeon record, quests, monster book, tour, each town's
   `CEditData` and its `TownLayout` as a checked variable field), `DCP2` player (chara_data, robo, bag, bait,
   aquarium, badges, party folk, inventions, party bits, active character, money, medals, photos, fishing,
   costumes, menu data), `DCT2<n>:` town with `MergeTown`; accounts at `<save root>/moonmod/coop_accounts.json`
   (`AccountsEnsureLoaded`/`AccountsStore`).

4. **The co-op units** (`moonmod/coop.hpp`; `coop_api_moonmod.cpp` the ops, dispatched first by the adapter's `Call`;
   `coop_world_moonmod.cpp` avatars as `CCharacter2` subclasses in scene slots, puppets, nearest-player targeting,
   shared map events, the floor seed; `coop_ui_moonmod.cpp` the choice panel, title modes, visits). Hooks (one line
   each, listed in docs/COOP.md). Tests `moonmod_coop_save_test.cpp`, `moonmod_coop_api_test.cpp`,
   `moonmod_net_test.cpp`. `tools/coop/two_games.py` (two headless games through the relay). docs/COOP.md.
5. **The mod**: MoonMod `mods/moonmod.coop` lists both games, reads `game.info.get`, names DC2's dungeons and towns,
   uses `DCT2`, arrives by `world.get().town_map`; `tools/tests/coop/test_coop.lua` has a Chronicle Two section
   (`lua tools/tests/coop/test_coop.lua`: 129 passed). MoonMod docs: COOP_PLAN.md "Chronicle Two", API.md
   "Chronicle Two's co-op". (MoonMod changes are left uncommitted for the coordinator.)

## Step 6 (in progress)

- Disk: the session's allowance is about 40 GB; check `df -h /` during a build (stop under 1.5 GB free).
- Build: `export CT_ROOT=/home/user/ct-coop CT_JOBS=2; . /home/user/ChronicleTwo/tools/gauntlet/env.sh` (export
  CT_ROOT: a `CT_ROOT=... .` prefix does not survive the sourcing, and cmake then gets no -S), PATH with
  /opt/clang20/bin and ~/.cargo/bin, `ct_configure && ct_build`; log in port/build/coop_build.log.
- `tools/coop/two_games.py` rewritten for the real runtime: a mod has no `io`/`os` (lua_runtime.cpp
  OpenSafeLibraries), so the script writes role, relay and the room code into each fixture's main.lua, the host's mod
  stores the code in its storage.json for the script to read, both store their markers and `done` there, and headless
  games are unpaced, so the script stops both when both are done (or on --timeout) instead of counting frames.
- Built (276 steps, EXIT=0). Fixes from the first ctest run: `moonmod_coop_api_test.cpp` includes
  `moonmod/coop_save.hpp`; `coop_world_moonmod.cpp` `OnFloor()` (loop + scene, no player body) for monster slots and
  shared map events (`InDungeon()` wanted `MainChara__2`, so doors and puppets did nothing without one), and
  `FloorSeed` no longer drops a seed set before the floor builds; the API test checks `mods.*` as core operations and
  makes slot 2 a puppet before the host stops listing it (DC_Web's semantics: only puppets are removed).
  `ctest -LE gpu -R "MoonModNet|MoonModCoop"` with the relay: 27/27.
- `^MoonMod` (CPU) needed two more fixes: `lua_runtime.cpp` lets `api.list`/`compat.list` answer up to the resource
  limit (384 KiB; the catalogue with the co-op operations passed 64 KiB, as DC_Web found), and `moonmod_api_test.cpp`'s
  probe mod has `net.session` and lists `mods.*` as core (DC_Web's test does both).
- Results (all in port/build/pc, DC2_DATA/DC2_SAVE empty temp dirs, SDL_VIDEO_DRIVER=offscreen, DC2_AUDIO=off,
  DC2_COOP_RELAY=ws://127.0.0.1:<relay port>):
  - `ctest -LE gpu -R "MoonModNet|MoonModCoop"`: 27/27 (the relay case ran, not skipped).
  - `ctest -LE gpu -R '^MoonMod' -j2`: 106/107; the one failure, `MoonModManagerUi.ChangesTypedSettingsNotifiesMods
    AndSurvivesReload`, is in the gauntlet baseline (baselines/linux-x86_64.json).
  - `ctest -R '^MoonMod.HeadlessProbe$'` (gpu label, the headless game smoke): passed.
  - `python3 -I tools/coop/two_games.py`: PASS (host 7 markers, guest 6, all 8 checks ok).
  - gauntlet `tools/gauntlet/coop/handshake.mjs` (copied beside the relay's node_modules) against
    `node DC_Web/coop/relay/server.mjs` with game `chronicle-two`: PASS (5 ok).
  - MoonMod `lua tools/tests/coop/test_coop.lua` (gauntlet .cache Lua 5.5.1): 129 passed.

## Next

- The gauntlet's coop stage (`tools/gauntlet/run.py --coop-build /home/user/ct-coop/port/build/pc`, G4) on this
  build, by the coordinator (it writes under ChronicleTwo/tools/gauntlet).
- Play-testing on a disc: avatars, puppets, shared doors and visits are only unit-tested without data (the headless
  games stay in the logo fade). COOP.md "What is not finished" lists the known gaps.
- The MoonMod changes (mods/moonmod.coop, tools/tests/coop, docs) are uncommitted, for the coordinator.
