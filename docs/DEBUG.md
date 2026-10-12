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

The current real-data run confirms the floor transition (`FLOOR 0 -> 1`) and loads
`d02m01a.img`, but then stalls in repeated `flat put err` messages from dungeon event-point placement
before a gameplay screenshot is produced. This is the next dungeon-loading blocker to investigate.

In a running dungeon, R3 opens the retail dungeon debug panel. Start+Select exits the floor. In Palm
Brinks, R3 opens the edit debug panel. The top-level menu also includes direct map, title, character,
texture, sound and movie views, plus language, item and save-data tools.
