# crandom: reverse-engineering notes

## CRandom (size 0x4)
- `0x0 seed` (u32): the only offset `nget` touches. Each step is `seed = seed * 0x5D588B65 + 1`
  (stored back to memory after every step). The value is converted to float as unsigned (m2c
  shows the `(x >> 1 | x & 1) * 2` unsigned-to-float sequence) and divided by 4294967296.0f.
- Size 0x4: the only instance seen is a local in `FishModifyParam` (gyoracesim) at `sp+0x7C`,
  the last word of a 0x80-byte frame, so nothing follows the seed.
- No constructor, vtable or other member in the manifest. `FishModifyParam` sets the seed inline:
  it hashes a string into an int (0 becomes 1), stores it at `sp+0x7C`, steps the LCG 1000
  times (unrolled by 8) in place, then calls `nget` five times (`value * 0.03f + 1.0f`). The
  seed store and warm-up may be an inline member (e.g. a seeding function) that retail never
  emitted out of line; its name is unknown, so it is not declared.
- No first-game counterpart (no `CRandom` in `/home/adubbz/development/chronicle`).

## nget (0x321850, 0x310)
- `nget` = normal get: sums 12 uniform [0,1) values and subtracts 6.0f (Irwin-Hall approximation
  of a standard normal). Retail code is the 12-iteration loop unrolled by 8 (one 8-step block,
  then a remainder loop for counts 8..11), the usual MWCC unrolling of `for (i = 0; i < 12; i++)`.
- Returns float in `$f0`.

## abs(float) (0x321B60, symbol size 0x24)
- Global (called from `LaneBattleStep` in gyoracesim). `if (x < 0.0f) x = -x; return x;`.
- Overloads `int abs(int)` from `<cstdlib>`; no conflict.

## Matching source
- `nget` matches as a plain `for (int i = 0; i < 12; i++) { seed = seed * 0x5D588B65 + 1;
  sum += seed / 4294967296.0f; }` with `seed` as `u32`.
- `abs(float)`: `return value < 0.0f ? -value : value;` matches the retail code exactly.
  MWCC leaves the branch delay slot empty and moves the selected value to the return register
  in the `jr` delay slot. The equivalent `if` form moves it earlier and does not match.
