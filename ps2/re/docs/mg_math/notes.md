# mg_math notes

Engine maths helpers (`mg` prefix). No class is owned by this unit (`class_units.tsv`); the header
declares 60 free functions, two enums and three globals. First-game counterpart: the un-prefixed
helpers in `chronicle/ps2/include/mathutil.hpp` / `src/mathutil.cpp` (`VectorMaxMin`, `DistVector`,
`AngleInterpolate`, `Sinf`, ...). This game's versions are mostly VU0 inline asm (`lqc2`/`sqc2`,
`vmax`/`vmini`, `ctc2`/`cfc2` on the status flag) rather than C.

## Types used
- `mgVu0FBOX` is owned by `mg_drawenv` (header not written yet); forward-declared here. Layout seen
  from this unit and its callers: two quadwords, **max corner at 0x00, min corner at 0x10**
  (mgBoxMaxMin stores vmax to +0, vmini to +0x10; mgApplyMatrix callers in mapparts pass
  `box, box+0x10` as `max, min`; mgInsideScreen passes `box, box+0x10` to mgCreateBox8(max, min)).
- `mgInterpolateMode` (name not retail): mode argument of mgVectorInterpolate / mgAngleInterpolate.
  0 = fixed step (snap when closer than step), 1 = gap / step. Other values: VectorInterpolate
  writes nothing; AngleInterpolate returns `from` wrapped. Parameter stays `int` for mangling.
- `mgPointPoly3Result` (name not retail): return of Check_Point_Poly3 / mgCheckPointPoly3_XZ.
  With cross products c0 (edge v0->v1), c1 (v1->v2), c2 (v2->v0): out of bounding rectangle -> 0;
  c0 == 0 -> 2; c1 == 0 -> 3; c2 == 0 -> 4; all > 0 or all < 0 -> 1; else 0.
  mgCheckPointPoly3_XYZ returns only 0/1 (sign test of three cross products dotted with normal;
  all >= 0 or all <= 0 -> 1).

## Globals
- `sin_table_num` (.sdata 0x37C700, float): set to 1024.0 by mgCreateSinTable, used as divisor.
- `sin_table_unit_1` (.sdata 0x37C704, float): set to 0x4322F983 = 162.97466 = 1024 / (2*pi);
  mgSinf multiplies the angle by it.
- `SinTable` (.bss 0x395EC0, 0x1000 = float[1024]): sin(2*pi*i/1024).
- First game had all three as file `static`; ELF binding for this game is not available in the
  tree, so they are declared `extern` per the brief. If retail turns out local, drop the externs.

## Function details (for the body writer)
- Sizes in the header are retail symbol sizes (main.symbols.txt), not the padded manifest sizes.
- Clip functions return the result of `(cfc2 status & 0x80) == 0` (sticky sign flag; `ctc2 $0`
  clears status first). Return type taken as `int`; callers only test != 0.
  - mgClipBoxVertex(p, max, min): vsub.xyz max-p, p-min.
    Five `vnop` instructions wait for the VU status update before `cfc2`.
  - mgClipBox(max0,min0,max1,min1): vsub.xyz max0-min1, max1-min0 (overlap).
    Either negative result sets the sticky sign flag; five `vnop` instructions
    wait before the flag is read and converted to a Boolean return.
  - mgClipInBox: vsub.xyz max1-max0, min0-min1 (box0 inside box1).
    Both vectors must be nonnegative on x, y and z for containment.
  - W variants use `.xyw` masks (screen-space boxes, mg_frame).
    `mgClipBoxW` uses the overlap operands of `mgClipBox`; z is excluded and
    w participates in the sticky-sign test instead.
- mgZeroVector: `sq $zero` (all four zero). mgZeroVectorW: `sqc2 vf0` -> (0,0,0,1).
- mgFotI4: `lqc2` loads four floats, `vftoi4.xyzw` converts each lane to a signed
  integer after multiplying by 16, and `sqc2` stores all four results.
- mgAddVector: `lqc2` loads both four-component vectors, `vadd.xyzw` adds
  their corresponding components, and `sqc2` writes the result to the first.
- mgSubVector: `lqc2` loads both four-component vectors, `vsub.xyzw` subtracts
  the second from the first, and `sqc2` writes the result to the first.
- mgVectorMin(out, a, b): `vmini.xyzw` selects the lower value in each of
  the four lanes, including w, then `sqc2` stores the result.
- mgVectorMin(out, a, b, c, d): three consecutive `vmini.xyzw` operations
  reduce four vectors to one per-lane minimum, including w.
- mgVectorMaxMin(max, min, a, b): `vmax.xyzw` and `vmini.xyzw` select both
  bounds across all four lanes and store them to separate vectors.
