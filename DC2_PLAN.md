# Dark Cloud 2 engine plan

The plan for Dark Chronicle (Dark Cloud 2) on PC and the web: bigger limits, an engine that mods can extend,
a browser build, online co-op, and a verification gauntlet that an agent runs without a person. It mirrors
what was built for Dark Cloud 1 (Chronicle, MoonMod, DC_Web) and reuses it wherever the two games share code.

## Decisions

- **The engine host is this repository's `port/`**: ChronicleTwo's own native port (clang, C++26, SDL3,
  Kazarov's Vulkan renderer), taken from upstream `TheMoonPeople/ChronicleTwo` at `7a7d7f29` (its master of
  2026-11-02). The branch `claude/dark-cloud-2-engine-yr3rlk` is that commit plus the work below. Upstream is
  merged forward on a schedule (see "Upstream").
- **Kazarov is vendored** at `tools/kazarov` (upstream's `fd72338a` plus MoonMod's input and camera patch), as
  DC_Web vendors it, so a clone needs no submodule and the patch stays with the tree.
- **MoonFactory (MoonMod) is applied in-tree** from `MoonMod/ports/chronicle-two/` and is the extension
  framework: Lua mods in a sandbox, a Mod Manager, the `moonmod` API, native plugins. Every new engine
  capability (more characters, NPCs, weapons, items, Georama parts, co-op) is exposed through that API, so a
  mod and the game's own content use the same path. MoonMod's canonical sources (`src/`, `include/moonmod/`)
  stay in the MoonMod repository; this tree's copies are checked against them.
- **DC2-Toolkit stays the content toolkit**: scripts, maps, dungeons, Georama, data tables, the Blender add-on.
  Its own native port (`port/`, SDL_GPU) is kept as a second engine for comparison and for its recordings,
  savestates and sweeps, but new engine features land here. Toolkit-side limits are raised in step with the
  engine's so content that needs the raised limits can be authored and checked (`dc2tk ... --limits port`).
- **Capacity is raised only under `PORT`**: PS2 builds keep retail values. Each limit is one constant in
  `port/include/capacity.hpp`, and the arrays, loops, save records and menus that depend on it are sized from
  it. Saves stay readable: the port's JSON saves carry the counts they were written with, and a retail-sized
  record still loads.
- **The web build reuses DC_Web's layer**: `port/web/` (Vulkan on WebGPU, Vulkan on WebGL2, WasmFS/OPFS
  storage, fibers under Asyncify, the page, the touch pad) ported from DC_Web to this tree's Kazarov. Emscripten
  `6.0.13`, as DC_Web pins.
- **Co-op is MoonMod's**: host-authoritative mirroring over the DC_Web relay (`coop/relay`, protocol v1, rooms
  of four), the `moonmod.coop` mod extended to `chronicle-two`, with the adapter operations it needs
  (avatars, monster mirror, shared doors, world and player save exchange, settings overlay).
- **Nothing ships retail data**, and no test here needs a disc to pass. Checks that need the disc run only
  where one is present and are reported as "not run" otherwise, never as passed.

## Where the limits are

Retail values, from `port/include` (the decompiled headers, under `PORT`), and the target of the first raise.
"Save" says whether the value reaches a save record.

| Limit | Constant | Retail | Target | Save |
|---|---|---|---|---|
| Rooms per generated floor | `AUTOMAP_ROOM_MAX` (`automap.hpp`) | 64 | 160 | no |
| Generator grid cells | `CAutoMapParts[900]` (`dng_main.cpp`) | 900 (30x30) | 3600 (60x60) | no |
| Monsters out at once | `MONSTER_ACTIVE_MAX` (`monster.hpp`) | 24 (upstream port: 60) | 128 | no |
| Monster placement list | `MONSTER_LOCATE_MAX` | 32 | 128 | no |
| Monster kinds loaded per floor | `MONSTER_REFER_MAX` | 12 | 32 | no |
| Treasure chests per floor | `TREASURE_BOX_MAX` (`dng_event.hpp`) | 24 | 128 | no |
| Floor-drop items | `DNG_PULL_ITEM_NUM` (`dng_main.hpp`) | 72 | 256 | no |
| Dungeon floors searched | `DNGMAP_FLOOR_NUM` (`dngfloor.hpp`) | 40 | 128 | yes (floor records) |
| Floor records in a save | `SAVE_DUNGEON_FLOOR_NUM` | 162 | 512 | yes |
| Georama parts a town lists | `GEORAMA_PARTS_LIST_MAX` (`editmenu.hpp`) | 384 | 1024 | no |
| Georama parts placed in a town | `EDIT_DATA_PARTS_MAX` (`editdata.hpp`) | 300 | 1024 | yes (TownLayout JSON) |
| Georama part definitions held | `SAVE_BUILD_PARTS_MAX` (`savedata.hpp`) | 256 | 1024 | yes |
| Houses per town | `EDIT_DATA_HOUSE_MAX`, `EDIT_MAP_HOUSE_MAX` | 32 | 96 | yes |
| Villagers on the main map | `SCENE_VILLAGER_SLOT_NUM` (`scenevillager.hpp`) | 16 | 64 | no |
| Villagers in interiors | `SCENE_SUB_VILLAGER_SLOT_NUM` | 8 | 32 | no |
| Villager records | `CVillagerMngr data[32]`, `NpcBaseData[180]` | 32, 180 | 128, 512 | no |
| Playable characters | `USER_CHARA_NUM` (`userdata.hpp`) | 4 | 4 + registered | yes (accounts) |
| Common item table | `ITEM_COMMON_DATA_MAX` (`gamedata.hpp`) | 432 (+MoonMod slots) | 1024 | item ids in saves |
| Item numbers | `ITEM_NO_MAX` | 512 | 2048 | yes |
| Inventory places | `MENU_BAG_PLACE_MAX` (`menusys.hpp`) | 150 | 400 | yes |
| Weapon records | `local_weapondata[116]` (`gamedata.cpp`) | 116 | 512 | by id |
| Map placements (town/dungeon) | DC2-Toolkit `MAX_PLACEMENTS` 320/400 | 320/400 | 2048 | no |
| Shop goods | `SHOP_ITEM_MAX` (`menushop.hpp`) | 64 | 256 | no |

