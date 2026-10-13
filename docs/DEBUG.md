# Built-in debug menu

The retail debug tools are available in the PC port and enabled by default. Set `game.debug_mode` in
`save/config.json` to `false` to disable them. F10 or Gamepad:paddle1 toggles the retail debug flag;
the game's pad-2 Circle toggle remains available.

At the title screen, press Start and Select together to open the built-in start menu. The top menu
starts on “game start”; move down twice to “dungeon,” move right once to set dungeon index 1, and
confirm to open dungeon 2. Its chart is Rainbow Butterfly Wood. The first floor box is the box
immediately right of ENTER. In the tested fresh-save route, that box is already highlighted; an
extra right moves onto the next locked “?” box. If the marker is still on ENTER in your run, move
right once, then press X (Cross) through the confirmation prompts. The input-script test is
`tools/input/sweep-debug-dungeon.input`; it reaches `map/d/d02/d02_1.stb` and selects floor 1
without playing the opening cutscene.

The debug-menu replay confirms the floor transition (`FLOOR 0 -> 1`) and loads
`d02m01a.img`, but its gameplay capture still needs verification. A separate, repeatable
`--jump dungeon:1:1` replay uses `tools/input/jump-rainbow-butterfly-floor1.input` to confirm
the floor and move Max after the scripted entry event. At frame 3300, the Windows headless
capture shows Max, the first encounter, HUD and minimap. This verifies floor entry and movement;
it does not yet verify a successful attack or exiting the floor.

In a running dungeon, R3 opens the retail dungeon debug panel. Start+Select exits the floor. In Palm
Brinks, R3 opens the edit debug panel. The top-level menu also includes direct map, title, character,
texture, sound and movie views, plus language, item and save-data tools.

## Jump replays

See [PC test hooks](PC.md#test-hooks) for launch arguments. The direct Dungeon 2 control probe
selects Rainbow Butterfly Wood Floor 1, waits through the entry event, then moves Max forward.
Run it through frame 3300 with `--jump dungeon:1:1`; the replay and Windows headless screenshot
are documented in `docs/PC.md`.