- mgVectorMaxMin(max, min, a, b, c): a second `vmax.xyzw`/`vmini.xyzw` pair
  folds the third input into both four-lane bounds before storing them.
- mgVectorMaxMin(max, min, a, b, c, d): three paired VU reductions produce
  the per-lane maximum and minimum of all four input vectors.
- mgBoxMaxMin(box, other): paired VU reductions take the maximum and minimum
  across both corners of both boxes, so the result also normalizes reversed
  corners. Both results include the w component and are stored in `box`.
- mgPlaneNormal(out, v0, v1, v2): subtracts v0 from v1 and v2, then uses
  `vopmula.xyz`/`vopmsub.xyz` to form their cross product. The result is
  unnormalized; the masked operation does not assign `vf12.w` before the
  whole quadword is stored, so the output w component is unspecified.
- mgZeroMatrix: `vsub.xyzw` clears all lanes of `vf1`, then four `sqc2` stores
  write the rows of the matrix, with row zero in the return delay slot.
- mgUnitMatrix: three `vmr32.xyzw` rotations of VU0's constant `vf0` form
  the x, y and z identity rows; `vf0` supplies the w row, and four `sqc2`
  stores write them in reverse row order.
- mgCreateBox8(out[8], max, min): out[0]=min, out[7]=max, others mixed via vaddx.x/.y/.z with vf0.
  Each mixed corner starts as a four-lane copy of max or min, then a masked
  `vaddx` copies one selected component from the opposite bound; every corner
  retains the source vector's w component.
- mgDistVector*/XZ: vmul then vmr32 sums; non-squared ones use vsqrt + vwaitq, result via
  `cfc2 vi22` (Q). XZ sums x and z only. Squared ones via qmfc2.
- mgDistVector2(vector): squares the x/y/z lanes with `vmul.xyz`, rotates
  them so the x lane accumulates x²+y² then z²+(x²+y²), and transfers that
  lane through `qmfc2` and `mtc1` to the float return register.
- mgDistVector(vector): uses the same x/y/z accumulation, then `vsqrt` writes
  the VU Q register. After `vwaitq`, `cfc2 vi22` and `mtc1` return its value.
- mgDistVectorXZ(vector): squares x/y/z but rotates z² into the x lane and
  adds only x²+z² before the VU Q square root; y does not affect the result.
- mgDistVector2(a, b): subtracts `b-a` in the x/y/z lanes, squares those
  lanes, accumulates x²+y² then z²+(x²+y²), and returns the raw float bits
  from VU0 through `qmfc2` and `mtc1`.
- mgDistVectorXZ2(a, b): uses the same masked subtraction and squaring but
  adds only x²+z² before returning the VU x lane through `qmfc2`.
- mgDistVector(a, b): follows the two-vector squared-distance lane sequence,
  then takes the VU Q square root and transfers Q to the float return register.
- mgDistVectorXZ(a, b): subtracts `b-a`, squares xyz, accumulates x²+z² in
  one VU lane, and returns its VU Q square root after `vwaitq`.
- mgDistPlanePoint(n, on_plane, p) = n . (p - on_plane) (sceVu0SubVector + sceVu0InnerProduct).
  Ghidra shows it void; mgReflectionPlane consumes its $f0, so it returns float.
- mgReflectionPlane: d = DistPlanePoint; out = (on_plane - p) - n*(-2d); returns 2d.
- mgDistLinePoint(p, a, b, nearest): parameter t of projection onto segment; outside [0,1] picks
  the nearer endpoint (sceVu0CopyVector) and returns its distance.
- mgIntersectionSphereLine0(r, from, to, hits): sphere at origin, solves quadratic; returns 0, 1
  or 2. mgIntersectionSphereLine(sphere, ...): radius = sphere[3]; translates into sphere space,
  calls the 0 variant, adds the centre back to each hit with mgAddVector.
- mgIntersectionPoint_line_poly3(from, to, v0, v1, v2, normal, hit): 0 if line parallel to plane,
  else hit = from + (to-from)*t and returns mgCheckPointPoly3_XYZ(hit, v0, v1, v2, normal).
- mgCheckPointPoly3_XZ: tail-jumps to Check_Point_Poly3(p.x, p.z, v0.x, v0.z, ...).
- MulMatrix3(m, a, b): loads m, a, b; computes (m*a) then that *b with the same vmula/vmadda
  pattern as mgMulMatrix, stores into m. Used by mgRotMatrixXYZ(m, rot): Z(m), Y, X ->
  MulMatrix3(m, Y, X).