A raise lands only with: the constant in `capacity.hpp`; every dependent array, loop bound, bit mask and
static assertion sized from it (found by `grep` for the retail number as well as the name); a GoogleTest case
that fills the structure to the new limit and reads it back; and the save round-trip test extended where the
limit reaches a save.

## Work packages

Each is a branch commit on `claude/dark-cloud-2-engine-yr3rlk`, in the repository named, and each has its
gauntlet gate (GAUNTLET.md).

| # | Package | Repository | Gate |
|---|---|---|---|
| A | Capacity layer in the engine host | ChronicleTwo | G1 |
| B | Toolkit limits in step; `--limits port`; Blender budgets | DC2-Toolkit | G1 |
| C | Web build (`PLATFORM=WEB`, `port/web/`) | ChronicleTwo | G3 |
| D | Co-op for Chronicle Two (adapter operations, transports, the mod) | ChronicleTwo, MoonMod, DC_Web | G4 |
| E | Content registration: playable characters, NPCs, weapons, items, Georama parts | ChronicleTwo, MoonMod | G2 |
| F | The gauntlet: runner, report, gates, agent protocol | ChronicleTwo, DC2-Toolkit | G0 |
| G | Upstream merge: 7a7d7f29 to upstream master | ChronicleTwo, MoonMod | G0 again |

## Upstream

Upstream moves fast (109 commits between DC2-Toolkit's engine pin `d51e74b` and master `1dc0c47`). The
rule from DC1 holds: our changes live in `port/` and `tools/kazarov`, never in `ps2/`, and are kept as the
smallest hooks in game units with the body in `port/src/moonmod/`, `port/src/web/` or `port/include/capacity.hpp`,
so a merge touches few lines. After every merge the whole gauntlet runs again, and MoonMod's
`ports/chronicle-two/` patch is regenerated from this tree (`tools/gauntlet/export_moonmod_patch.py`).

## Building here

What this tree was built and checked with on Linux x86-64 (Ubuntu 24.04, 4 CPUs, no GPU):

- clang 20 with libc++ and lld (`apt.llvm.org`), CMake 3.28, Ninja; Rust 1.99 (`rust-toolchain.toml`).
- SDL3 3.4.18 built from the release tarball and installed to `/usr/local`; Vulkan 1.4.363 headers
  (`KhronosGroup/Vulkan-Headers`); Ubuntu's loader and Mesa `lavapipe`; `glslang-tools`; Ubuntu's FFmpeg 6.1
  through a tree of symlinks (`KZ_FFMPEG_DIR`), as MoonMod's verification did.
- The pinned third-party sources CMake fetches (Corrosion 0.6.1, simdjson 4.6.11, fastgltf 0.9.1, Luau 0.740,
  quill 13.1.0, cpptrace 1.0.4, libdwarf-lite `5dfb2cd`, Tracy `30997d5`, GoogleTest 1.18.0) cloned by git and
  passed as `FETCHCONTENT_SOURCE_DIR_<NAME>`, because the archive URLs were unreachable.
- Emscripten 6.0.13 from its release tarball, with `.emscripten` pointing at the system Node 22.

`tools/gauntlet/env.sh` sets these up; `tools/gauntlet/run.py` builds and checks everything (GAUNTLET.md).
