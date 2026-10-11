# padcontrol notes

Unit owns one class, `CPadControl` (6 members, all in this unit). No virtuals, no vtable, no
constructor/destructor symbols. No global data of its own; the single instance is `PadCtrl`
(mainloop, BSS, size 0x510, declared in `mainloop.hpp`). No first-game counterpart (the first
game has no `CPadControl`).

## CPadControl layout (size 0x510)
Size: `PadCtrl` symbol size 0x510 = 0x10 + 128*8 + 32*8.

| Offset | Field | Evidence |
|---|---|---|
| 0x0 | `float rx` | Update: `swc1` of `GetRXf` result; read for axis 3 |
| 0x4 | `float ry` | Update: `GetRYf`; read for axis 4 |
| 0x8 | `float lx` | Update: `GetLXf`; read for axis 1 |
| 0xC | `float ly` | Update: `GetLYf`; read for axis 2 |
| 0x10 | `PAD_CTRL_BTN btn[128]` | stride 8 (`sll 3`), bound `< 0x80` in RegisterBtn/Btn/Update/Initialize |
| 0x410 | `PAD_CTRL_ANALOG analog[32]` | stride 8, bound `< 0x20` in RegisterAnalog/Analog/Update/Initialize |

`PAD_CTRL_BTN` {0x0 `int value`, 0x4 `int config`}: Btn returns `value`; Update stores the result
of `CGamePad::On/Down/Up(config & 0xFFFF)` into `value`, selected by `config & 0xF0000`
(0 / 0x10000 / 0x20000; any other trigger leaves value untouched); `config == 0` is skipped.
RegisterBtn stores `value = 0`, `config = trigger | button` (asm `or $3, $7, $6`).

`PAD_CTRL_ANALOG` {0x0 `float value`, 0x4 `int axis`}: Analog returns `value` (lwc1). Update
switch on `axis`: 1 -> lx, 2 -> ly, 3 -> rx, 4 -> ry, 0/other -> untouched. RegisterAnalog stores
`axis`, `value = 0`. Struct and field names are neutral (no retail names exist).

Initialize zeroes only `config` of each button and `axis` of each analog (offsets 0x14+8n and
0x414+8n), loop unrolled by 8 (counter += 8, offset += 0x40). Values are not cleared.

## Functions
- `Initialize`, `RegisterAnalog`, `Btn`, and `Analog` compile and match the linked retail image.
  `RegisterBtn` and `Update` have named compiling drafts behind `NONMATCHING`; their single
  promotion attempts differed from retail, so the normal build retains their assembly.
- `int RegisterBtn(int no, int button, int trigger)` / `int RegisterAnalog(int no, int axis)`:
  return 1 if `0 <= no < max`, else 0. Parameter order from InitPadTable (mainloop):
  `RegisterBtn(&PadCtrl, row.no, row.button, row.trigger)` with `PAD_TABLE_ENTRY {no, trigger,
  button}` (0xC rows, `pad_table`); `RegisterAnalog(&PadCtrl, row.no, row.axis)`
  (`analog_table`). Both tables end at `no < 0`.
- `int Btn(int no)`, `float Analog(int no)`: return 0 / 0.0f when out of range.
- `void Update(CGamePad *pad)`: called per frame with the GamePad.
- Range checks compile as `if (no >= 0 && no < MAX) {...} return 0;`-style; m2c shows a goto from
  the early `bltz`, likely `if (no < 0 || no >= MAX) return 0;`.

## Enums
- `PadCtrlTrigger`: 0 On (held), 0x10000 Down (pressed), 0x20000 Up (released); masks 0xF0000 /
  0xFFFF. Seen in Update and in `pad_table` data (column 1 values 0 and 0x10000).
- `PadCtrlAxis`: 0..4 as above; `analog_table` binds axes 0->1, 1->2, 2->3, 3->4, 4->2, 5->1.

## Unresolved
- The second bindings have names: buttons 4 (`PAD_BTN_L2`, read by CCameraControl::MoveCamera),
  6 (`PAD_BTN_R2`, read by editctrl), 0x35 (`PAD_BTN_L1_HELD_2`) and 0x36 (`PAD_BTN_R1_HELD_2`,
  read by pbuggy); analog 4 and 5 (`PAD_ANALOG_LEFT_Y_2` / `PAD_ANALOG_LEFT_X_2`, read by fishing,
  editctrl and pbuggy) and analog 6 and 7 (`PAD_ANALOG_RIGHT_X_2` / `PAD_ANALOG_RIGHT_Y_2`, read by
  CCameraControl::MoveCamera). The caller reads of these numbers in `ps2/src` use the names.
- `PAD_TABLE_ENTRY::trigger` in mainloop.hpp is `int`; it could use `PadCtrlTrigger` (mainloop's
  header, not edited here).
