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

## Next

2. Runtime core: manifest `coop` role, `mods.*` (list/settings_get/settings_overlay/settings_overlay_clear),
   permissions `net.session` and `mods.settings`, catalogue entries and events for the co-op ops.
