# Windows tester guide

This guide is for the Windows playtest build on `port/upstream-1553ee88-playtest-2026-10-11`. It covers setup, build, launch, known gaps and a broad manual test pass. Use an owned PAL disc image; the game data and save files stay local and out of Git.

## Get the branch and build

Install Git, PowerShell 7, Python 3, 7-Zip, CMake 3.28 or newer, and a Vulkan-capable graphics driver. Clone the branch with its Kazarov submodule:

```powershell
git clone --recurse-submodules --branch port/upstream-1553ee88-playtest-2026-10-11 https://github.com/Jeffiest/ChronicleTwo.git
Set-Location ChronicleTwo
build.bat windows-x64-release
```

The first build downloads the pinned Windows toolchain and dependencies. The game executable is `port\build\windows-x64-release\darkchronicle.exe`.

Place the owned PAL image at `rom\pal\Dark Chronicle (PAL).iso`. The first launch extracts its data into the ignored `rom\pal\extracted\iso` directory. To build and launch in one step at 1280x720 with a fresh test save:

```powershell
run.bat windows-x64-release --save .kazarov\tester-save --window 1280x720
```

If extracted game data is already available at `data\`, launch it directly without re-extracting the disc:

```powershell
& .\port\build\windows-x64-release\darkchronicle.exe --data .\data --save .\.kazarov\tester-save --window 1280x720
```

The fresh config is written to `.kazarov\tester-save\config.json`. Its initial values are `video.present_mode: "mailbox"`, `video.interpolation: false`, and `game.debug_mode: true`. A config already present in a save directory keeps its current choices; use a new save directory to check the defaults. The built-in debug menu is available with **F10**; controls and routes are in [`../../DEBUG.md`](../../DEBUG.md).

## Build and run the tests

After the game build has acquired the toolchain, run the Windows test build and CTest suite from PowerShell:

```powershell
$repo = (Get-Location).Path
$toolRoot = Join-Path $repo '.kazarov\windows'
$testBuild = Join-Path $repo 'port\build\windows-x64-tests'
.\tools\windows\configure-tests.ps1 -Root $toolRoot -BuildDirectory $testBuild
.\tools\windows\build.ps1 -Root $toolRoot -BuildDirectory $testBuild -Tests -Jobs 4
ctest --test-dir $testBuild --output-on-failure -j4
```

Attach the final CTest summary if the build or tests fail. Do not copy the disc image, extracted data, or a personal save into a report or commit.

## What is already checked

The current Windows branch has passed a full Windows build and CTest run (858 cases completed: 852 passed and 6 skipped for platform/prerequisite conditions). A clean save reached the Max costume confirmation screen and a fresh-config windowed startup completed. The scripted opening reaches its first battle tutorial, and the direct debug route loads Dungeon 2 Floor 1. Combat damage/reload and a debug-triggered floor-clear sequence have run without the earlier effect-arena overflow.

The nine retail movie files advance through decoded frames in the movie player and the event movie render loop. That verifies the decoder and render path, but not every story-triggered movie in a windowed playthrough.

## Known issues and unverified areas

- Full story progression has not been swept. In particular, the opening-to-Palm-Brinks route is not yet automated, and the first dungeon and the complete set of dungeon exits/next-floor transitions have not had an end-to-end pass. A previous player-reported crash at a dungeon exit has not been reproduced on this branch.
- Character visibility and placement still need broad testing. Camera/player movement has exposed scene occlusion, HUD icon placeholders, and a report of distant or cutscene characters disappearing or deforming. The reported indoor circus/bench case remains unreproduced.
- Story-triggered movies may freeze even though the nine movie assets and the lower-level event render loop pass. Test the transition into and out of each movie, with audio, in a normal windowed run.
- HUD weapon icons and their health/ammo bars have shown blank or incorrect states during movement and combat. Inventory/menu visuals also need checking; the separate item-bag redesign and icon-cache feature branch is not part of this PR build.
- Wide and 4:3 presentation should both be checked. Fill mode has removed the old inner 4:3 frame in tested scenes, but camera-dependent culling, floor seams, and HUD placement are not fully covered. Black bars in 4:3 Letterbox mode are expected.
- Audio is only partially validated in real scenes. Voice lines have worked in recent user testing, while movie/event audio and transitions between music, ambience and effects still need a full pass. Reverb and some streamed-audio behavior remain limited.

## Manual playtest checklist

Use a fresh save for the first pass, then repeat critical transitions from a save near the relevant event. Test with a controller and keyboard where possible. Do not use debug warps as a substitute for completing the normal route; use the debug menu for targeted reproductions and record when it was used.

### Boot, controls and menus

- [ ] Start from a clean save; confirm language/title flow, music, and the opening logo movie.
- [ ] Check Xbox controller navigation and confirm/select/cancel in title, pause, options, item bag and equipment screens.
- [ ] Open and close each menu repeatedly; change an option, return to gameplay, reopen it and confirm the setting persists.
- [ ] Check item names, descriptions, counts, selection cursor, character switching, weapon slots, and icon stability while scrolling.
- [ ] Verify keyboard F10 opens the debug menu; verify normal controls still work after closing it.

### Story scenes and movies

- [ ] Play every story cutscene from trigger to completion. For each, check character models, animation, camera framing, subtitles, voice/audio, and whether the game accepts the continue input after dialogue.
- [ ] Record any scene that needs skipping to continue, freezes, crashes, or leaves gameplay in the wrong map/state. Repeat from a nearby save once.
- [ ] Test both normal playback and the game's skip action where available; confirm each returns to the expected gameplay/map state.
- [ ] Exercise every retail PSS asset in the movie view and confirm playback, audio, exit and return to the caller: `l5logo.pss`, `open1.pss`, `rush.pss`, `e54001.pss`, `e54002.pss`, `e54003.pss`, `e54004.pss`, `end1.pss`, `end2.pss`.
- [ ] Separately test every story event that launches a movie; movie-view playback alone does not cover these transitions.

### World and dungeon progression

- [ ] Load every chapter hub, town exterior/interior, shop, georama or equivalent building scene, event map, and dungeon. Check doors, exits, return destinations, map changes, save/load, and entering/leaving after a cutscene.
- [ ] Complete every dungeon through its final required floor and boss/event. On every floor, test movement, camera rotation, normal and ranged attacks, lock-on, damage, item use, treasure, floor exit and the next-floor load.
- [ ] Confirm dungeon clear rewards and post-clear scenes, then return to the correct town/world area and verify a save/reload preserves progress.
- [ ] Test the normal story route independently of debug warps. Include a full run of Dungeon 2, whose first-floor debug route is currently the most repeatable smoke test.

### Rendering, audio and stability

- [ ] At 16:9 Fill and 4:3, rotate the camera while walking, standing near foreground objects, and viewing distant NPCs. Check characters, props, shadows and floor geometry for disappearing, stretching, clipping, bright blocks or seams.
- [ ] Check HUD weapon icons, health/ammo bars and minimap during idle, movement, combat, weapon changes, taking damage and character switches.
- [ ] Compare interpolation off and on using separate configs if an artifact appears; keep the present mode unchanged during that A/B. The default test config starts with interpolation off and Mailbox.
- [ ] Listen for dialogue, sound effects, ambience, background music and movie audio before, during and after scene/map transitions.
- [ ] Note crashes, hangs, unexpected pauses, bad loads, or progression blockers. Preserve the console output and the save immediately before the issue.

## Playtest report

Include the branch/commit, Windows version, GPU and driver, display resolution/aspect mode, controller, whether debug mode/warps were used, and the save or chapter/floor. For each issue give the exact steps, expected and actual result, whether it repeats, and attach the relevant console log or screenshot. Keep saves and copyrighted game data local.
