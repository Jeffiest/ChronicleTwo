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

## Next

4. `moonmod/coop.hpp` (written) and its three units: `coop_api_moonmod.cpp` (the ops), `coop_world_moonmod.cpp`
   (avatars, monster mirror, shared events, floor seed), `coop_ui_moonmod.cpp` (choice panel, title modes, visit);
   the one-line hooks (main.cpp, automap.cpp, dng_main.cpp, editloop.cpp, monster.cpp, save_slots.cpp, title.cpp);
   the adapter's two hook lines; tests; the mod; the two-process check; docs.
