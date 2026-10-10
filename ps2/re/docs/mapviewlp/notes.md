# mapviewlp notes

Debug map viewer mode of the main loop. All three functions are 0x10-byte stubs in retail; the unit
owns no classes, structs, enums or global data (no `INCLUDE_RODATA`/`INCLUDE_BSS` in the `.cpp`).
No first-game counterpart exists.

## Functions
| Function | Body | Notes |
|---|---|---|
| `MapViewInit(INIT_LOOP_ARG)` | `jr $31; nop` | empty; `void` |
| `MapViewExit()` | `jr $31; nop` | empty; `void` |
| `MapViewLoop()` | `jr $31; addiu $2,$0,1` | `return 1;` declared `int` (first game's mode loops return `int`) |

Retail spellings (`Textuer`, `Viewe` in the Finish functions) are kept as in the symbols.

## How the main loop uses them
- `mainloop` tables `LoopInit` (0x3392E0), `LoopMain` (0x339310), `LoopExit` (0x339340), each
  0x28 bytes = 10 function pointers. This unit is index 6 in each.
- `MainLoop` calls `LoopInit[LoopNo](&NextInitArg)` (struct passed by value = pointer to copy),
  then per frame `LoopMain[LoopNo]()` and leaves the mode when the result is non-zero, then
  `LoopExit[LoopNo]()`. Returning 1 therefore exits the mode on its first frame.

## INIT_LOOP_ARG
- Not owned by any class in `class_units.tsv`; its user is `mainloop` (`NextLoop(int, INIT_LOOP_ARG)`,
  global `NextInitArg` at 0x3FB060, size 0x50). The header includes `mainloop.hpp`, which defines it.
- Size 0x50 (NextLoop copies 10 x 8 bytes). From NextLoop: 0x00 a 4-byte word; 0x04..0x43 a
  0x40-byte block copied bytewise (likely `char[0x40]`); 0x44, 0x48, 0x4C three 4-byte words.
  Field meanings unknown here.
- The source includes `mainloop.hpp` for the complete argument type. All three definitions
  match retail byte for byte.