- mgRotMatrixX/Y/Z start from mgUnitMatrix then set cos/sin with libm `cosf`/`sinf` (not mgSinf).
- mgCreateMatrixPY: unit, sceVu0RotMatrixY(m, m, angle), m[3] = position with w = 1.
- mgInversMatrix: VU0 cofactor inverse of the 3x3 part, divided by determinant (vdiv), translation
  row = -(t * R^-1), w = 1.
- mgLookAtMatrixZ: two matrices (pitch about x using y component, yaw about y using xz length),
  multiplied with mgMulMatrix.
- mgShadowMatrix(m, light_dir, on_plane, normal): light_dir xyz with w=0, normalised; if
  normal . on_plane == 0, on_plane -= normal*0.1 first; builds planar projection along light.
- mgApplyMatrixN(out, m, in, n): n vectors. mgApplyMatrixN_MaxMin: same plus vmax/vmini bounds.
- mgVectorMinMaxN(max, min, v, n): seeds with v[0] and loops n times comparing v[1]..v[n] (reads
  n+1 vectors); callers in mg_visual/visualmotion should be checked for the count they pass.
- mgApplyMatrix(max, min, m, box_max, box_min) = CreateBox8 + ApplyMatrixN_MaxMin(.., 8, ..)
  in a 0x80-byte stack buffer.
- mgAngleCmp(a, b, tol): d = a-b wrapped to (-pi, pi]; 0 if d == 0; 1 if d > tol; -1 if d < -tol.
- mgAngleLimit: if |a| >= pi, a -= (int)(a / 2pi) * 2pi then one more wrap step.
- mgRnd: rand() / 2147483648.0f. mgNRnd: sum of 12 mgRnd() - 6.0.
- mgSinf: index = (int)(|a| * sin_table_unit_1) % 1024 (signed modulo code), negated for a < 0.
  mgCosf: mgSinf(a + pi/2) (tail call).

## Unresolved
- Retail ELF binding (global vs local) of MulMatrix3, Check_Point_Poly3, mgDistPlanePoint,
  mgIntersectionSphereLine0, mgRotMatrixX/Z, mgApplyMatrixN_MaxMin (only called within this unit)
  could not be checked; all are declared in the header.
- Clip functions could return `bool` instead of `int`; codegen (sltiu) is the same either way.

## Job mg_math.1 (first 50 functions)
- Binding (local_symbols.tsv): `MulMatrix3` and `Check_Point_Poly3` are file-local -> `static` in
  the .cpp, removed from the header (Check_Point_Poly3 has a static prototype at the top because
  mgCheckPointPoly3_XZ precedes it). `sin_table_num`, `sin_table_unit_1`, `SinTable` are not
  listed, so they are global: the header's externs are right.
- Correction: mgVectorMinMaxN reads only `count` vectors. It seeds with v[0], preloads v[1], but the
  loop advances the pointer before reloading, so v[1] is folded twice and the last folded is
  v[count-1].
- The VU0 functions are MWCC `asm` bodies on raw argument registers ($4..$9).
  The clip functions test the sticky sign bit (`MG_VU0_STATUS_SIGN_STICKY`).
- Loop asm (mgApplyMatrixN, _MaxMin, mgVectorMinMaxN; retail marks them "handwritten") uses
  `.set noreorder`, so each `bgez` delay slot is the instruction written after it.
- Remaining DIFFs are logic-faithful: mgDistLinePoint, mgIntersectionSphereLine0, Check_Point_Poly3
  (scheduling / comparison form); mgCreateMatrixPY copies the position as one quadword in retail
  (`lq`/`sq`) then sets w = 1; mgShadowMatrix (expression order).
- mgShadowMatrix: the plane normal is scaled by 1 / (normal . point) so the plane is n.x = 1;
  with l the normalised light and d = n.l, m[i][j] = -(n_i l_j - [i==j] d) / d, row 3 = l / d,
  m[3][3] = 1, column 3 rows 0-2 = 0.
- mgLookAtMatrixZ calls mgDistVector once (the first game's version called it twice).

## Job mg_math.2 (last 10 functions)
- All ten are plain C++ and promoted; they follow the first game's mathutil.cpp except where noted.
- mgVectorInterpolate STEP mode differs from the first game: it measures the whole gap with
  mgDistVector, snaps with a quadword copy (`*(u_long128 *) out = *(u_long128 *) to`, lq/sq), else
  Normalize + ScaleVector by step + AddVector. FRACTION mode is per-component from + gap / step.
- mgAngleLimit calls fptosi once (`angle -= 2pi * (int) (angle / 2pi)`); the first game's
  version truncated twice.
- mgCosf is `mgSinf(1.5707964f + angle)` (tail jump). `sin_table_unit_1 = 162.97466f` gives
  retail's 0x4322F983.

## Loop functions

`mgApplyMatrixN`, `mgApplyMatrixN_MaxMin`, and `mgVectorMinMaxN` are `asm` bodies that apply the
matrix to each vector and update four-component bounds. The supported callers pass a positive count.

## VU0 arithmetic functions

The VU0 arithmetic functions are `asm` bodies in `mg_math.cpp` that match the retail VU0 instruction
sequences.

- `mgFotI4` scales four lanes by 16 before integer conversion (`vftoi4.xyzw`).
- `mgCreateBox8` emits the eight corners in the VU0 store order: minimum; maximum x/y/z separately;
  three maximum corners with x/y/z respectively replaced by minimum; maximum. The four-component
  loads and stores preserve the input w components.
- `mgZeroVectorW` and `mgAddVector`/`mgSubVector` operate on all four lanes.
- The five `mgClip*` functions test whether any of the two vector differences has a negative lane,
  as the cleared VU0 sticky sign bit does. The W forms test x, y, w; the others test x, y, z.
- `mgVectorMin`, `mgVectorMaxMin`, and `mgBoxMaxMin` compare four lanes; scalar conditional
  comparisons may choose a different NaN operand than `vmini`/`vmax`.
- `mgPlaneNormal` takes the cross product of the two edges starting at the first point. Its w lane
  is modeled as zero; the retail VU0 operation only writes xyz, so the exact stored w value needs
  a register-level match.

- The seven `mgDistVector*` functions sum squared x/y/z or x/z differences, then take a square root
  for the distance forms.
- `mgUnitMatrix` and `mgZeroMatrix` write all sixteen scalar elements. `mgMulMatrix` multiplies two
  4x4 matrices, and file-local `MulMatrix3` computes `(matrix * second) * third` with temporaries
  to preserve aliasing. The retail functions use VU0 multiply-add sequencing.
- `mgInversMatrix` computes the inverse of the 3x3 linear portion using cofactors, then the
  translated fourth row. Like retail, it has no singular-matrix guard.

`mgZeroVector` now uses a 128-bit integer store through the four-float vector
address. MWCC emits retail's `jr ra` with `sq zero,0(a0)` in the delay slot;
the isolated linked image matches. The reinterpretation is needed to request
one PS2 quadword store from C++. `mgZeroVectorW` uses the VU-only inline
assembly exception: retail stores VU0 constant `vf0` with `sqc2`, which a
scalar or integer C++ store does not express.
`mgFotI4` also uses the VU-only exception because C++ cannot express the
four-lane `vftoi4.xyzw` conversion. `mgAddVector` uses the VU-only exception for
its four-lane `vadd.xyzw`.
`mgSubVector` uses the VU-only exception for its four-lane `vsub.xyzw`.
`mgZeroMatrix` and `mgUnitMatrix` use the VU-only exception for their
quadword stores and vector instructions.
`mgCreateBox8` uses the same exception because its masked VU component
operations produce the eight four-component box corners.
The two-input `mgVectorMin` uses the exception for VU0's per-lane minimum.
The four-input overload uses the same VU minimum operation three times.
The two-input `mgVectorMaxMin` uses VU maximum and minimum operations for
both output vectors.
The three-input overload uses a second VU reduction stage for the third vector.
The four-input overload uses a third stage for the fourth vector.
`mgBoxMaxMin` uses the same VU reduction over the two corners of each box.
`mgPlaneNormal` uses VU0's outer-product accumulator instructions; its old
C++ draft's explicit zero for w did not reflect the masked VU destination.
The single-vector `mgDistVector2` uses VU0's lane rotation and accumulation
order to preserve the retail floating-point result.
The single-vector `mgDistVector` uses the same accumulation and VU Q square
root, which differs from a scalar `sqrtf` call.
The XZ overload keeps only the squared x and z components in the Q input.
The two-vector `mgDistVector2` adds a VU masked subtraction before the same
three-lane squared accumulation.
The two-vector `mgDistVectorXZ2` omits y from that final accumulation.
The two-vector `mgDistVector` follows the squared three-lane path with the VU
Q square root and wait.
The two-vector `mgDistVectorXZ` uses only x and z in the Q input.
`mgClipBoxVertex` uses VU0's sticky sign flag across its two masked
subtractions; the integer return path tests bit 0x80 after the VU pipeline wait.
`mgClipBox` applies the same status test to the two box separation vectors.
`mgClipBoxW` performs that test on x, y and w only.
`mgClipInBox` uses the sticky VU sign flag for containment rather than overlap.

The VU0 vector and matrix routines in this unit are inline `asm` bodies in `mg_math.cpp`.
